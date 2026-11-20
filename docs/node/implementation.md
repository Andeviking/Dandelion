# Node Implementation Plan

This plan records the implemented first-version storage and algorithms.
Phase 1 and Phase 2 additions are implemented and have recorded verified test
runs. Phase 4A implements the Phase 1C numeric boundary and storage. Internal
structures can change while preserving the observable semantics, ownership,
and error guarantees in [design.md](design.md) and [operators.md](operators.md).

## 1. Stores and internal representations

`ManagerState` contains the sort, sort-constructor, declaration,
declaration-instance and datatype-group stores; the weak node table; the node
and state reference/identity counters; and allocation bookkeeping. Published
sorts and declarations live until state destruction. Their records use local
IDs, strings, immutable values and flat recipes, not owning public handles.
Sort records have stable addresses. Declaration IDs index a dense vector of
records. On the supported 64-bit ABI a declaration record uses 56 bytes.
Its three 64-bit identities, string view, datatype pointer, and kind require
49 bytes before alignment. A declaration with an empty concrete domain stores
a zero domain ID.
Nonempty domains use dense span records referenced by that ID. Names and
domain elements are stored in separate append-only blocks whose addresses
survive growth of the record and span vectors. Large payloads get blocks
sized to the request. A failed publication restores both block cursors to
their previous positions.
Manager-local instance IDs index compact entries that contain a declaration
ID and, when applicable, the index of a stable substituted signature.
An ordinary declaration stores its own concrete signature and remembers its
single instance after publication. Datatype-only recipe data is allocated only
for datatype declarations. Parameterized instances use a transparent
`unordered_set` keyed by declaration and arguments. A nonparameterized
datatype declaration also caches its single instance, including its
substituted signature.

### Phase 1 BV width cache

Use one internal BV sort ID lookup shared by both `MakeBvSort` overloads and
width-producing builtin inference. Check positivity and the `uint32_t`
limit before indexing or narrowing. Derived width calculations must also
check for arithmetic overflow before lookup. The initial cache in
`ManagerState` is a fixed array of 64 `SortId` slots for widths 1 through
64, indexed by `width - 1`. This covers widths through one machine word
with a direct lookup. Zero marks an empty slot. Wider valid widths use the
ordinary sort interner.

A populated slot returns the canonical ID without probing the sort table.
Fill an empty slot only after the ordinary interner returns a published
sort ID. Failed interning leaves the slot empty. Published sorts live as
long as their state, so the cache needs no invalidation or owning handles.
The public factories still acquire a checked state reference for the
owning result. Inference uses the borrowed ID within the manager state.
Facades attached to the same state share the cache.

### Store publication

Reserve each destination store before publishing an instance. If the instance
needs a hash index, insert the prepared record after reserving the ID and
signature vectors. Publish the entries with nonthrowing moves. On failure,
existing objects stay valid and no published ID is consumed. Capacity changes
and valid result sorts may remain. Ordinary and fresh declaration identities
are independent of their names and signatures.

A datatype builder has a stable private staging allocation and owns one state
reference. It copies source names and maintains append-only recipe tokens.
Tokens are valid only while that builder is open and cannot be passed to a
different builder. Classify parameter ownership once in recipe order and use
the result to validate each field. A field with a direct recursive dependency
contributes one pending dependency to its constructor. Use reverse edges
from a productive datatype to its dependent constructors. Each dependency
is processed once, and one constructor reaching zero pending dependencies
makes its datatype productive. This computes the fixed point described in
[design.md](design.md#4-datatype-group-transactions).
Prepare all records and reserve all destination vectors before assigning IDs
or publishing any group member. A successful commit transfers the builder's
state ownership to the returned group handle, frees staging storage, and
closes the builder. A failed
commit stays open: appending a missing base constructor can repair a
productivity failure; other irreparable staged records require `Abort()` and a
new builder. An `Abort()` or failed commit publishes no name or record.
Frontend name checks do not replace the builder's structural validation.

## 2. NodeData and payload

The implemented node header occupies 56 bytes on the supported 64-bit builds:

```cpp
struct NodeData {
  ManagerState* state;
  std::byte* storage;
  std::uint64_t id;
  union {
    std::uint64_t ref_count;
    NodeData* reclaim_next;
  };
  std::uint64_t structural_hash;
  std::uint64_t sort_id;
  std::uint16_t inline_child_count;
  std::uint8_t kind_code;
  PayloadTag payload_tag;
  std::uint32_t literal_limb_count;
};
```

The node retains the storage-providing byte array for offset calculations.
After the reference count reaches zero and the table entry is erased, the
reclamation stack activates the link member of the union. Before retirement,
only the reference count is active. The cached hash allows erasure without
traversing the structural key. A child count of 65,535 or greater uses the
16-bit sentinel and a full 64-bit count in the byte array before the child
slots. The public count range and 64-bit identity are independent of this
layout. The header and payload shapes are private implementation details.

| Payload form | Constructed object |
| --- | --- |
| No payload | No additional object. |
| Bool and rounding mode | The corresponding scalar. |
| Int, BV, and FP literals | Word, inline limb array, or tag-only encoding as detailed below. |
| Application | A `DeclInstanceId`. |
| Indexed operation | One or two 32-bit indices, or an owned `Integer` when a permitted unbounded index exceeds 32 bits. |
| Rational FP conversion | Two 32-bit format parameters followed by compact words or packed magnitudes. |

The factory validates sign, arity, sort, and bounds, then selects the unique
representation of the mathematical value. Borrowed index and literal views
expose the same exact values across all layouts. The sort stores the BV width
or FP format. A const-array result sort is the complete array qualifier and
participates in its structural key. Payloads own their exact values
independently of caller objects, and padding does not participate in equality
or hashing.

### Phase 1C numeric payloads

The 56-byte header uses four former padding bytes for the trimmed
`uint32_t` limb count on wide literals.
Literals have no children. The sizes below are requested node block sizes on
the supported 64-bit layout, including the header.

| Payload form | Tail and requested bytes |
| --- | --- |
| `kIntWord` | One signed `int64_t` for every value in the signed word range, including `INT64_MIN`. **64 bytes**. |
| `kBvWord` | One `uint64_t` for every magnitude that fits in a word. The sort supplies the width. **64 bytes**. |
| `kFpWord` | One `uint64_t` for finite nonzero packed bits that fit in a word. The sort supplies the format. **64 bytes**. |
| FP specials | Separate payload tags for NaN, positive and negative infinity, and positive and negative zero. The tag carries the entire category. **56 bytes** with an empty tail. |
| Wide Int, BV, and finite FP | One aligned inline array of `L` significant `uint64_t` limbs in increasing order of significance. The header holds `L`, and two wide Int tags distinguish positive and negative magnitudes. **`56 + 8L` bytes**. |
| `kRationalConversion` (small) | One RM child slot, two `uint32_t` format indices, an `int64_t` numerator and a positive `uint64_t` denominator. **`56 + 8 + 8 + 16 = 88` bytes**. |
| Rational conversion (wide) | After the same child slot and format indices, store `uint32_t` numerator and denominator limb counts, then their trimmed magnitude arrays in that order. Two private wide tags encode the numerator sign. **`80 + 8(Ln + Ld)` bytes**. |

Zero Int and BV values use their word forms. A wide magnitude has a nonzero
highest limb and may contain zero lower limbs. Wide BV and finite FP require
at least two significant limbs, regardless of the declared width or format.
An Int outside the signed word range can still have a one-limb magnitude,
including `2^63` and `-2^63-1`. It uses the wide Int encoding with `L = 1`,
still requesting 64 bytes. Extract signed magnitudes with unsigned operations
so the magnitude of `INT64_MIN` cannot overflow. The little-endian limb order
refers to significance. Each limb uses native object representation.

A small rational has a signed word numerator and an unsigned word
denominator, matching the [util small representation](../util/rational.md#canonical-value-and-storage).
Whenever either component exceeds its word range, pack both normalized
magnitudes into the wide rational tail. Normalized zero stays in the small
representation as `0/1`. The wide counts determine each component's span
without separate pointers or allocations. Existing layout rules for
unbounded builtin indices (`kIndexWide`) still use an owned Integer with
separate backing when required.

### Phase 1C construction, borrowing, and reclamation

Select the payload tag from the checked canonical input before hashing. A
signed-word Int carries its normalized word through hashing, comparison, and
construction. Wide keys borrow numeric views and use their exact magnitudes,
including an inline unsigned word above `INT64_MAX`. Every construction path
uses the same canonical tag and value. Structural identity includes the
explicit literal sort, the Int sign, the FP special category, and the full
applicable limbs. Rational conversion identity includes
both format indices and both exact normalized components. Read only
initialized fields. Storage addresses and padding are outside the key.
A hit requires only the checked result retain after lookup.

Use one layout calculation for candidate sizing and accounting. On a miss,
check count conversions, array lengths, alignment, additions, and backing
size before allocating. For the packed rational this includes the sum
of the two array lengths. Start aligned `uint64_t` array lifetimes
in the byte block and copy significant limbs. Word, packed, and tag-only
payloads need no secondary owner or allocation. With the table capacity and
result sort already available, a wide literal miss needs a single `NodeData`
allocation.

Build util views from the node's word or immutable limb span, with the sort
supplying the BV width or FP format. A rational view reads both packed
components directly. The view representations accept canonical words and
read-only limb spans independently of their owner's type. The numeric
module can therefore consume these views with no dependency on Node.
These views acquire no ownership and remain valid while the node is live,
including across unique-table growth. The numeric implementation adapts
aligned borrowed limbs to GMP input through a stack
`mpz_roinit_n` descriptor for the duration of a call. Verify the supported
GMP ABI's `mp_limb_t` compatibility with `uint64_t`, 64 usable bits per limb,
and representability of the signed limb count before creating the descriptor.
Keep the borrowed descriptor read-only, never clear it as an owning `mpz_t`,
and use separate owned storage for GMP outputs. GMP types stay in
implementation units. Copying a view into an independent util owner may
allocate. The Node accessor itself is allocation-free.

Finish all payload writes and state acquisition before publishing or
transferring input owners. A size, allocation, or retain failure releases any
candidate block and balances the candidate's state reference and any retained
child prefix. No entry or `NodeId` is published. Phase 2 consuming failures
leave all input owners intact. A changed rational rebuild borrows the
original parameter while preparing the key and copies it to the candidate
before any transfer. Erase the table entry while the node key is live, then
free the block once. Trivial tail fields have no separate numeric destructor
or limb deallocation. The recoverable guarantees cover checked size and C++
allocation failures. GMP allocation failure during a util operation still
follows its process-termination policy.

## 3. Allocation and C++ lifetime

Use one ordinary `operator new`/matching `operator delete` per `NodeData`
allocation, without a node pool in the first version. This makes individual
releases visible to ordinary ASan allocation instrumentation; it does not
prove every lifetime error is detectable. Future pooling must retain the
semantic and ownership guarantees and provide an appropriate instrumented
path.

```text
child_offset   = AlignUp(sizeof(NodeData) + external_count_bytes,
                         alignof(NodeData*))
payload_offset = AlignUp(child_offset + n * sizeof(NodeData*),
                         PayloadAlignment(tag))
allocation     = payload_offset + PayloadSize(tag)
```

Compute both offsets and the allocation size together. The input span's
count fits the 64-bit field on supported platforms. Check multiplication,
alignment adjustment, and the final allocation size before allocating. The header and payload types
fit the default allocation alignment, checked statically.

1. Obtain the block and placement-create a `std::byte[]` covering it.
2. Construct the header and, when required, its trailing 64-bit child count.
3. For a nonempty child array, use non-allocating placement array new at
   the checked aligned offset to start the array and pointer lifetimes.
   The first version value-initializes the slots. Phase 1 permits
   default-initialized placement construction (`::new (address) NodeData*[n]`)
   with no whole-tail zero fill. Construct any payload at its own checked
   offset.
4. Retain the state. For each successfully retained child, assign its pointer
   once to the next slot and advance the retained-prefix count. On failure,
   rollback reads and releases only that prefix, including each retained
   occurrence of a repeated child. Leave the unwritten suffix unread.
   Destroy any constructed payload and free the candidate storage, and
   release the state reference if it was acquired.
5. Publish the node after all references have been acquired.
6. During destruction, release child references, destroy the payload and
   header, end the applicable storage lifetime, and use matching delete.

Use the byte-array base for offset calculations. Never use zero-length C++
arrays, compiler-specific flexible array members, unconstructed nontrivial
`Integer`s, or reinterpret a `NodeData*` slot array as a `Node` array.
[C++23 lifetime provisions](https://timsong-cpp.github.io/cppwp/n4950/obj.lifetime)

## 4. Unique table and structural hash

The first version uses a dedicated, minimal open-addressed linear-probing table:

```text
slot: NodeData*; nullptr means empty
initial capacity: 16; capacities are powers of two
maximum occupancy: capacity - capacity / 4
grow: double before an insertion would exceed maximum occupancy
erase: backward shift; no tombstones
shrink: at explicit compaction or substantial batch retirement
empty: release enlarged slot storage; allocate on the next insertion
```

The table is weak. It owns no node reference. Full equality checks kind,
the explicit sort when the caller supplies one, child count, ordered child
pointers, payload tag, and all applicable payload fields. The hash uses the
same fields. For a live node the ordered child pointers identify the exact
immutable children it owns. Hashing a borrowed query reads pointer values
without dereferencing them. A collision cannot admit an unvalidated child,
because equality requires each pointer to match the corresponding owned
pointer.

Use one multiply per appended scalar word and avalanche the result before
masking it to the table capacity. Arithmetic wraps modulo 2^64.

```text
mix(x):
    x = (x XOR (x >> 30)) * 0xbf58476d1ce4e5b9
    x = (x XOR (x >> 27)) * 0x94d049bb133111eb
    return x XOR (x >> 31)
append(h, x): return (h XOR x) * 0x9e3779b185ebca87
```

Fold the full explicit sort ID (or zero), child count, kind, and ordered
child pointers as separate words. Include the payload tag before its fields
for nonempty payloads. A plain lookup hashes the no-payload shape and
requires the stored tag to be `kNone`. A tag or field mismatch continues the
probe sequence until an exact match or empty slot.

An integer includes twice its magnitude chunk count plus one if negative.
For zero or one magnitude chunk, use the scalar fold. For multiple chunks,
absorb the chunks into a multiword SipHash state and fold the compressed
result back into the structural hash. This avoids the algebraic cancellation
possible with the scalar fold across two arbitrary chunks. Apply `mix` after
all fields.

Hash the mathematical value using the util view's allocation-free chunks.
The exact hash is internal, not a serialized or cross-version contract.

A query key borrows the already validated children and value views only until
the call returns. A hit retains and returns the existing node without
allocating a candidate node. A miss records the empty slot, performs checked
growth, and creates one independent candidate. Reuse the slot when the
capacity stays the same. A rehash allocates a new empty array, places existing
pointers using cached hashes, then swaps arrays; failure leaves the old table
valid. It does not move nodes. Iterators or references to table slots are not
published and may be invalidated by growth or deletion.

Backward-shift erase uses cached hash and node pointer:

```text
find node from its hash home slot and pointer identity
hole = node slot
for each occupied subsequent slot, wrapping around:
    home = entry.hash & mask
    if distance(home, hole) < distance(home, current):
        move the entry pointer to hole
        hole = current
set the final hole to null
```

Erase and the associated payload destruction do not allocate or throw.
When the last node dies, release any enlarged slot array. The next insertion
can allocate the initial array. `CompactNodeTable` is an explicit maintenance
operation. When occupancy is at most one quarter, it rehashes into half the
capacity repeatedly in its size calculation and allocates only the final
target array. A failed allocation leaves the prior table usable. Both paths
preserve live-node addresses and identities.

`GetStorageStats` inspects the current manager without allocating or retaining
nodes. It counts published nodes, child slots, requested node blocks,
significant GMP storage, and manager metadata arrays and records. Hash
containers expose entry and bucket counts. The table peak includes the
overlap of old and replacement slot buffers. Process RSS additionally
includes allocator overhead, unused pages, the workload and runtime libraries.

### Phase 1C storage accounting

`NodeStorageStats::node_bytes` sums requested sizes of live published node
blocks, including the header, child storage, alignment, and every inline
payload byte. Reconstruct each size from the same layout rules and validated
counts used for the allocation, independently of limits set for later
allocations. `numeric_backing_bytes` continues to count
separately owned numeric backing, such as for a GMP-backed wide builtin
index. Inline limbs, packed rational components, and FP categories add zero
to this field. Hash table and manager metadata have their own existing
counters. Candidate blocks and caller-owned numeric values are outside the
live-node totals. The snapshot remains allocation-free and retains no
objects.

## 5. Checked construction and rollback

`CheckBuiltin` and `MakeBuiltin` share shape validation and result-sort
inference from [operators.md](operators.md). An unindexed intrinsic operator
with no explicit qualifier or extra payload can look up its kind and ordered
child identities directly. Such a hit requires the stored node to have no
payload. The const-array operator always uses the explicitly qualified key.
Other keys validate kind, arity, payload schema and qualifier ownership
before lookup. An exact hit establishes that the same immutable inputs
already passed the operator rule. A miss completes shape validation if
needed, checks operand ownership and sorts, validates operator-specific
bounds, and computes or interns the result sort. `CheckBuiltin` performs
shape validation and inference directly. A source frontend additionally enforces
logic/profile rules; internal well-sorted operations are not restricted to one
source logic. Cache the immutable IDs of lazily interned Bool, Int, and
rounding-mode sorts. A concrete declaration signature is read directly from
the validated instance. Parameterized signatures evaluate their recipe DAG
with one temporary memo per instantiation, shared across domain and result.
It stores only recipe IDs reached from that signature.

```text
MakeBuiltin(op, children):
    choose the plain key or prepare a validated payload key
    if full key is present:
        checked-retain node and return adopting Node
    validate remaining shape and operands, then infer the result sort
    check node ID, allocation size and ref capacities
    ensure unique-table capacity
    allocate candidate and construct header, child array and owned payload
    acquire one state reference
    retain and store each child in order, including duplicate children
    if a retain or any recoverable operation fails:
        undo the retained prefix, release candidate state ownership,
        destroy constructed payload/storage, and return the corresponding error
    assign the next NodeId, set ref_count to one
    publish in the reserved table slot without allocation
    return an adopting Node
```

A resize can invalidate the originally found empty slot: recompute the
insertion position after growth. A failed resize preserves the old table. A
failed node allocation after successful resize may leave extra table capacity
but publishes no node. `MakeApply` includes a validated declaration-instance
ID in its key and checks its concrete signature on a miss. Bit-vector literal
keys include the explicit sort and canonical numeric value. A miss checks
the sort category and width. Value constructors own their payloads before
publication.

`MakeFreshConstant` stages its fresh declaration, empty-parameter declaration
instance, and application together. Reserve IDs, vector and table capacity,
allocate records and node, and retain the state before publishing any of them.
Then publish all three using nonthrowing operations and return the node. On a
recoverable failure publish none of them and consume no published ID. A name
hint is copied; it is never used to intern fresh declarations. This avoids
orphaned, inaccessible declarations on a failed fresh-constant request.
The instance points directly to the staged monomorphic declaration
signature and uses the same dense publication step as any ordinary instance.

### Phase 1 word construction and rebuild

`MakeWordIndexedBuiltin` reads the caller's borrowed word span directly
through the same builtin shape and index rules. Use the canonical index key
and storage selection used for exact `Integer` inputs. In particular, a word
above `UINT32_MAX` can still be a valid exact rotate or divisible index.
The lookup can represent these larger words with an unsigned `IntegerView`
without allocating a temporary `Integer` vector. For a valid
`MakeBvWordValue` request, use the ordinary BV word key with the explicit sort
ID. Share the BV kind and fit checks. Compare the word's bit length to the
width so every word fits at widths of at least 64.

For `RebuildNode`, validate the original, the span length, and each
replacement's manager and concrete sort at its position. Compare child
pointers during the scan. An entirely unchanged sequence, including an empty
one, takes only the checked retain of the original after validation. It
does not hash a key, probe the table, allocate a candidate, or acquire new
child references.

For a changed sequence, form the ordinary lookup key from the original
kind and stored payload with the new child order. Keep the existing key's
explicit-sort convention. In particular, use the original result sort as
the qualifier for `kArrayConst`, and keep the explicit-sort field zero for
intrinsically typed builtins and `kApply`. Carry the original result
`SortId` separately into candidate construction. Borrow wide indices and
rational data from the live original during lookup, and copy them into
owned payload storage on a miss. Reuse the regular lookup and publication
steps with the established result sort and without builtin shape checking,
type inference, or declaration signature rechecking. On recoverable
failure, restore references, destroy constructed payload and storage, and
leave the original and table entries valid. A failed miss consumes no
`NodeId`. The existing table capacity and GMP failure rules apply.

C++ allocation failure is translated to `kResourceLimit/kCppAllocation` by
factories after rollback. Checked state/child retain failures are translated
to `kResourceLimit/kReferenceCount`; owning-handle copies outside a factory
throw `ReferenceLimit` as described in [design.md](design.md#7-errors-and-factory-guarantees).
GMP limb OOM follows GMP's process-termination contract, not this rollback
promise.

### Phase 2 operand move-in

The [consuming factories](design.md#phase-2-consuming-construction) use
the existing checked construction and rebuild rules, reading their ordered
children through a typed range. Use a compile-time accessor for borrowed
`NodeView` and owning `Node` ranges in rank validation, hashing, equality
and candidate preparation. Both accessors obtain the same node identity
and sort. Use the existing key's exact indices, qualifier, rational or
declaration-instance payload. The frontend passes a contiguous slice of
its owning operand buffer while its separately indexed source marks stay
in source order. Raw byte strides and a temporary view array are unnecessary.
`CheckBuiltinOperands` shares the check-only inference routine through the
owning accessor, so optional source-order diagnostic replay can use the
same buffer.

The transfer path targets per-edge retain traffic on construction misses
while preserving prompt reclamation and the owning public handle. Keeping
borrowed construction for shared input views avoids requiring new temporary
owners from callers that already have a live root.
[Node architecture review](/pub/netdisk2/zhangx/dandelion-frontend-bench/qf_bv_compare/review_philosophy_node.md)

1. Probe for the same canonical parent as the borrowed path. Check
   and acquire the result retain on a hit, including the unchanged
   rebuild shortcut. An exact hit and a failed retain leave all input
   handles intact.
2. On a miss, complete shape, rank and ownership checks. Check the ID,
   size and alignment calculations, ensure table capacity, and allocate
   and construct the candidate header, child slots and owned payload.
   Retain the manager state. Recompute the insertion slot after any
   table growth. Copy or otherwise acquire any borrowed payload while
   its source is live. Leave the candidate's initialized-edge count at
   zero during preparation.
3. Once every fallible operation has succeeded, detach the internal
   pointer from each input handle and initialize its corresponding edge
   by nonthrowing operations. Each detach nulls one handle and performs
   neither a retain nor a release. Advance the initialized-edge count for
   each stored slot. Set the parent count to one, assign its ID, publish
   it into the reserved table slot, and return the adopting owner.
   There is no error path after the first detach.

Pre-transfer rollback releases any candidate state ownership and
destroys its constructed payload and storage. It owns no edges to drop.
The borrowed path retains its initialized-edge rollback. Both paths
share ordinary parent destruction, which releases one edge per slot
even for duplicates. The move-in path preserves stable reference counts
without temporarily incrementing any child's count. Reference limits
still apply to state and returned hit ownership. The factory retains
the existing error translation, table-growth and derived-sort guarantees.

## 6. Reference counts and reclamation

For each published node at a stable point:

```text
ref_count = owning Node handles
          + incoming parent slots, counting duplicate slots separately
```

Each published allocation holds one state reference. Copy retains before
publishing the destination. Copy assignment retains new before releasing old;
failure preserves the old target. Move transfers without retaining and clears
the source; self-move is valid. Checked `uint64_t` node and state counts
never wrap. Destruction, erasure, release and payload destruction are
`noexcept`.

The first version immediately and iteratively reclaims a node when its count
reaches zero:

```text
Release(node) noexcept:
    decrement node.ref_count
    if it is nonzero: return
    state = node.state
    work = empty intrusive stack
    retired_state_refs = 0
    erase node while its structural key is intact
    push node using reclaim_next
    while work is not empty:
        current = pop
        for each child slot of current:
            decrement child.ref_count
            if child becomes zero:
                erase child while its structural key is intact
                push child using reclaim_next
        destroy current payload and storage
        increment retired_state_refs
    release retired_state_refs from state in one checked subtraction
    destroy state if its reference count becomes zero
```

The state references of retired nodes are transferred to the drain until it
finishes; the destructor path does not acquire an additional reference that
could overflow and does not allocate a work queue. Removing each weak entry
before destroying its key also prevents lookup from returning freed storage.
Duplicate child slots are decremented individually. Internal underflow or a
missing table entry indicates an invariant violation and fails fast.

`ReleaseRoots` validates all handles before consuming any. It propagates
reference-count decrements across the batch and keeps retiring nodes on an
intrusive list until the weak index is cleaned up. For an empty graph it can
clear the whole index. If many nodes retire and the survivors fit a smaller
table, it rebuilds the table from those survivors. A failed replacement
allocation falls back to erasing retired entries in place. All payloads and
node references from the batch are released before the call returns. If the
old index buffer was detached, its backing allocation is released after
the retired node allocations so the allocator can coalesce their free blocks.

## 7. DAG traversal and caches

```cpp
class DagWalker {
 public:
  static Result<DagWalker> Create(std::span<const Node> roots);
  Result<std::optional<DagEvent>> Next();
  Result<void> SkipChildren();
};
```

The move-only walker owns roots, uses an explicit stack and a private table
of visit states keyed by `NodeId`, and produces `RootEnter/RootExit`,
`NodeEnter/NodeExit`, and `EdgeEnter/EdgeExit` events.
The visit table stores two bits per ID in a word shared by 32 adjacent IDs.
An open-addressed table maps occupied word indices to their state words.
Capacity grows with the number of occupied words, including for sparse IDs.

- An empty root list produces no events. Null or mixed-state roots return a
  checked error.
- Process roots in input order and child slots in stored order. Every root
  occurrence gets root events, including duplicate roots.
- Emit node enter/exit at most once per unique node. Emit an edge enter/exit
  for every expanded parent slot, including duplicate edges and edges whose
  child was already visited. Finish a child's traversal before its edge exit.
- `NodeEnter` is preorder and `NodeExit` is postorder. `SkipChildren()` is
  valid only immediately after a `NodeEnter`; it omits outgoing edge events
  but still emits `NodeExit`. The skipped node is considered visited and is
  not later expanded through another incoming edge during the same walk.
- Event views refer to nodes reachable from the walker's owning roots and
  remain valid while the walker and the corresponding roots remain alive;
  `Next()` or unique-table growth alone does not invalidate them.
- Allocation failure returns a resource error and puts the walker into a
  terminal failed state. Destruction still releases all roots. No traversal
  or reclamation uses a depth-dependent native recursion.

Each explicit DFS frame stores the current node, the next child slot, and
whether its current edge requires an exit event. Mark a node when actually
entered, not merely when scheduled as a sibling. Distinguish in-progress and
finished nodes: an edge to a finished shared child emits edge events without
revisiting it; an edge to an in-progress ancestor would violate the immutable
DAG invariant and fails fast. This gives child-before-parent `NodeExit` order
without confusing a scheduled shared child with a completed child.

`VisitDagPostorder` serves consumers that need each reachable node once after
its children. It validates and retains its roots before visiting. It marks
discoveries in the sparse visit table and uses an explicit stack of child
cursors. The callback sees nodes in root and stored-child order and may
start an independent traversal. The call propagates the first callback
error and releases its root copies on return.

A rewrite memo belongs to one call with a fixed rules/environment and owns
the roots and any produced results it must retain. Definition-substitution
memo belongs to one instantiation with a fixed argument environment. Encoding
caches belong to one query and fixed encoding configuration. Equality and
theory state is search-dependent and must obey the solver's backtracking
contract. A model-evaluation cache belongs to one immutable model snapshot.
A cache key containing only a `NodeId` does not keep a node alive; old IDs do
not alias newly created nodes in the same state but can waste memory.

## 8. Frontend and model boundaries

The frontend resolves names, source scopes, logic/profile restrictions,
operator overloads, symbolic tester identifiers and qualified result sorts.
It resolves the finite expression's sort constraints before passing a resolved
operator or `DeclInstance` to the checked factory. A polymorphic constructor
whose arguments do not determine its result requires `(as symbol Sort)`;
an enclosing equality alone does not disambiguate it. Never select an
arbitrary instance. `as` constrains the result sort, not an implicit
Bool/BV or numeric conversion. `to_fp` forms resolve by arity, source sort and
the concrete-rational compatibility grammar.

- `let`: resolve every RHS in the outer lexical environment, then introduce
  all new bindings simultaneously; duplicate names in a binding group fail.
- `define-fun`: the frontend retains distinct formal identities,
  an owning body and a defined-call identity. A query or model containing the
  call must carry its definition table; the internal declaration is not a
  free UF. A consumer may later perform capture-safe substitution when needed.
  Formal placeholders remain in definition bodies, not free in query roots.
  The Phase 1 Node boundary requires retained declarations and instances for
  definitions with formals. It permits a closed nullary definition to share
  its typed body directly and keep its definition and occurrence information
  in the frontend. Calls with formals within an aliased body still require
  the owning definition table.
- The frontend checks source scope and active signatures without expanding
  defined calls to enforce catalog fragment shapes. A model-only abstract
  value is a fresh internal constant whose meaning is supplied by its owning
  model binding, not an ordinary UF. See the selected [frontend interfaces](../frontend/semantic-interfaces.md).
- Constructor rank matching must avoid revisiting an already matched recipe
  and actual-sort pair within the same binding attempt. The frontend-facing
  group-scoped recipe identity comes from `SortRecipeView::GetId`.
  [Node design](design.md#9-implemented-query-details)
- `match`: require a concrete datatype; type-check every branch, even
  unreachable ones; enforce pattern variable rules and exhaustiveness.
  Repeated constructor patterns are allowed and first-match order applies.
  Lower right-to-left to typed testers, selectors and `ite`; share the
  scrutinee and substitute pattern variables without creating free symbols.
- Source `OccurrenceId` records own the corresponding node and keep source
  range and metadata outside the shared node. Multiple source occurrences can
  refer to the same node.
- `:named` is a closed-term zero-argument definition alias, not an independent
  fresh variable. Process command-local labels in left-to-right postorder,
  allow later references, check source-level closedness before `let` expansion,
  and roll back labels if the command fails. Preserve other accepted
  nonsemantic annotations on occurrences; report excluded `:pattern` use
  explicitly.

Underspecified operations are total functions constrained only as specified.
A model's choices must agree for the same semantic operation and equal
semantic arguments, not just for identical node IDs. In particular, n-ary
`div` follows the binary left fold, and equal semantic NaNs must not acquire
inconsistent interpretations across UF, arrays, or datatype fields. General
arrays need total model interpretations; `ArrayConst` and `store` syntax
alone do not constitute a complete array model.

## 9. Source organization and build integration

The `dandelion_node` static library publishes `dandelion.node` in its
`PUBLIC` C++ module file set. The implementation units cover node handles
and factories, sort and declaration stores, datatype builders, builtin
rules, the unique table, and DAG traversal. They import the primary
interface through `module dandelion.node;` and import the non-exported
`dandelion.node:internal` partition explicitly. A separate `PRIVATE` C++
module file set keeps that partition inside the target.

### Phase 1C module and test boundary

`dandelion_node` links publicly to `dandelion_util`, with an ordinary
`import dandelion.util.numeric;` in the Node module. Consumers which name
numeric types import the util module explicitly. Node exports its own
diagnostics and `ReferenceLimit` and translates util failures at the checked
factory boundary. Util links privately to GMP and supplies the link
dependency for consumers of the static libraries.
[Node error contract](design.md#phase-1c-error-translation),
[Util build and module layout](../util/implementation.md#build-and-module-layout)

In test builds, `node_test_peer.cc` is a private Node implementation unit
that uses `Access` from `:internal`. The Node and util peer declarations
live in test-only headers with global module attachment. Initial manager
and util fault hooks are thread-local. Existing-manager hooks are per
state. Tests use the public modules and the test headers. Product builds
omit the hooks.
[Node tests](../testing/node.md#phase-1c-typed-literals-and-inline-numeric-payloads)

### Build and test requirements

Follow [cmake.md](../cmake.md): list owned sources explicitly, apply
`dandelion_configure_target()`, use existing dependency resolution and
GoogleTest, and discover node cases with `gtest_discover_tests` in `PRE_TEST`
mode and label `node`. A zero-test discovery is a failure. Ensure a sanitized
static node library's final consumer links the sanitizer runtime; private
compile instrumentation alone does not propagate it. GCC ASan/UBSan is the
required local sanitizer configuration; current local Clang sanitizer runtime
availability is documented in the build guide, not assumed.

Before production code, create the complete scoped tests according to
[AGENTS.md](../../AGENTS.md), [node testing](../testing/node.md), and the
common testing strategy. Tests may initially fail to compile; preserve and
report that state. The implementation dependency order is numeric values,
stores and datatype transactions, complete kind rules, unique table and
lifetime, traversal, and frontend integration. This is a dependency order,
not a reduced theory scope.

## 10. Measurement and research evidence

Record node lookup/hit/miss counts, probe distributions, allocation and
reclamation counts, live/peak nodes and bytes, cache retention, traversal
nodes/edges, parser time and end-to-end time when the relevant components
exist. Compare with the same workload, compiler and configuration; distinguish
input construction from rewriting, encoding, solving and model costs. Only
small bounded checks are ordinary development work. Use local datasets only
under the project's strict read-only/metadata policy; do not download them
without explicit authorization.

The source discussion used small prototype checks with GCC 16.2, Clang 23.1.5,
CMake 4.5 and GMP 6.3, including collision deletion and iterative reclamation.
Those probes are not Dandelion's implementation and do not establish this
design's performance or full correctness. The selected table and allocation
parameters are concrete baseline choices, not measured optima.

A separate isolated CMake 4.5/Ninja probe defined one public primary module,
one private implementation partition, two implementation units importing the
partition, a test-peer implementation in the library target, and a consumer.
It built and ran successfully with GCC 16.2.0 and Clang 23.1.5. This checks
the selected module organization on those toolchains, not every detail of the
future node module or its runtime behavior.

References for the design comparison include
[Bitwuzla node table, 0.10.0](https://github.com/bitwuzla/bitwuzla/blob/0.10.0/src/node/node_unique_table.cpp),
[cvc5 node value, fixed commit](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/src/expr/node_value.h),
[Z3 AST, fixed commit](https://github.com/Z3Prover/z3/blob/430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e/src/ast/ast.h),
[Yices term representation, fixed commit](https://github.com/SRI-CSL/yices2/blob/ccdf8998809c82763c683c5e4d9f78eabe6a24e3/src/terms/terms.h),
[STP hashing factory](https://github.com/stp/stp/blob/2.3.4/lib/NodeFactory/HashingNodeFactory.cpp),
and [SMTInterpol's term factory and checker](https://smt-comp.github.io/2023/system-descriptions/SMTInterpol.pdf).
These describe different implementations and do not establish Dandelion's
performance.

### Phase 1C allocation measurements

On the target 64-bit glibc allocator, the 56-byte header-only FP special
request fits a 64-byte chunk. Use requested bytes for `NodeStorageStats`
and measure allocator effects separately in a Release run using that
allocator.

Keep the 48-byte header as a separate A/B benchmark candidate after the
56-byte Phase 1C implementation. Hold payload encodings, workloads,
toolchain, build configuration, and allocator fixed. Compare requested node
bytes, allocation counts and size classes, peak RSS, and construction and
reclamation time on matched word, special, wide, and mixed node workloads.
Record reproducible measurements before adopting the smaller header.

## 11. Concrete implementation notes

The public owning sort/declaration/group names are aliases of one checked
`StateHandle<View>` implementation; their view types remain distinct. This
shares special-member logic without changing their state-plus-ID ownership or
identity contracts. No record stores those owning handles.

The numeric interface additionally supplies exact sign, comparison, bit length,
checked narrowing, magnitude chunks, read-only decimal conversion and deep
copy on its views. GMP types remain private. Rational copy assignment first
copies both components and then moves, preserving normalization after C++
allocation failure. Its allocation-free moves reset the source to 0/1, so a
moved-from value cannot publish an invalid denominator. Both regressions were
observed before their fixes; see acceptance.

`MakeFreshConstant` creates an internal-function declaration. User-declared
constants use a zero-domain `DeclareFunction`. Datatype commit also stages a
missing Bool sort for testers in the same transaction, including its interning
set node and destination bucket capacity; a failed group consumes no sort ID.
Flat recipe substitution and validation use explicit storage, not native
recursive descent. Store-vector capacity grows geometrically after checked
size calculations; this is an implementation choice, not a performance claim.

GCC 16.2 crashed while consuming the module's defaulted Node equality function.
Explicit equality definitions for owning handles, views and range iterators
avoid that compiler path with unchanged identity semantics. Non-template
comparisons are defined out of line; the common owning-handle template compares
its state and ID inline. The raw compiler backtraces are retained in the local
acceptance evidence directory. Both compilers build the resulting
primary-interface/private-partition organization.

`NumericTestPeer` is also test-only. Its numeric C++ allocation countdown is
thread-local because standalone exact values have no manager. Hooks for
existing managers remain per state; the two creation-allocation checkpoints
use a separate thread-local countdown before a state exists. Checkpoints cover
potentially allocating project operations,
including enclosing standard-container operations; a checkpoint may fire even
when existing container capacity would have avoided a real allocation. They
exercise rollback, not GMP allocator replacement or recovery from arbitrary
limb OOM. A separate thread-local limb-capacity override exercises GMP
representation checks with small values. The bounded GMP subprocess test
checks fatal limb failure.

The frontend section remains an integration contract. No parser, substitution
engine, rewriter, model evaluator, SAT adapter or solving procedure is supplied
by this library. Node API boundary tests verify the structures those components
can construct, without claiming their source-level behavior has run.

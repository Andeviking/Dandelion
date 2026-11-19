# Node Implementation Plan

Status: **implemented first-version storage and algorithms**. This document
selects one concrete implementation for the contracts in [design.md](design.md)
and [operators.md](operators.md). It is not a stable binary ABI or a claim of
superior runtime performance. Future implementations can change internal
structures while preserving observable semantics, ownership, and error
guarantees.

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

The node header occupies 56 bytes on the supported 64-bit builds:

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
| Bool, rounding mode, FP special | The corresponding scalar; special FP values carry only their canonical category. |
| Int or wide BV/finite FP value | One owned canonical `Integer`. |
| Word-sized BV or finite FP bits | An unsigned 64-bit word, including the full unsigned range. |
| Application | A `DeclInstanceId`. |
| Indexed operation | One or two 32-bit indices, or an owned `Integer` when a permitted unbounded index exceeds 32 bits. |
| Rational FP conversion | Two 32-bit format parameters and one canonical `Rational`. |

The factory validates sign, arity, sort, and bounds, then selects the unique
representation of the mathematical value. Borrowed index and literal views
expose the same exact values across all layouts. The sort stores the BV width
or FP format. A const-array result sort is the complete array qualifier and
participates in its structural key. Payloads own their exact values
independently of caller objects, and padding does not participate in equality
or hashing.

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
3. For a nonempty child array use non-allocating placement array new with
   value initialization at the checked aligned offset. Construct any payload
   at its own checked offset.
4. Retain the state. Retain each child and write its pointer to the next
   slot in the same pass. On failure release only the retained prefix.
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

Hash based on mathematical value, not GMP limb width, address, or internal
allocation. Obtain chunks without allocating; handle `GMP_NUMB_BITS` and
possible nails rather than assuming the host limb is exactly 64 value bits.
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

C++ allocation failure is translated to `kResourceLimit/kCppAllocation` by
factories after rollback. Checked state/child retain failures are translated
to `kResourceLimit/kReferenceCount`; owning-handle copies outside a factory
throw `ReferenceLimit` as described in [design.md](design.md#7-errors-and-factory-guarantees).
GMP limb OOM follows GMP's process-termination contract, not this rollback
promise.

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
  The production solver will consume this table.
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

The implementation files and logical responsibilities are:

```text
src/node/
  numeric.cppm             numeric.cc
  node.cppm                node.cc
  node_internal.cppm       sort_store.cc
  decl_store.cc            datatype_builder.cc
  kind_rules.cc            unique_table.cc
  traversal.cc
tests/node/
  node_test_peer.cc        numeric_test.cc
  sort_test.cc             declaration_test.cc
  datatype_test.cc         operator_test.cc
  identity_test.cc         lifetime_test.cc
  traversal_test.cc        failure_test.cc
  boundary_test.cc         value_test.cc
  category_test.cc         frontend_boundary_test.cc
  gmp_failure_test.cc      gmp_oom_probe.cc
```

One `dandelion_node` STATIC target owns the two named modules
`dandelion.numeric` and `dandelion.node`. Publish module interfaces through
`PUBLIC FILE_SET CXX_MODULES` and compile implementation sources privately.
The node interface re-exports the numeric module when its public declarations
use numeric types. `node_internal.cppm` provides the non-exported
`dandelion.node:internal` partition in a separate `PRIVATE` CXX_MODULES file
set. It imports the primary interface and defines shared internal types once.
Node implementation units declare `module dandelion.node;` and explicitly
`import :internal;`. The public interface neither imports nor re-exports the
private partition. Never repeatedly include a header defining the same
module-attached internal types in several module implementation units: the
multiple definitions violate the C++ one-definition rule even if a compiler
does not diagnose them. [C++23 ODR](https://timsong-cpp.github.io/cppwp/n4950/basic.def.odr),
[CMake module visibility](https://cmake.org/cmake/help/v4.5/manual/cmake-cxxmodules.7.html#module-visibility)

Use C++23 and `import std` on the verified GCC/Clang
toolchains; include `gmp.h` only in an implementation global module fragment.
The GMP C API is private to the module and `GMP::GMP` is a private dependency.
A final executable linking the static library must still receive the required
GMP link dependency. The node target does not find or link a SAT backend.

When `DANDELION_BUILD_TESTS` is enabled, compile `node_test_peer.cc` as a
private implementation source of `dandelion_node`, where it can import the
private partition. Expose its narrowly scoped controls through a test-only
`NodeTestPeer` declaration in the primary interface, enabled only in that
build. It is not a supported product API. Test executables import the public
node module and call that peer; they do not import a private partition from
another target. Controls for existing managers are per state and are established before
the corresponding operation. Manager creation and standalone numeric values
use narrowly scoped test-only thread-local controls, since no state exists
at those boundaries. Without tests, neither
the peer declarations, friend declarations, nor peer source is built. Under
the same test-build condition in the primary interface, export the peer's
forward declaration before affected handles, add their friend declarations,
and export the peer definition after the handles. Keep test-enabled and
test-disabled module artifacts in their own correctly configured builds.

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

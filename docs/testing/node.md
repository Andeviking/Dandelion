# Node System Testing

Status: **implemented test suite and coverage requirements**. See the
[feature inventory](node-inventory.md) and [acceptance record](node-acceptance.md).
This document applies the [common testing strategy](README.md) and
[AGENTS.md](../../AGENTS.md) test-first requirements to the full
[node design](../node/design.md), [operator specification](../node/operators.md)
and [implementation plan](../node/implementation.md). It specifies required behavior; the acceptance record contains execution
evidence and the explicitly deferred frontend integration checks.

Before implementing a scoped node feature, add the complete applicable
GoogleTest cases below. Tests can initially fail to compile because interfaces
do not exist; report this accurately. Do not disable, delete, or weaken tests
to obtain a pass. A demonstrably incorrect test may be corrected only with
independent evidence under the project test-change rule.

## 1. Coverage inventory and oracles

Maintain a reviewable inventory with one row for **each Kind** in the operator
specification and each sort and declaration category. For each applicable
row record:

1. A valid construction, exact result sort, payload and children.
2. Minimum valid arity and invalid arities, or N/A for a dedicated literal.
3. Each distinct operand-sort failure and manager mismatch, or N/A.
4. Applicable index/qualifier/rational boundaries and extra-payload rejection.
5. Structural identity, changed key fields and forced-collision case.
6. Test IDs, independent expectation, and passing/missing/unsupported status.

Parameterized tests may cover a group, but each Kind must appear as an actual
case and cannot inherit another Kind's passing status. Test the specified
observable semantics; do not make public tests depend on header size, slot
addresses, a GMP limb layout, or incidental pretty printing. The initial table
and reclamation algorithms may have separately labeled internal strategy tests.

Use hand-derived typing and identity expectations from the selected standard
and specification, small independently implemented models or reference
containers where useful, seeded bounded generated cases, and optional
differential/round-trip checks. Agreement with another solver is evidence, not
proof. Node and frontend tests must work without a SAT backend or Dandelion
`check-sat` result. A timeout, unsupported input, unknown result, skipped
dependency, or reference-tool disagreement is not a pass.

## 2. Required coverage matrix

| Group | Required behaviors |
| --- | --- |
| Sorts | All eight `SortKind` categories; exact interning, parameter order, `Bool != BV1`, arrays' index/element order, fresh sort constructors, arity/foreign-state errors, positive width and FP format boundaries. |
| Declarations | Fresh same-name/same-signature identities, zero- and multi-argument functions, formals, internal symbols, concrete domains/ranges, instance interning, no unsupported type arguments, state ownership. |
| Core | Every Core kind and arity; `ite` branch types; chainable equality vs all-pairs distinct; frontend zero/single `and` and `or`; operand ordering. |
| Integer | Every Int kind; unary versus n-ary minus; exact large literals and `divisible` indices; Euclidean `div/mod` boundaries, `**` representation and logic restriction; zero divisor remains a valid term. |
| BV | Every listed BV kind; signed/unsigned operators; result widths, unary/binary/n-ary rules, exact literals, all index constraints, widths 1, 2, 8, 32, 64, 65 and overflow; preserve zero-division operation nodes. |
| FP and RM | Every FP kind and all five RM values; valid formats including F(2,2) and F(2,3); child order, formats, indices, conversions, NaN normalization, infinities, signed zeros, core equality vs `fp.eq`, concrete rational payload. |
| Arrays and UF | `select`, `store`, const-array qualifier and sort errors; nested arrays; UF with Bool, BV, Int, FP, arrays and DT arguments/results; exact function identity and arity. |
| Datatypes | Enumeration, parameterized list, mutually recursive productive group, nonproductive cycles, concrete constructor/selector/tester signatures, array fields, forbidden recursive container, changing type parameters and lazy instantiation. |
| Structural sharing | Identical complete keys share; change each applicable field independently; reversed ordered children differ; equal values constructed from small and GMP paths share; collisions never merge unequal keys. |
| Lifecycle | Owning/borrowed handles, child edges including duplicates, shared diamonds, deep chains, copying, moving, self-assignment, manager facade-before-node, sort/decl/model owners, final state destruction, cross-state errors. |
| Unique table | Initial capacity and threshold, successful and failed growth, wrap-around probe clusters, removal at head/middle/tail, backward-shift lookup correctness, stable live-node addresses, release and reconstruction with a new ID. |
| Traversal | Empty/single/multiple and repeated roots, preorder/postorder, duplicate and shared child edges, skip, wrong skip state, node creation/table growth during traversal, deep chains, allocation failure. |
| Failure safety | Each project-owned allocation checkpoint, payload construction, store reserve/insert, table growth, child-retain prefix, state retain, ID/count/size limits, group transaction and cleanup; no invalid published state. |
| Frontend boundary | Qualified sort resolution, simultaneous `let`, retained definition identity and capture-safe use, exhaustive/duplicate `match`, `:named` closedness and visibility, source occurrence sharing, logic/profile and unsupported-input distinctions. |

The implemented `DatatypeReason` detail is covered by Node tests for an empty
datatype group, an empty constructor set, a recipe using a parameter from
another datatype, a current-group occurrence nested under a field sort
constructor, and a nonproductive recursive group. Each retains the
`kInvalidDatatype` code and receives its specified distinct reason. Other
errors receive `kNone`. If a group has multiple defects, the detail must name
a real defect found in builder validation order. In every failure verify that
no group IDs or records are published and that retry and abort behavior
remains as specified.

Node tests cover the implemented `SortRecipeView::GetId()` query for repeated
views of one shared recipe, distinct records in one group, and IDs scoped to
distinct groups and manager states. Hold the owning group after the builder
is destroyed and verify the identities while the owning group is alive.
Different groups may reuse the same numeric ID, and separately constructed
structurally equal recipes need not share one.

## 3. Concrete cases

### Types, values, and identity

- **N-SORT-01**: construct Bool and BV1 and verify distinct sorts; construct
  `Array(Int, Bool)` twice and check shared identity, then reverse its
  arguments and check it differs.
- **N-SORT-02**: BV width zero and FP exponent or significand width one are
  invalid. A positive mathematically valid width exceeding `uint32_t` is a
  resource limit, not an invalid sort.
- **N-DECL-01**: declare two same-name, same-signature functions and two fresh
  constants; each declaration identity differs. Reconstructing an instance of
  the same declaration and parameters shares the instance.
- **N-BV-01**: on BV8 `x`, extract `(3,0)` and `(7,4)` both return BV4 but
  have different indices and node identities; repeating either construction
  shares. `(8,0)` and `(3,4)` report invalid parameters.
- **N-BV-02**: frontend `(_ bv256 8)` is invalid, not BV8 zero. BV65
  `2^64` is exact; matching `#b` and `(_ bvN 65)` spellings denote the same
  width/value. BV64 with value `2^64` is invalid.
- **N-IDX-01**: `(_ divisible 18446744073709551617)` retains the complete
  positive index. BV8 rotations by that amount and by 1 have different node
  keys; their semantic equivalence is independently justified modulo 8.
- **N-FP-01**: construct one FP sort's NaN value using several sign/fraction
  bit patterns and check shared abstract value identity. Positive and
  negative zero remain different; core `kEqual` and `kFpEq` are different.
- **N-FP-02**: with identical RM and BV input, signed and unsigned
  BV-to-FP kinds differ. Accept supported F(2,2) and F(2,3) formats even if a
  reference tool rejects them.
- **N-VALUE-01**: create a node from an external large `Integer`, then change
  or destroy the external value. The node's value and hash remain unchanged.
  Include `INT64_MIN`, `INT64_MAX`, transitions beyond both, `2^128`, and
  normalized rational zero and denominator sign.
- **N-COLLIDE-01**: force every structure hash to one value before creating
  any nodes in the test manager; unequal sort, child, kind, index, declaration
  and literal keys remain separate and equal keys still share.

### Datatype transactions

- **N-DT-01**: create `List<Int>` and `List<BV8>` and their nil, cons,
  selector and tester instances. Verify concrete signatures, result sorts,
  different instantiations, and repeated-instance sharing.
- **N-DT-02**: commit mutually recursive `A -> B` and `B` with a base
  constructor; the entire group is published and productive.
- **N-DT-03**: reject `A -> B`, `B -> A` with no base. No group member or
  associated declaration is visible after failure. Append a valid base
  constructor and retry successfully.
- **N-DT-04**: reject a recursive field `(Array Int D)` for group member `D`;
  abort the builder. Separately accept a supported parameter-changing
  `D A = leaf(A) | step(D(Array Int A))` and instantiate only requested finite
  recipes without recursively expanding all later instances.
- **N-DT-05**: `head(nil)` has the selector's correct result sort even though
  its semantic value is underspecified. Invalid field sorts, bad group slots,
  wrong parameter arity and foreign sorts fail without partial publication.

### Lifecycle, traversal and failure

- **N-RC-01**: a parent with two child slots both pointing to `x` owns two
  references. Releasing it decrements both and preserves `x` while another
  owner exists.
- **N-RC-02**: construct a 20,000-deep unsimplified `not` chain; traverse and
  release it iteratively without a depth-dependent native call stack.
- **N-RC-03**: destroy the facade while retaining Node, Sort, Decl and an
  applicable model owner in separate cases. Each retained object remains
  valid; after the last owner is released, state destruction occurs once.
  Include arrays and datatype declarations to expose internal ownership cycles.
- **N-RC-04**: test copy/move, `x = x`, self-move, and copy assignment where
  releasing the old owner would otherwise release a child used by the new one.
- **N-RC-05**: retain a reachable child from a walker `NodeView`, release the
  walker and its roots, and verify the new owning node and sort remain valid.
  Reject null and foreign-state views; do not attempt to retain a stale view.
- **N-WALK-01**: use a diamond with a parent containing a repeated child
  slot, and repeat a root. Assert root events per occurrence, one enter/exit
  per unique node, and one edge enter/exit per expanded slot in order.
- **N-WALK-02**: skip a node immediately after enter and check no outgoing
  edge events but a node exit; a later edge to the same node does not expand
  it again. Calling skip in any other state reports `kInvalidTraversalState`.
- **N-WALK-03**: let root children be `A, B` and let `A` have child `B`.
  The specified child order visits `A` first; assert that `B` exits before
  `A`. A merely scheduled shared child is not complete.
- **N-TABLE-01**: create colliding wrap-around clusters, erase first, middle,
  and last nodes in varied orders, and compare surviving lookup with an
  independent reference map. Force a resize and verify old live node identity
  and address stability.
- **N-ID-01**: through a private test peer, approach the last legal NodeId.
  The last ID succeeds, later insertion misses report resource exhaustion,
  and an existing-key hit still succeeds.
- **N-FAIL-01**: force failure while retaining the second repeated child edge.
  The first retained edge is rolled back and all preexisting handles remain
  valid.
- **N-FAIL-02**: fail each project-controlled allocation checkpoint in turn,
  including a unique-table growth, node/payload creation, store insertion and
  datatype preparation. Old objects remain usable and there is no leaked
  published node or partially committed group.
- **N-FAIL-03**: fail every allocation/retain boundary while creating a fresh
  constant. Its declaration, declaration instance, and application are either
  all committed or none are visible; no published ID is consumed on failure.

Use a private `NodeTestPeer` and allocator/checkpoint hooks for collision,
capacity and reference-count boundaries. They are not a public configuration
framework. The test-enabled node target compiles the peer implementation and
exports its test-only declaration as specified in the
[build plan](../node/implementation.md#9-source-organization-and-build-integration);
the separate test target does not import the node target's private module
partition. Collision mode must be established before the first cached hash in
that test manager. GMP limb OOM is not simulated as recoverable C++ OOM.

### Node hot-path optimization checks

The identity, lifecycle, operator, and failure cases above remain the oracle
for the optimized paths. Exercise a wrapped collision cluster that inserts
with and without growth, including a failed candidate followed by retry.
Count post-miss slot searches with the private test peer. A miss can reuse
its vacant slot until growth moves the table. Compare identity and reclamation
with the model in the existing table tests.

Count node storage for two applications sharing a leaf, including their
ordered child slots. Check that releasing one root reduces node bytes without
changing the declarations. Release the last root and require zero live node
bytes. A null manager reports a null-object error. Check the node-table peak
at a known growth boundary independently of allocator and process RSS.

Instantiate two ordinary declarations with the same signature. Each must
retain its own instance identity without using the parameterized instance
index. Check the borrowed signature after growing the stores, and keep the
parameterized nil/cons and failure-injection cases as oracles for the other
instantiation path.

Grow a table and release enough roots to make it sparse. Request compaction
with injected allocation failure, then retry and check all surviving handles
and lookup identities. After the last release, check that the manager can be
reused and that large slot storage is released without an allocation.

Exercise neighboring arities around the inline child-count boundary with a
repeated Boolean child and a different last child. Check length, last-child
access, structural sharing, and reclamation. Check the requested block size
across the boundary to include the out-of-line count and its alignment.

Check exact small and wide parameter storage in the same manager: a rotation
of up to 32 bits, a rotation just beyond 32 bits, a GMP-sized divisibility
parameter, a two-index extract, and a normalized rational FP conversion.
Keep distinct parameter values distinct under forced structural collisions.
Range iteration and individual indexing must return the exact source
parameters. Verify signed-word and unsigned-word boundaries for BV literals,
including a width above 64 with a small value and a width-65 value requiring
more than one word. A literal copied as an Integer must preserve its value.

Interleave two walks of a shared graph with sparse, high IDs and enough
nodes to grow a compact visit table. The event sequences must match a
hand-derived chain and repeated root sequence. Existing traversal checks
cover order, skipping, terminal failures, and ownership after manager growth.
Check two distinct visited IDs on opposite sides of a packed-word boundary
and a root at the largest legal ID. Each ID has its own completion state.

Instantiate a constructor whose fields share a nested parameterized recipe.
Check the result with array sorts built through the public API. The number
of allocation checkpoints should stay below the number of fields when the
signature reuses the recipe. Include a second constructor with a deep array
recipe outside the requested signature. With the requested domain and range
sorts already interned, verify that instantiation interns no additional sorts
and stays within the same allocation bound. The datatype transaction and
resource tests check that an interrupted instantiation publishes no partial
instance.

Compare separately built Release executables on matched workload files.
Use bounded corpus replay for hit, miss, wide and deep graphs, and mixed
replay. Record probe statistics in a separate test-enabled executable so
instrumentation remains outside the Release timings.

### Input keys, metadata storage, and DAG consumers

- With structural collisions forced, reconstruct a typed operator and an
  application using the same live operands. The identity, sort, and type
  evaluation count must show that the hit reused the published conclusion.
  A different index or explicit array qualifier distinguishes the key.
  Rejected null or foreign operands, malformed payloads, and invalid sorts
  publish no node even when all other fields could match. The full operator
  matrix supplies exact checks for FP normalization, signed and wide
  parameters, resource failures, and result sorts.
- Keep a borrowed declaration name, concrete domain, and parameterized
  instance alive across declaration-store growth and failed commits.
  Check empty and long names, embedded zero bytes, empty and long domains,
  and both ordinary and datatype records. Check allocation and retain
  failures before publication, including retry with no consumed ID or leaked
  payload bytes. If a direct nullary slot is evaluated, test a shared hit,
  reclamation and reconstruction, distinct instances of a parameterized
  datatype, and a mixed application that keeps the symbol alive.
- Verify a postorder consumer visits each reachable identity once, with
  children before parents and deterministic root and operand order. Use
  shared and repeated edges, duplicate roots, sparse IDs, a deep chain,
  and an empty graph. Check that foreign or null roots are rejected before
  visiting. Verify ownership across callbacks, reentrant walks, allocation
  failures, and early failure from a visitor. Compare graph summaries with
  the existing event traversal on recorded graphs.
- Compare batch root release against sequential release on independent
  graphs. Include all-dead, mostly-dead, and mostly-live cases, owning aliases
  and duplicate edges, wrapped collision clusters, and retained metadata.
  A foreign root rejects the entire batch. Test a failed attempt to allocate
  a replacement index after retirement and require equivalent cleanup via
  in-place erasure, including final state destruction and successful reinterning.

### Construction cost and sparse declaration domains

- Force collisions among payload-free, indexed, and qualified builtin keys.
  An exact payload-free hit must bypass shape and type validation. A miss and
  an explicit check use the normal rules. Missing qualifiers or indices, extra
  payload, wrong arity, invalid kind, and foreign or null operands must never
  resolve to a published node through the fast lookup path.
- For a single-pass candidate constructor, inject allocation and retain
  failures around an owned wide numeric payload and its operand. Verify the
  payload and edges on success, the operand's ownership after each failure
  and release, and preservation of node identity and table entries on failure.
  Use the existing repeated-edge and external-child-count cases to cover the
  initialized prefix and both header layouts.
- Verify that empty declaration domains need no per-declaration span record.
  Across growth and injected allocation failures, an existing nonempty
  domain must stay valid and a retry must publish the original next ID.
  The existing tests cover long domains, embedded zeroes in names, fresh
  constants, and substituted datatype signatures.

### Frontend component cases

**NODE-DT-RECIPE01** checks that repeat views of a shared field recipe have
the same ID in their group and that distinct constituent records have
different IDs there. Check a structurally equal recipe in a second group
and another manager, whose IDs are local. **FE-DT-ERROR-01** checks the
detail field on the established invalid-datatype code for empty groups,
missing constructors, wrong parameter scope, nested recursion and lack of
productivity. Check unrelated errors retain the default detail and that
repair after a failed commit is possible.

The following are structural checks, not requests to solve the assertions.

```smt2
(let ((x #x01))
  (let ((x #x02) (y x))
    (= x y)))
```

The equality compares inner `#x02` with outer `#x01`. A parallel RHS referring
to an otherwise unbound name fails; no sequential binding is invented.

```smt2
(define-fun twice ((x Int)) Int (+ x x))
(assert (= (twice 3) 6))
```

The proposed frontend retains a defined-call identity and argument, with the
formal and body owned in its definition environment. When a consumer later
instantiates the body, both addition operands refer to the actual argument;
no formal is free in the query root and the call is not a free UF. Capture,
shadowing and definition-environment lifetime have separate tests. This is a
proposed frontend integration case, not an existing NodeManager capability.

```smt2
(declare-datatype Color ((red) (blue)))
(declare-const c Color)
(assert (= (match c ((red 1) (red 2) (blue 3)))
           (ite ((_ is red) c) 1 3)))
```

The first red pattern wins, all branches are typed, and the cases are
exhaustive. Inspect the lowered DAG rather than requiring a solver result.

```smt2
(assert (= (! 1 :named one) one))
```

The later `one` denotes the same Int term. A `:named` subterm containing a
source-level outer `let` variable is not made closed merely because the
variable later substitutes to a constant. A failed command leaves no partial
label binding.

## 4. Build and acceptance

- Test all project behavior with GoogleTest and discover it with CTest under
  label `node`; run with `--no-tests=error`. Checks must remain effective with
  `NDEBUG`.
- GCC Debug ASan/UBSan, Clang Debug, and GCC Release/RelWithDebInfo must run
  the applicable implemented cases. The current missing local Clang sanitizer
  runtime is not silently treated as a pass.
- Run project format, Clang compilation-database tidy, and existing applicable
  tests when the implementation exists. The node/frontend configuration does
  not require a SAT backend.
- Test construction failure and destruction under sanitizers, while recognizing
  that no finite suite or sanitizer proves correctness. Test GMP termination
  behavior separately from recoverable C++ allocation injection.
- For each required inventory row record passing evidence; skipped, unsupported,
  timed out, failed and missing tests remain explicit.
- Parser integration uses typed-structure, binding, source occurrence and
  round-trip checks without claiming a fabricated solving result.
- Optional corpus checks are small, explicit and follow the common dataset
  policy. Never read `/pub/netdisk1/zhangx` unless the filesystem can guarantee
  metadata is unchanged; never download a dataset without explicit permission.


## 5. First implementation test plan and test-first record

The concrete [feature inventory](node-inventory.md) enumerates all 108 kinds,
eight sort categories and six declaration categories. The initial production
repository had no `src/node` implementation. On 2026-11-16, the complete scoped
test sources were added first. A GCC Debug build of `dandelion_node_tests`
failed at `import dandelion.node` with `unknown compiled module interface: no
such module`; the raw configure/build output is retained locally in
`build/node-evidence/test-first-*.log`. This is an expected unavailable-interface
failure, not passing implementation evidence.

The test peer uses per-state, zero-based one-shot allocation and retain
countdowns, established before each operation, and explicit limit overrides.
Allocation checkpoints precede project-controlled allocating operations;
standard container internals are checked through their enclosing operation.
They never intercept GMP's process-wide limb allocator. Each failure sweep
advances its countdown until the operation succeeds, verifies that at least one
failure occurred, and checks the applicable publication/reference invariants.
The node and state reference limit can be lowered below current counts for
boundary testing; releases still subtract real references and never clamp.
Collision mode is established before any node hashes exist. A global
state-destruction counter is test-only diagnostic evidence.

Traversal events use a null parent and `UINT64_MAX` index for node events;
root events carry the root occurrence index and null parent. Edge events name
the child, parent, and child-slot index. This specifies the previously omitted
concrete fields without changing traversal order or ownership contracts.

Before production implementation, temporary expressions in the new tests
were corrected to use a const-reference `View` helper within their full
expression: design.md explicitly deletes `Borrow()` on rvalues. Temporary
owners remain alive through those expressions; no such borrowed result is
saved. The helper is only test code. No expected result was weakened.

The follow-up numeric allocation tests require per-thread test-only numeric
checkpoints, since standalone integers do not belong to a manager. They cover
C++ object/string allocation only. In particular, a failed Rational copy
assignment must not combine a newly copied numerator with the old denominator:
that can violate the always-normalized-value invariant independently of GMP.
Copy-and-move assignment is the selected strong-guarantee implementation.
No GMP limb failure is represented as a C++ exception.

A bounded POSIX subprocess test verifies GMP's default fatal allocation
behavior separately: an uninstrumented helper lowers its address-space limit
to 64 MiB and requests 128 MiB of GMP limb storage. GoogleTest requires
SIGABRT, not a returned resource error or any arbitrary nonzero exit. The
helper disables core files. It neither changes GMP allocators nor allocates
the requested storage successfully; the node library and node tests retain
their configured sanitizer instrumentation. The helper is uninstrumented
because ASan reserves a large virtual address space before main.

Manager creation has two additional test-only, per-thread checkpoints before
state allocation and the initial table-slot allocation. There is no manager to
own a per-state control at those boundaries. Both errors must be translated to
CppAllocation, expose no facade, and unwind all partially constructed members.
These controls do not replace the process allocator and are absent from product
interfaces. A separate test checks that no fully constructed state is destroyed
on either injected pre-construction failure.

Moved-from numeric values remain valid values. Integer resets to zero;
Rational resets to normalized 0/1 after move construction or assignment, with
self-move preserving the value. The regression checks the source after both
moves and uses it as a rational-to-FP node parameter. A default memberwise move
would leave 0/0, allowing an invalid payload through the otherwise checked API.
Resetting the denominator to the small Integer(1) requires no allocation.


## 6. Node rectification regressions

| Area | Independent expectations |
| --- | --- |
| Representation and diagnostics | Static type checks cover the wide identities, counts, arities, and indices. A sort constructor and staged datatype header accept a parameter count beyond 32 bits without allocating its parameters. A bad operand and a bad indexed parameter report their positions, with `UINT64_MAX` in the unused field. |
| Key isolation | Keep both candidates alive, alter one key field at a time, and re-query both. Include either extract endpoint, indices separated by one above 64 bits, either rational component, GMP BV and FP values with the same sort, and all five rounding modes. Generate wide integers that cancel under the original multiply-XOR recurrence and bound probes with natural hashing. |
| Typing | Use declared fixture domains, type arguments and ranges for all six declaration categories. Check distinct datatype selector fields and tester links. Check a late variadic operand of the same category with an incompatible width or format, preserve a nested same-kind operand and edges in exact order, and infer a fresh result sort without allocating a node. Exercise a valid FP format with mismatched packed BV width, a null const-array qualifier, and a wrong-width element in store. |
| Numeric boundaries | Force reduced GMP limb capacity around decimal and radix imports, decimal powers, shifts with a spare limb, and rational cross-products. Keep source values intact. Compare 129-bit values with equal upper chunks and query a past-end chunk. Derive rounding for early underflow at ±1/32 in FP(2,3), ties at 9 in FP(8,3), and the exact fraction at 1 + 1/2 + 2^-65 in FP(65,66). Check wide sign, exponent and fraction boundaries and backing-byte accounting. |
| Failure atomicity | Sweep numeric C++ allocation checkpoints in every GMP-backed Node payload form. Fail a late retain beyond the inline child count and compare all counts and IDs before retry. Exhaust constructor, declaration, and implicit Bool sort identities independently during datatype commit while retaining a prior instantiated signature. |
| Datatype productivity | A constructor requiring both B and C must wait for both. Use A → (B,C), B → base, C → A, then repair C with a base. A reverse-order chain becomes productive from its final base. |
| Table and ownership | Delete a record from a capacity-16 cluster with homes 15, 15, 1, 15. Re-query survivors immediately after failed compaction and after growth. Compare batch and sequential release when few or no nodes survive, including replacement allocation failure. Exercise each metadata handle and ManagerRef as sole state owner, overflow through Attach and Child, and staged recipes after builder moves. |
| Traversal | In a sparse graph with repeated roots, sweep visit-table allocation failures and compare the entire delivered prefix to a successful traversal. Check that errors are terminal and roots remain held until walker destruction. A postorder callback can retain a child and return an unchanged full NodeError. |

With 64-bit GMP limbs, exercise the decimal import boundary at 19 and 20
significant digits under a two-limb cap, for integer and fraction parsing.
Check the last accepted length and the first rejected length at `INT_MAX`
limbs using the import sizing calculation alone. Under a zero-limb cap, a
radix word of value `2^63` fails with `kResourceLimit/kSize` and `2^63 - 1`
still fits inline. With one limb available, the unsigned word is exact.
Binary and hexadecimal spellings of `2^127` require a three-limb import.

For FP(8,2), `(2^64 - 1)/(2^64 + 1)` rounds to 1 toward nearest and to
3/4 toward zero. The nearest comparison requires a spare limb for the
denominator-minus-remainder subtraction. A two-limb cap permits the
toward-zero result and must report `kResourceLimit/kSize` for nearest. Check
the extra limb required when incrementing the two-limb quotient of `7/3`
in FP(8,65) and when biasing the two-limb exponent of 1 in FP(66,2). For
directed overflow to the largest finite value in FP(2,130), test the limb
boundary when constructing the fraction field.

# Node System Testing

The first-version, Phase 1 and Phase 2 Node suites are implemented and have
recorded verified runs. The cases below retain their test design and
acceptance requirements. The review did not independently verify the current
tree or completeness of every subcase. [Phase 1C](#phase-1c-typed-literals-and-inline-numeric-payloads)
covers the numeric boundary and payloads. See the first-version
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
- **N-BV-02**: the frontend reduces `(_ bvN w)` modulo `2^w`, so
  `(_ bv256 8)` and `#x00` produce the same BV8 zero. Exact Node value
  construction requires `0 <= v < 2^w`. A typed BV65 value of `2^64`
  remains exact and matches equivalent `#b` and `(_ bvN 65)` spellings.
  Preparing that value with `BitVector::FromIntegerExact` at width 64
  fails. Passing the BV65 view to `MakeBvValue` with a BV64 sort reports
  `kSortMismatch`.
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

### Multi-range batch root release

The Phase A1 test design covers `RootRange` and the static
`NodeManager::ReleaseRoots(const ManagerRef&, std::span<const RootRange>)`
overload using the typed, private `RootRange` contract.

- **N-RANGE-01** checks that `RootRange::From` is `noexcept` and projects
  exactly the selected `Node` member at each index. Use two records of each
  type, `EntryA` with `term` and `EntryB` with `tag` followed by `body`.
  Verify release clears exactly the selected members and leaves other fields
  intact, including for `span<Node>`. Check that arbitrary callbacks cannot
  be supplied and that the static release overload is `noexcept`.
- **N-RANGE-02** checks validation before any handle is consumed. Require
  `kNullObject` for a default `ManagerRef`, with both an empty batch and a
  live root. Require `kForeignManager` when a
  foreign handle follows a local handle in the second range, even if the
  foreign node has the same numeric ID as a local node. Check that the
  supplied IDs and reference counts are unchanged after rejection.
- **N-RANGE-03** covers an empty batch, a range from an empty node span,
  a range from an empty record span, and null node handles. A live
  node outside those ranges keeps its identity, and reclamation and
  allocation counts stay unchanged.
- **N-RANGE-04** distributes owning handles to one parent across both
  record types. That parent has two edges to a leaf, and a second parent
  has one edge to the same leaf. Include a null handle in each range, the
  leaf's only direct owner, and a copy of the second parent's external
  owner. Place an empty range between the two populated ranges.
  All submitted handles clear and scalar tags are preserved. Only the
  first parent retires. The remaining parent keeps the leaf alive through
  its single edge, and interning the retired parent yields a new ID.
- **N-RANGE-05** splits all owners of a grown table across two ranges.
  The first range alone would leave a small set of survivors eligible for
  a rebuild. With only a `ManagerRef` and the node owners held at the call,
  require all handles to clear, one reclamation per node, zero table
  capacity, and no allocation attempt. The last two checks distinguish
  full detachment from a rebuild between ranges. Recreate a retired key
  after reattaching a facade and require a new ID.
- **N-RANGE-06** forces a wrapped collision cluster of 256 distinct integer
  nodes. Keep four spaced owners external and put the other 252 in two
  ranges. Expect one replacement allocation and a smaller table. Repeat
  with `NodeTestPeer::FailAllocationAfter(manager, 0)` to inject
  `std::bad_alloc` at the replacement attempt. Expect success and the old
  capacity after in-place erasure. In both runs every submitted handle
  clears, 252 nodes retire, all four survivors keep their references and
  identity, and retired keys from both ranges can be interned with new IDs.

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

### Phase 1 construction and rollback

The following GoogleTest cases specify the Phase 1 construction paths. Use public factories and typed queries for the contract
checks. Use private test-peer instrumentation for hash, probe, inference,
cache, and failure checks. Keep all nodes involved in identity comparisons
live, and run collision cases in a fresh manager with collisions forced
before constructing any node.

- **N-REBUILD-01** — Rebuild `bvsub(x,y)` with its original ordered children.
  Use an attached facade in one call to cover shared manager state.
  Repeat with a BV literal and a nullary `kApply` using empty spans. Assert
  the same live identity, one additional owner, and unchanged child and
  state reference counts. Hash computations, table probes, allocation
  attempts, builtin shape checks, type evaluations, and the next `NodeId`
  must stay unchanged. Fail the result retain and expect
  `kResourceLimit/kReferenceCount` with the original and counts intact.
- **N-REBUILD-02** — Exercise both a changed-child miss and a repeat hit in
  each row below. Check the stated result type and payload, exact replacement
  sequence, and distinct identity from the original. Compare the live result
  with ordinary checked construction of the same term. Also prebuild an
  ordinary target before rebuilding in a separate case, so reuse works in
  either direction. Run the key comparisons under natural hashes and forced
  collisions. Assert that rebuild adds no builtin shape or type evaluations
  and leaves the input untouched. Drop the original after success and confirm
  the rebuilt node still exposes its exact payload and owns its new children.

  | Original and change | Expected result |
  | --- | --- |
  | `bvsub(x,y)` for distinct BV8 nodes, replace with `[y,x]` | `kBvSub` of BV8 with ordered children `[y,x]`. |
  | `((_ extract 5 2) x)` on BV8, replace `x` with `y` | `kBvExtract` of BV4 with indices `[5,2]`. Keep `extract 6 3` over `y` live to distinguish equal result widths and different index keys. |
  | `((_ rotate_left 18446744073709551617) x)` on BV8, replace `x` with `y` | BV8 with the exact index `2^64+1`. A rotate by 1 with the same child has a distinct key. |
  | Const array qualified by `Array Int BV8`, replace its element `x` with `y` | `kArrayConst` with the original qualified array sort. A const array qualified by `Array Bool BV8` over `y` has a distinct key. |
  | `to_fp` from exact rational `2/3` with format indices `[8,24]`, replace RNE with RTZ | `kFpFromRational` of FP(8,24), same format indices, exact rational `2/3`, and RTZ as its sole child. Keep the corresponding `3/4` conversion live as a distinct key. |
  | Apply `cons` instantiated at `List<BV8>` to `[x,nil]`, replace with `[y,nil]` | `kApply` with the same concrete `DeclInstanceId`, type arguments, and `List<BV8>` result. A different declaration of the same concrete signature over `[y,nil]` has a distinct key. |

- **N-REBUILD-03** — Give a binary BV8 original too few and too many
  replacements, expecting `kInvalidArity`. Replace the second child with BV4,
  expecting `kSortMismatch` at operand 1. Try a null original, a null
  replacement, a foreign original, and a foreign replacement of the same
  apparent width. Expect `kNullObject` or `kForeignManager` as appropriate,
  with the operand position for replacement errors. Include a nonempty
  span for a nullary original. No failure publishes a node, changes a
  reference count, or consumes an ID.
- **N-REBUILD-04** — On a changed-child miss for a rational FP conversion
  with exact payload `18446744073709551617/3`, inject each project-controlled
  allocation and retain failure through candidate publication. Verify
  the corresponding `kResourceLimit` detail, the original payload and
  references, and unchanged live counts and next ID after every failure.
  A clean retry must publish at that ID with the original payload.
- **N-WORD-INDEX-01** — Construct extracts of a BV8 child with `[7,4]` and
  `[6,3]`, rotations with indices 0, 1, `UINT32_MAX`, `UINT32_MAX+1`, and
  `UINT64_MAX`, and `divisible` with `UINT64_MAX` over an Int child.
  Use fixed caller word arrays and exact `Integer` values independently.
  Check result types (BV4, BV8, and Bool respectively), observable indices,
  identity across APIs in both insertion orders, and distinct live keys for
  unequal indices under forced collisions. With the same rotation child,
  keep a general-factory rotation by `2^64+1` live and distinct from the
  word rotation by 1. An exact hit requires no candidate allocation.
- **N-WORD-INDEX-02** — Compare both factories with a one-index extract,
  an inverted extract interval, an endpoint outside BV8, a zero repeat,
  a missing const-array qualifier, and a missing rational for rational FP
  conversion. Each returns `kInvalidParameter`. An extension of BV8 by
  `UINT64_MAX` returns `kResourceLimit/kSize`. Cover `kInvalidKind` with
  `kInvalid`, `kInvalidArity` with two extract operands, `kSortMismatch`
  with a Bool extract operand, and `kNullObject` or `kForeignManager` with
  the corresponding extract child. `kBvNot` with an empty index span
  follows ordinary builtin rules. A nonempty index span for that kind
  returns `kInvalidParameter`. Keep colliding valid keys live and check
  that rejected inputs publish no node and consume no ID.
- **N-BV-WORD-01** — At width 1, accept 0 and 1 and reject 2. At width 8,
  accept 255 and reject 256. At width 63, accept `2^63-1` and reject
  `2^63`. At widths 64 and 65, accept `UINT64_MAX`. Verify exact
  magnitudes and equality with the corresponding `MakeBvValue` node
  in both insertion orders, including exact decimal integers above
  `INT64_MAX`. Equal words at different widths and distinct words at one
  width remain distinct under forced collisions. Reject overflow with
  `kInvalidLiteral`, Bool sorts with `kSortMismatch`, and null and foreign
  sorts with the corresponding view errors. Rejected inputs leave live
  nodes and the next ID unchanged.
- **N-BV-WIDTH-01** — In separate fresh managers, infer BV8 through an
  extract from BV16 before requesting BV8 explicitly, and request BV8
  explicitly before inferring it. Use different extract keys that yield
  BV8 to force inference when measuring the warm path. Verify that the word
  and exact-width overloads and an attached facade share the same
  `SortId`. Intern a different sequence of sorts in a separate state
  and verify its BV8 view belongs to that state. With the private peer,
  check that warm cached requests skip generic sort-table lookup.
  Check width 1 remains distinct from Bool,
  width 64 is cached, and width 65 and `UINT32_MAX` use the ordinary
  interner and match the exact-width overload. Zero yields
  `kInvalidParameter`. `UINT32_MAX+1`, `UINT64_MAX`, and a concat of a
  BV(`UINT32_MAX`) symbolic term with BV1 yield `kResourceLimit/kSize`
  before narrowing. Failure publishes no invalid sort or node.
- **N-BV-WIDTH-02** — Fail first-use sort allocation for a cacheable width,
  verify the slot stays empty and the next sort identity is available, and
  retry through the other lookup path to populate it. Fail the public
  owning-sort retain with a warm slot, expecting
  `kResourceLimit/kReferenceCount`. After clearing the failure, both
  explicit and derived lookups still return the same published sort.
- **N-TAIL-01** — For default-initialized child storage, fail the state
  retain and each child retain of a candidate with ordered slots
  `[a,b,a]`. Reuse the external-count fixture in the lifecycle regressions
  to fail just before the last slot of a 65,536-child candidate. Check
  rollback against the exact retained prefix, including repeated edges:
  after every failure, state and child counts, live nodes and edges, and
  next ID match their pre-call values. Verify survivor identity and that a
  successful retry owns every edge exactly once. Release the retried parent
  and check that child counts return to baseline. Run with the applicable
  sanitizer configurations and review the rollback loop for reads of
  unwritten suffix slots.

### Phase 2 consuming construction

Add these GoogleTest cases before implementing the
[consuming factories](../node/design.md#phase-2-consuming-construction).
Use hand-checked rank and payload expectations and the existing
`NodeTestPeer` fault hooks and reference-count observations. Construct
known canonical counterparts with the borrowed APIs. Use fresh states or
distinct keys to establish a miss, and keep the expected parent live to
establish a hit. Compare child addresses or IDs only while the relevant
owner is live. Keep setup and fixture owners outside injected operations.

| Case | Fixture and independent expectation |
| --- | --- |
| N-CONSUME-MISS | Build an ordered binary BV parent with two solely owned, different operands. Observe the same child IDs and sorts at the published slots, null input handles, and stable child reference counts with one edge per child. Retain a child independently, release the parent and verify only that child survives. Also release the manager facade first and verify the parent keeps its DAG and ranks alive. After all handles are released, verify prompt reclamation and reinterning with a fresh ID. |
| N-CONSUME-HIT | Keep a borrowed-built parent live, give a consuming factory fresh owning copies of the same children, and obtain a second parent owner with the same identity. Check both input handles stay intact, their counts stay unchanged and the parent gains one reference. Repeat with consuming-first and subsequent borrowed lookup, an unchanged rebuild, an empty child span for a nullary application and a zero-child rebuild. Dropping intact input owners leaves the retained parent usable. |
| N-CONSUME-DUPLICATE | Use two input slots owning the same `a`, with an optional third independent owner. On a miss of a binary parent, both slots become null, both ordered child views name `a`, and its reference count still includes two edges. Destroy the parent and observe two drops, or complete reclamation with the independent owner absent. Repeat on a hit and on pre-transfer failure to check there are two still-owning slots with unchanged counts. |
| N-CONSUME-PAYLOAD | Cover plain builtin, exact wide index, word index above `UINT32_MAX`, qualified constant array, FP rational, and ordinary UF and concrete datatype instance applications. Compare result rank, child order and key with independently built borrowed results. Cover changed rebuilds of each payload-bearing kind, including when the original supplies borrowed payload views. Use a noncommutative parent and an alternate payload to verify order and payload remain identity-bearing. Force a table hash collision and show an unequal key is a miss with distinct identity. |
| N-CONSUME-VALIDATE | For each consuming family, pass null and foreign handles in a later slot, wrong operand count or sort, and the applicable wrong kind, index, qualifier, rational or declaration-instance payload. For rebuild test null/foreign original and a different sort at a given position. Use the documented borrowed-factory codes and indices and compare all owning slots, reference counts and live entries after each error. Replace the rejected input and retry successfully. Compare `CheckBuiltinOperands` with `CheckBuiltin` on valid and invalid ranks, including mismatched widths at a specific written position. Check inferred sort or error, intact slots and no new parent. |
| N-CONSUME-ALLOC | With a fresh key, inject each applicable project allocation failure from key/derived sort preparation and unique-table growth through candidate storage and payload preparation. Use a payload-bearing parent and duplicate operands, and a control with no table growth. Every recoverable failure keeps each slot, its count, original/collision neighbors and live table entries. Compare the next published parent ID with an independently counted successful sequence and require no ID consumed by the failed factory. Capacity and derived-sort changes are allowed by the factory contract. Clear the hook and retry with the same operands. |
| N-CONSUME-RETAIN | Inject a failed result retain for a live hit and unchanged rebuild, and a failed manager-state retain after preparing a miss. Expect `kResourceLimit/kReferenceCount`, all input owners intact, no publication, and balanced state/payload cleanup. Use the retain hook allowing just the state retain to prove that a consuming miss succeeds with no extra child retains, including repeated edges. The matched borrowed miss fails at the subsequent child retain and rolls back. Also hold enough owners of one child to fill its test reference limit while keeping state retains below that limit. A consuming miss at the limit transfers the supplied owners without increasing the child's count. Confirm both implementations keep independently owned children alive. |
| N-CONSUME-BOUNDS | Force the existing Node ID or checked-size boundary on a miss using a small fixture and the established test peer. Require its existing resource kind before ownership transfer. Check that a live exact hit still uses only the result retain when new IDs are exhausted. Test a payload view borrowed from an original also present as an owned replacement in an eligible rebuild and verify the published child preserves that owner's lifetime. |

For successful move-in, count each occupied input slot before and each
corresponding parent edge afterward. The test oracle is ownership
conservation and canonical structural identity. Count recoverable
retains and allocations at their existing hooks. Exercise the complete
sweep through one successful construction, and retain borrowed-path
rollback coverage. Use the same [build and acceptance](#4-build-and-acceptance)
conditions for the new cases.

### Phase 1C typed literals and inline numeric payloads

These cases test the
[typed API and error boundary](../node/design.md#10-phase-1c-numeric-boundary-and-typed-literals)
and [node payload layout](../node/implementation.md#phase-1c-numeric-payloads).
Add the complete scoped GoogleTest cases before implementation. Use public
APIs for semantics, identity, and lifetime. Use the private test peer for
layout inspection, collisions, and fault injection. Preserve the semantic
coverage of the earlier tests under the project test-first rules.

The storage-accounting fixture interns the Int sort before recording its
baseline. `MakeInt` interns its result sort on first use, and
`GetStorageStats` includes all interned sorts in `metadata_bytes`. Warming
the sort isolates the measured node and numeric backing bytes.

- **N-NUMERIC-MODULE** — Build and link consumers with GCC and Clang in
  test-enabled and test-disabled configurations. A Node-only consumer can
  name and use `ErrorCode`, `ResourceKind`, `DatatypeReason`, `NodeError`,
  `Result<T>`, and `ReferenceLimit`. A util-only consumer can build typed
  values without a Node dependency and receives `numeric::NumericError` on
  failed numeric input. A consumer of both public modules uses typed Node
  factories. Check that importing the Node module alone exposes neither
  util value names nor the test peers and that the old
  `dandelion.numeric` import is unavailable. First compile positive controls
  under the same configuration to establish that failures are for the
  intended names or import. The test-enabled consumer can link and use the
  test-only header and peer without importing the private partition.
- **N-TYPED-ACCESS** — Verify that Int, BV, and FP literals populate only
  their matching one of `GetInteger()`, `GetBitVector()`, and
  `GetFloatingPoint()`. Bool and RM literals populate none of these three.
  Check `GetFpFormat()` on F(2,2), F(8,24), and a format with both fields
  above 32, and require an empty result on non-FP sorts. Check that
  `GetRationalParameter()` is absent for literals and present only on the
  rational conversion. A constant-child `fp` or `to_fp` remains an operation
  node and has no literal view.
- **N-TYPED-BV** — Compare typed and word construction in both insertion
  orders for width 1 (0 and 1), width 64 (`UINT64_MAX`), and width 65
  (`UINT64_MAX`). Check that BV65 `2^64` retains both limbs and the exact
  width. Test a valid BV view with the wrong width, a same-state non-BV
  sort, a null sort, and a foreign sort with the same width. Expect
  `kSortMismatch`, `kSortMismatch`, `kNullObject`, and `kForeignManager`,
  respectively. The word entry point reports `kInvalidLiteral` for BV1
  word 2 and retains the sort checks. Invalid requests leave the live entries
  and next node ID unchanged.
- **N-TYPED-FP** — In F(8,24), pass two bit-derived NaNs (for example
  `0x7fc00000` and `0xff800001`) and a directly constructed NaN and
  require one live identity. Check both signed zeros and both signed
  infinities, including agreement between bit-derived and direct specials.
  Check packed finite bits with the smallest positive subnormal in F(8,24),
  negative one (`0xbff0000000000000`) in F(11,53), and both the one-word
  smallest positive subnormal and the two-word finite encoding
  `(127 << 64) + 1` in F(8,65). Require the complete format and canonical
  category in each result and exact bits only for finite nonzero values.
  Test an F(8,24) value with an F(9,23) sort, which has the same total bit
  width. Expect `kSortMismatch`, also for a non-FP sort. Null and foreign
  sorts report their view errors without publication or ID consumption.
- **N-NUMERIC-KEYS-AND-LIFETIME** — Keep unequal keys live under forced
  collisions. Vary the Int sign, a low or high limb, the BV width, the FP
  format or special category, and either rational component independently.
  For each family, reconstruct a matching value from an independent util
  owner and from the returned view. Require the same live node on each hit.
  Move or destroy the original numeric source, grow the table, and verify
  the borrowed results while a node owner remains live, including when the
  facade is destroyed first. Exercise nonallocating queries with allocation
  and retain fault hooks armed. Independently copied util values remain
  exact after releasing the node.
- **N-NUMERIC-BORROWED-ROTATE** — Create an Int literal for `2^64 + 1`,
  destroy the source Integer, and pass the borrowed view of the live node
  to `BitVector::RotateLeft` and `BitVector::RotateRight`. Use the BV5 value
  `0b10011`. The count is 2 modulo 5, so the results are `0b01110` and
  `0b11100`, both with width 5. This width detects truncation of the count
  to its low word.
- **N-NUMERIC-TAIL** — On the supported 64-bit layout, check the 56-byte
  header and the count field's use of its four former padding bytes. Expect
  64-byte blocks for `INT64_MIN`, `INT64_MAX`, and the BV and finite FP
  word fixtures. Check the out-of-range one-limb Ints `2^63` and
  `-2^63-1`, also 64 bytes, with the appropriate signs. Include
  `-2^64` and `2^128+1` for two and three limbs, and a two-limb BV and
  finite FP. Check tags, alignment, trimmed counts, order, and zero low or
  interior limbs against hand-derived values. A wide view's limb span must
  refer to the corresponding bytes of the live node block. Each FP special
  requests 56 bytes with no payload tail, including on a wide format.
  Warm the result sort and reserve table capacity through fixture setup.
  With util inputs prepared before the call, require one candidate allocation
  for a wide literal miss, no secondary numeric allocation, and no allocation
  on an exact hit. Exercise a util GMP operation on a node-borrowed signed
  magnitude and packed rational component and compare exact results with
  the same operations on independently owned values while both owners live.
- **N-NUMERIC-RATIONAL** — Convert `-6/-9` and `2/3` with an identical
  RM child and format. Require one identity, a 16-byte rational tail
  following the format indices, and an 88-byte block. Check zero
  normalization, `INT64_MIN/1`, and `1/UINT64_MAX` in the same layout. For
  `(2^64+1)/3`, `-3/(2^64+1)`, and
  `-(2^64+1)/(2^65+1)`, check both trimmed counts, the sign, both
  component spans, and requested block sizes of 104, 104, and 112 bytes.
  Check a component just above the signed range using `2^63/1`. Borrow
  the parameters after destroying the util input, and exercise both rebuild
  forms with a different RM child. Drop the original and verify that the
  rebuilt node's borrowed rational, format indices, and child remain exact.
- **N-NUMERIC-STATS** — Use a manager with word and wide literals, FP
  specials, and small and wide rational conversions. Compute live node,
  edge, and `node_bytes` deltas from the independent sizes and fixture child
  slots above. `numeric_backing_bytes` stays zero for those forms. Add a
  GMP-backed wide builtin index as a control with nonzero external numeric
  backing. An additional owner on a hit contributes zero bytes and edges.
  Check that releasing some roots removes exactly the retired blocks and
  edges, and releasing the last roots clears node and numeric backing bytes.
  Keep table and metadata accounting separate. Use requested bytes for
  correctness checks and the [allocation benchmark](../node/implementation.md#phase-1c-allocation-measurements)
  for allocator size classes and the 48-byte header comparison.
- **N-NUMERIC-ERRORS** — Check each `NumericError` mapping, including
  invalid numeric input as a literal or parameter error, a zero denominator
  as `kInvalidParameter`, `kSizeLimit` as `kResourceLimit/kSize`, and
  `kCppAllocation` as `kResourceLimit/kCppAllocation`. Exercise actual
  factory call sites where a util failure is reachable and the private
  translation entry for any mapping without a reachable public error path.
  Check exact indices
  when a Node operand or parameter position is known, sentinel values for
  unused positions, and `kNone` for the datatype reason. Keep a checked
  width or format mismatch and an owning-handle reference overflow as
  controls for errors originating in Node. Keep GMP termination tests
  separate from recoverable fault injection.
- **N-NUMERIC-ROLLBACK** — For word, special, wide literal, and both rational
  layouts, fail each reachable project allocation and retain in a miss.
  Use warmed fixtures both without and with required table growth. Force a
  checked layout size failure with a limit between adjacent requested sizes,
  and exercise limb-count conversion and byte-size overflow using bounded
  layout inputs through the private peer. Fail the result retain on an
  exact hit. For rational construction and changed rebuild, fail state
  acquisition and the borrowed child retain after payload preparation, and
  the applicable pre-transfer failures on the consuming path. After each
  failure verify the error detail, exact survivor lookups, live node and
  edge counts, requested node and numeric backing bytes, state and child
  references, and next node ID. Source values and consuming input owners
  remain intact. A candidate allocation is paired with one reclamation
  or rollback free. Table capacity and valid derived sorts may remain.
  Clear the fault and retry with the same input, checking the expected ID,
  key, and references. Run the sweeps and reclamation under the existing
  sanitizer acceptance configuration.

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

The implemented frontend retains a defined-call identity and argument, with the
formal and body owned in its definition environment. When a consumer later
instantiates the body, both addition operands refer to the actual argument;
no formal is free in the query root and the call is not a free UF. Capture,
shadowing and definition-environment lifetime have separate tests. This is a frontend integration case, not a NodeManager capability.

For the implemented frontend Phase 1 body aliases, define two closed nullary
names with the same typed BV body and assert their equality. Both operands
must have the body's node identity, with distinct source occurrences.
Repeat with a nullary alias whose body calls a definition with formals.
The alias must resolve to that typed application, and the sealed query
must still own its definition environment. Check shadowing uses the
definition-site binding, invalid result sorts produce no binding, and a
later model-value request resolves the same body under the owning context.

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
| Numeric boundaries | Check exact packed values and payload sizes, including inline Int, BV, FP and Rational limbs and external backing for wide indices. Run import, capacity, ordering and FP rounding cases in the standalone [util suite](util.md). |
| Failure atomicity | Sweep allocation checkpoints for Node blocks with packed payloads and external wide indices. Fail a late retain beyond the inline child count and compare all counts and IDs before retry. Exhaust constructor, declaration, and implicit Bool sort identities independently during datatype commit while retaining a prior instantiated signature. |
| Datatype productivity | A constructor requiring both B and C must wait for both. Use A → (B,C), B → base, C → A, then repair C with a base. A reverse-order chain becomes productive from its final base. |
| Table and ownership | Delete a record from a capacity-16 cluster with homes 15, 15, 1, 15. Re-query survivors immediately after failed compaction and after growth. Compare batch and sequential release when few or no nodes survive, including replacement allocation failure. Exercise each metadata handle and ManagerRef as sole state owner, overflow through Attach and Child, and staged recipes after builder moves. |
| Traversal | In a sparse graph with repeated roots, sweep visit-table allocation failures and compare the entire delivered prefix to a successful traversal. Check that errors are terminal and roots remain held until walker destruction. A postorder callback can retain a child and return an unchanged full NodeError. |

Standalone numeric calls use `NumericError::kSizeLimit` for a checked
capacity failure. For example, binary and hexadecimal spellings of `2^127`
occupy exactly two 64-bit limbs. Radix import succeeds at a two-limb cap and
reports `kSizeLimit` below it. Derive working-limb requirements for decimal
import and FP rounding from the current numeric operation. Preserve the
mathematical expectations, including rounding `(2^64 - 1)/(2^64 + 1)` to 1
under RNE and to 3/4 under RTZ in FP(8,2). The [util suite](util.md) owns
numeric capacity tests. The [typed Node cases](#phase-1c-typed-literals-and-inline-numeric-payloads)
cover Node's translation to its own error types.

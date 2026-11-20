# Expression Nodes and Node Manager

This document records the first-version contracts and the implemented Phase 1
and Phase 2 additions. [Phase 1C](#10-phase-1c-numeric-boundary-and-typed-literals)
specifies the typed literal API and the boundary between Node and util.
[Node overview](README.md)

The companion [operator specification](operators.md) and
[implementation plan](implementation.md) specify the representation and
behavior. [Node testing](../testing/node.md) sets the test-first acceptance
requirements.

## 1. Responsibility and scope

The expression layer represents well-sorted, quantifier-free terms over Bool,
fixed-size bit-vectors, floating point, rounding modes, unbounded integers,
extensional arrays, uninterpreted sorts and functions, and inductive
datatypes, including their supported nested combinations. It preserves
high-level operators until a separate transformation lowers them. Representing
a term does not establish satisfiability or completeness of its theory
combination. General Real and String theories, user quantifiers, higher-order
maps and applications, and user-facing incremental sessions are out of scope.

The central representation is an immutable typed expression DAG. A
`NodeManager` checks and interns nodes. Sorts and declarations have their own
identities. The structural unique table does not own nodes. Rewrite results,
SAT assignments, equality classes, theory state, and models belong to their
components, not to node meaning.

```text
frontend / programmatic API
             |
       NodeManager facade
             |
        ManagerState
   +---------+----------+
   |         |          |
SortStore  DeclStore  weak UniqueTable
                         |
                  immutable Node DAG
                         |
           rewriter / theory / model
```

These are responsibilities, not a plugin system or one build target per box.
Construction does not reorder or flatten operands, remove duplicates, fold
general operations, or merge nodes because assertions make them equal.
Canonical literal values and specified frontend lowerings are the exceptions
to retaining source structure.

## 2. Identity and equality

Each object belongs to one `ManagerState`. Independently created states have
separate identity spaces; facades attached to the same state share them.
Factories reject foreign children, sorts, declarations, and views. No implicit
cross-manager translation is provided.

- `SortId`, `SortConstructorId`, `DeclId`, `DeclInstanceId`, and
  `DatatypeGroupId` are nonzero 64-bit manager-local IDs; zero is null.
  Published IDs are not reused while the state lives.
- `NodeId` is a nonzero 64-bit manager-local ID, monotonically allocated on
  successful insertion and not reused while the state lives. A table hit does
  not consume an ID. After the final legal ID, set an exhausted flag instead
  of wrapping; a later miss reports a resource limit and a hit may succeed.
- A bare ID neither owns an object nor identifies one across states. There is
  no public lookup from an old `NodeId` to a node. A reclaimed structure can
  receive a new ID when reconstructed.

For simultaneously live nodes in the same state, the complete structural key
identifies exactly one node:

```text
Kind, result SortId, child count, ordered child identities,
payload tag, and all applicable immutable payload fields
```

Payload fields include the applicable canonical literal, declaration instance,
numeric indices, or rational parameter. A hash match always requires full key
comparison. Child order remains significant for commutative operations.
`DeclId` distinguishes symbols with the same name and signature; different
concrete datatype instances use different `DeclInstanceId`s.

`Node` equality compares live node identity, not theory equality. Null nodes
compare equal to null and have ID zero and a fixed hash. Nodes from different
states compare unequal. `NodeHash` hashes live identity consistently with
equality. Structural hashes are internal, not a persistent format.

Distinguish structural identity, semantic equality of different expressions,
and search-dependent equality derived from assertions. An equality engine
never mutates node identity. Algorithms may not assume a parent/child ID
ordering; one that requires such an invariant must establish it separately.

## 3. Sorts and declarations

All published expression nodes have concrete sorts. Type parameters occur in
datatype declaration recipes, not in the sort of a published expression.

| Sort kind | Structural key and validity |
| --- | --- |
| `kBool`, `kInt`, `kRoundingMode` | Kind only. |
| `kBitVector` | Kind and width `w > 0`. |
| `kFloatingPoint` | Kind and `e > 1`, `s > 1`; `s` includes the hidden bit. |
| `kArray` | Kind, ordered concrete index sort and element sort IDs. |
| `kUninterpreted` | Kind, declared sort-constructor identity, ordered concrete sort arguments. |
| `kDatatype` | Kind, committed datatype constructor identity, ordered concrete type arguments. |

BV width, FP `e`, FP `s`, and `e+s` must fit `uint32_t`; a mathematically
valid request beyond that capacity returns `kResourceLimit`. Parse numerals
exactly before range checks. Any supported concrete sort can be an array index
or element. `Bool` and `BV(1)` differ. Function signatures live in declarations;
the core does not create first-class function sorts.

These **implemented representation bounds are the selected project baseline**.
They are not mathematical SMT-LIB syntax restrictions or configurable frontend
quotas. A valid width or format beyond the bound reports a representation
failure, not an invalid mathematical parameter. The current manager-local ID
representations also remain unchanged. The agreed direction excludes separate
discretionary resource quotas; any later representation change must update its
contracts and tests under the project's test-first rules.

Each uninterpreted sort declaration has a fresh constructor identity and
arity. Instantiation requires exactly that many same-state concrete sorts.
Frontend `define-sort` expands an alias and does not create a new semantic sort
identity. Published sort and declaration records live until state destruction.

Phase 1 adds a `MakeBvSort(std::uint64_t)` overload for a width already
available as a machine word. Zero reports `kInvalidParameter`; a width above
`UINT32_MAX` reports `kResourceLimit/kSize`. Validate before narrowing.
Both overloads and derived BV sort inference return the same canonical
sort identity for a given state and width.

```cpp
enum class DeclKind : std::uint8_t {
  kFunction,
  kInternalFunction,
  kFormal,
  kDatatypeConstructor,
  kDatatypeSelector,
  kDatatypeTester,
};
```

An ordinary or internal declaration has a fresh `DeclId`, name hint, ordered
concrete domain, and range. A zero-argument declaration is a constant.
A formal is a fresh zero-argument placeholder in a frontend definition
template; it must not remain in roots submitted to solving. A datatype
constructor has a group identity, constructor ordinal, ordered field templates,
and result template. Each selector and tester has its own declaration identity
and links to its constructor and, for a selector, field ordinal.

The first-version frontend retains applications of internal declarations
for nonrecursive definitions. Under the Phase 1 Node boundary, only
definitions with formal parameters require retained declarations and
instances. The frontend may bind a closed nullary definition directly to
its typed body. It owns the body and preserves source name, rank, definition
record, and occurrence information for scope, diagnostics, and output.

For retained calls, the owning definition table records the body and formal
identities. The sealed query context retains the table and the `ManagerRef`.
Every query or model using a retained call needs that environment to
interpret it as the associated definition. An aliased body can itself
contain retained calls and therefore needs the same environment. Model-local
abstract values can also use fresh internal constant nodes, distinguished
by a model-owned binding table. Source scope and `:named` closedness use
frontend occurrence records. Frontend admission checks the active signature
and source profile while keeping retained calls compressed.
[Frontend semantic interfaces](../frontend/semantic-interfaces.md)

A `DeclInstance` has identity `(DeclId, ordered type arguments)` and exposes
the concrete domain and range. An ordinary function has one instance with
no type arguments and shares its signature with the declaration. A nullary
datatype constructor also has a concrete instance:
`nil : List<Int>` differs from `nil : List<BV8>`. Display names are for
diagnostics, not identity or frontend scope resolution.

## 4. Datatype group transactions

Datatype groups use flat, builder-local `SortExpr` recipes:

```text
Concrete(SortId)
Parameter(datatype_slot, parameter_index)
Array(index_recipe, element_recipe)
Apply(existing_sort_constructor, ordered_argument_recipes)
Recursive(group_slot, ordered_argument_recipes)
```

Recipes contain local indices or same-state IDs, not owning public handles.
Parameters must belong to the datatype whose field is being defined. Validate
all references, arities, signatures and sort recipes. After alias expansion,
reject occurrences of any datatype from the current group below the top symbol
of a constructor-field sort.

Compute productivity to a least fixed point: initially mark a datatype
productive if one constructor has no current-group field dependency; then
mark one productive when a constructor's dependencies are all productive.
Every group member must be productive. A list with `nil` and `cons` is valid;
`D = loop(D)` is not. A recursive field `(Array Int D)` is outside the chosen
standard profile. A field `D(Array Int A)` in parameterized `D` has `D` at the
top and is allowed when all other rules hold. Ordinary array/DT combinations
and nonrecursive array fields remain supported.

Commit constructs and validates local records, reserves destination capacity,
checks ID and size limits, and publishes the entire group using nonthrowing
moves. Failure publishes no group member or visible name and consumes no
published IDs. A failed builder remains open for correction or retry; success,
`Abort()`, and destruction close it. Instantiation substitutes a finite recipe
on demand; it does not recursively expand every possible instance.
[SMT-LIB 2.7 §4.2.3](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-09-09.pdf)

## 5. Exact values

`Integer` uses canonical signed `int64_t` storage when possible, and an owned,
private GMP `mpz_t` otherwise. Values fitting `int64_t` always use the small
form. Parsing, comparison, hashing, sign, bit length, and narrowing are exact.
Computing the magnitude of `INT64_MIN` must not overflow signed arithmetic.
The GMP C API is private; neither `mpz_t` nor writable limbs are exported.
Published payloads own their values and cannot be changed through the caller's
original variable or a returned view.
[Phase 1C payload design](implementation.md#phase-1c-numeric-payloads)

A `Rational` is a normalized numerator and positive denominator with gcd one;
zero is `0/1`. A zero denominator is invalid at the numeric API. The
frontend's rational-to-FP compatibility grammar is specified in
[operators.md](operators.md#6-floating-point).

A BV value of width `w` has an exact payload `0 <= v < 2^w`.
`MakeBvValue` requires a canonical `numeric::BitVectorView` of the sort's
width. To prepare a view from an integer without reduction, use
`numeric::BitVector::FromIntegerExact`, which checks sign and bit length.
The frontend reduces the unsigned numeral in SMT-LIB `(_ bvN w)` modulo
`2^w` before constructing the Node value. The separate `int_to_bv` operation
also has modulo semantics and is retained as an operation node.
Phase 1's `MakeBvWordValue` accepts a `std::uint64_t` directly and uses the
same canonical key and exact fit rule as `MakeBvValue`. It reports
`kNullObject` for a null sort, `kForeignManager` for a foreign sort,
`kSortMismatch` for a same-state non-BV sort, and `kInvalidLiteral` for a
value outside the width.

An explicit FP value is its FP sort and `NaN`, positive or negative infinity,
positive or negative zero, or `FiniteBits` containing exact nonzero finite
interchange bits. NaN bit encodings normalize to one abstract NaN per FP sort;
signed zeros remain different under core `=`. `fp` and `to_fp` operation nodes
are retained even for constant children. They may denote the same value as a
literal without having the same structural identity. Rounding modes are RNE,
RNA, RTP, RTN, and RTZ. MPFR is not required by this expression representation.

To create an FP value from packed bits, use the checked util constructor. It
requires nonnegative `bits` with bit length at
most `e+s`. The high bit is sign, the next `e` bits are exponent, and the low
`s-1` bits are fraction. An all-one exponent and nonzero fraction is NaN; an
all-one exponent and zero fraction is signed infinity; an all-zero exponent
and zero fraction is signed zero. Every remaining pattern is finite nonzero
`FiniteBits`, including subnormals. Inspect fields without materializing a
huge all-ones constant merely to recognize a special value. The util special constructor selects
the corresponding canonical category directly.

## 6. Ownership and lifetime

| Object | Initial representation and ownership |
| --- | --- |
| `NodeManager` | Move-only facade with one owning `ManagerRef`; `Attach` can create another facade for the state. |
| `Node` | One `NodeData*` owning a node reference; null is permitted. |
| `NodeView` | Borrowed `NodeData*`; it retains neither node nor state. |
| `Sort`, `SortConstructor`, `Decl`, `DeclInstance`, `DatatypeGroup` | State pointer and local ID, owning one state reference. |
| Corresponding views | Borrowed state pointer and ID, without incrementing the count. |
| `DatatypeBuilder` | Move-only, owns one state reference and local staging data. |
| `ManagerState` | Owns stores, the weak unique table, counters, and allocator. |

Identities, logical arities, child counts, and recipe and diagnostic indices use
unsigned 64-bit machine integers. Storage is also bounded by the backing
container and allocation sizes.

Each live `NodeData` owns one state reference and one node reference per child
slot, including repeated edges. Each owning `Node` owns one node reference.
A `Node` copy retains only the node: its `NodeData` keeps the state alive.
Manager-owned records contain local IDs and plain data, not owning public
handles or `Node`s, avoiding state-to-object-to-state cycles. Frontend
definition bodies, query roots, rewrite caches, and model owners live outside
the state and explicitly own what they retain.

A node, sort, declaration, or model may outlive the facade. A model must own
its manager reference, necessary roots, values, and recovery information
independently of a solver it can outlive. Borrowing is valid only while an
owner of the referenced object remains alive. `Borrow()` is available on
lvalues and deleted on rvalues; it does not extend the lifetime. A walker
owns its roots.

A state is confined to one thread at a time; its counts are non-atomic.
Different states may be used concurrently. Transferring a state between
threads requires external synchronization and no concurrent access. GMP's
process-wide allocator configuration is not changed.

## 7. Errors and factory guarantees

```cpp
template <class T>
using Result = std::expected<T, NodeError>;

enum class ErrorCode : std::uint8_t {
  kNullObject, kForeignManager, kInvalidKind, kInvalidArity,
  kInvalidParameter, kSortMismatch, kInvalidLiteral, kInvalidDatatype,
  kBuilderClosed, kInvalidTraversalState, kUnsupportedFeature,
  kResourceLimit,
};
enum class ResourceKind : std::uint8_t {
  kNone, kCppAllocation, kSize, kIdentitySpace, kReferenceCount,
};
class ReferenceLimit final : public std::exception {
 public:
  const char* what() const noexcept override;
};
struct NodeError {
  ErrorCode code;
  ResourceKind resource;
  std::uint64_t operand_index;
  std::uint64_t parameter_index;
  DatatypeReason datatype_reason;
};
```

`NodeError` has fixed-size fields and requires no allocation; an inapplicable
index is `UINT64_MAX`. The frontend associates source occurrences separately.
The ownership and translation of numeric errors are specified in
[Phase 1C](#phase-1c-error-translation).
Datatype diagnostics use a fixed-size, defaulted reason field. The builder
reports `kEmptyGroup`, `kNoConstructor`, `kInvalidParameterScope`,
`kNestedRecursiveSort` and `kNonproductive` under the established code
`kInvalidDatatype`. Other errors default to `kNone`.
[Frontend datatype contract](../frontend/design.md#datatype-publication-and-match)
If several errors apply, diagnostic order is unspecified, but the reported
error must be real. Factories check manager, kind, arity, payload, sort, and
capacity before publishing a node. They return `Result` for ordinary failures
and caught C++ allocation failures. Failure cannot expose a malformed node,
partial datatype group, leaked references, or an incorrect solver result.
Retaining a valid interned result sort or increased table capacity after a
failed node construction is allowed; existing objects remain unchanged.

An owning-handle copy or owning accessor cannot return `Result`: checked
reference-count overflow throws a fixed-size `ReferenceLimit` exception before
changing the destination. `ReferenceLimit::what()` returns a fixed static
English string and performs no allocation. Copy assignment retains the new object before
releasing the old one. Move, destruction, table erasure, and reclamation are
`noexcept`; self-copy and self-move preserve a valid value. Internal count
underflow is an invariant violation and fails fast, never a solver answer.

GMP's default limb allocation failure terminates the process rather than
providing a recoverable C++ exception. Dandelion does not install a process-wide
GMP allocator or throw/`longjmp` from it. Checked limits and C++ allocation
failures can be reported, but arbitrary GMP OOM cannot be promised recoverable.
Numeric input conversions and arithmetic report `kResourceLimit/kSize` when
the GMP operation requires more limbs than its allocation count represents.
The check includes temporary or spare limbs required by the operation.
A terminated process must not fabricate `sat` or `unsat`.
[GMP custom allocation](https://gmplib.org/manual/Custom-Allocation)

## 8. Initial API

The following shows the implemented NodeManager API. The
[typed literal and format queries](#phase-1c-typed-literal-and-sort-api)
describe the numeric boundary.
The [Phase 2 consuming additions](#phase-2-consuming-construction) follow
the rebuild contract.
Returned owning objects hold their references. Factory inputs are borrowed
for the call and spans, names, and temporary views are not retained as payload.

```cpp
struct BuiltinOpView {
  Kind kind;
  std::span<const numeric::Integer> indices;
  std::optional<SortView> qualifier;
  std::optional<numeric::RationalView> rational;
};

class NodeManager {
 public:
  static Result<NodeManager> Create();
  static Result<NodeManager> Attach(const ManagerRef& state);
  Result<NodeStorageStats> GetStorageStats() const noexcept;
  Result<void> CompactNodeTable();
  Result<ManagerRef> RetainState() const;
  Result<Node> RetainNode(NodeView node);
  Result<void> ReleaseRoots(std::span<Node> roots);
  Result<Sort> RetainSort(SortView sort);
  Result<SortConstructor> RetainSortConstructor(SortConstructorView ctor);
  Result<Decl> RetainDeclaration(DeclView declaration);
  Result<DeclInstance> RetainDeclInstance(DeclInstanceView instance);
  Result<DatatypeGroup> RetainDatatypeGroup(DatatypeGroupView group);

  Result<Sort> BoolSort();
  Result<Sort> IntSort();
  Result<Sort> RoundingModeSort();
  Result<Sort> MakeBvSort(numeric::IntegerView width);
  Result<Sort> MakeBvSort(std::uint64_t width);  // Phase 1
  Result<Sort> MakeFpSort(numeric::IntegerView exponent,
                          numeric::IntegerView significand);
  Result<Sort> MakeArraySort(SortView index, SortView element);
  Result<SortConstructor> DeclareUninterpretedSort(
      std::string_view name_hint, std::uint64_t arity);
  Result<Sort> InstantiateSort(SortConstructorView constructor,
                               std::span<const SortView> arguments);

  Result<Decl> DeclareFunction(std::string_view name_hint,
                              std::span<const SortView> domain,
                              SortView range);
  Result<Decl> FreshFunction(std::string_view name_hint,
                            std::span<const SortView> domain,
                            SortView range);
  Result<Decl> MakeFormal(std::string_view name_hint, SortView sort);
  Result<DeclInstance> InstantiateDeclaration(
      DeclView declaration, std::span<const SortView> type_arguments);

  Result<Node> MakeBool(bool value);
  Result<Node> MakeInt(numeric::IntegerView value);
  Result<Node> MakeBvValue(SortView sort, numeric::BitVectorView value);
  Result<Node> MakeBvWordValue(SortView sort, std::uint64_t value);  // Phase 1
  Result<Node> MakeFpValue(SortView sort, numeric::FloatingPointView value);
  Result<Node> MakeRoundingMode(numeric::RoundingMode value);
  Result<Node> MakeFreshConstant(std::string_view name_hint, SortView sort);
  Result<Sort> CheckBuiltin(BuiltinOpView op,
                            std::span<const NodeView> children);
  Result<Node> MakeBuiltin(BuiltinOpView op,
                            std::span<const NodeView> children);
  // Phase 1
  Result<Node> MakeWordIndexedBuiltin(
      Kind kind, std::span<const std::uint64_t> indices,
      std::span<const NodeView> children);
  Result<Node> RebuildNode(
      NodeView original, std::span<const NodeView> children);  // Phase 1
  Result<Node> MakeApply(DeclInstanceView declaration,
                         std::span<const NodeView> arguments);
  Result<DatatypeBuilder> BeginDatatypeGroup(
      std::span<const DatatypeHeader> headers);
};
```

`IntegerView` can directly represent a signed or unsigned word or borrow a
larger `Integer` from a live owner. It exposes exact sign, comparison, bit
length, hash, and checked narrowing without writable GMP state. `IsSmall()`
means the value fits a signed word. `ParseDecimal`
accepts an optional leading minus and one or more decimal digits, with no
whitespace or leading plus; leading zeroes and negative zero normalize to the
same mathematical value. Util reports malformed text as `kInvalidText` and
a zero rational denominator as `kDivisionByZero`. `ToDecimal` returns a
canonical signed decimal string and can throw C++ allocation failure.

`CheckBuiltin` and `MakeBuiltin` use the same operator rules. The former may
intern a valid result sort but does not create a node. Fresh declarations
never intern by name. Source namespace and redeclaration checks
belong to the frontend.

`MakeWordIndexedBuiltin` has the behavior of `MakeBuiltin` with the same
kind, exact index values and ordered children, and absent qualifier and
rational fields. It uses the same validation and canonical structural key,
with no caller-owned `Integer` vector required. Use the general factory for
indices beyond the unsigned word range or payloads requiring a qualifier
or rational.

### Phase 1 payload-preserving rebuild

`RebuildNode` accepts a live, nonnull original in the receiving manager
state. The replacement span must have the same length as
`original.GetChildren()`. Each replacement must be live, nonnull, in that
state, and have the same concrete sort as the original child at its position.
The caller keeps the original and replacements alive throughout the call.
Null views, foreign states, wrong arity and wrong operand sorts return
`NodeError` with `kNullObject`, `kForeignManager`, `kInvalidArity` or
`kSortMismatch`, respectively. Report the child position for a child error.

When every child has the same identity as the original child at its
position, checked-retain and return the original directly, without hashing
a key or probing the table. This includes all zero-child nodes. Otherwise,
intern the replacement child sequence while preserving the original kind,
established result sort, and exact payload, including indices, qualifier,
rational parameter, or `DeclInstance` identity as applicable. Same-position
sort equality preserves the original rank. The rebuild uses it directly,
skipping `CheckBuiltin` and result-sort inference and borrowing the original
payload without reconstructing an index vector. The result shares identity
with ordinary construction of the same kind, children, and payload while
both results are live. An exact hit returns a new owner of that node.
A miss owns its payload and child edges before
publication. The factory guarantees in §7 apply to both paths.

### Phase 2 consuming construction

Add these checked `NodeManager` members.

```cpp
Result<Node> MakeBuiltinConsuming(BuiltinOpView op,
                                   std::span<Node> children);
Result<Node> MakeWordIndexedBuiltinConsuming(
    Kind kind, std::span<const std::uint64_t> indices,
    std::span<Node> children);
Result<Node> RebuildNodeConsuming(NodeView original,
                                  std::span<Node> children);
Result<Node> MakeApplyConsuming(DeclInstanceView declaration,
                                 std::span<Node> arguments);
Result<Sort> CheckBuiltinOperands(BuiltinOpView op,
                                  std::span<const Node> children);
```

Every slot is a distinct mutable owning handle, with the range kept live
and stable for the call. The slots may own the same node. Validate null
and foreign handles, typed rank, index and payload rules and error
attribution as for the equivalent borrowed factory. The same-position
sort and live-original requirements of `RebuildNode` apply. Payload views,
including views into the original or operands, remain valid through key
comparison and preparation. If the original is owned by an input slot
with no other owner, the caller's original view is usable after a miss
only if the result or another live owner keeps that node alive.

| Outcome | Inputs and result |
| --- | --- |
| New parent | Transfer the ownership of each slot into the corresponding published child edge and make each input slot null. Each transferred reference remains counted once, with no per-edge retain or release. Adopt the new parent as the result. |
| Exact hit | Checked-retain and return the canonical parent. Every input slot still owns its original node. This includes an unchanged rebuild and an empty operand range. The caller can release inputs normally. |
| Recoverable error | Return the corresponding `NodeError`. Every input slot still owns its original node. Publish no parent or new node ID. The existing private capacity and derived sort guarantees apply. |

A consumed `[a, a]` has two input owners and two edges. The parent
destructor releases both references. Separate caller-owned children,
roots and other parents retain their own references. Since transfer
already has the needed references, exhaustion that would block a
borrowed child retain does not block a move-in miss. A failed result
retain on a hit or manager-state retain on a miss still reports
`kResourceLimit/kReferenceCount`. The existing borrowed factories remain
available for callers with borrowed inputs. Both paths share the same key
and payload storage and interoperate with the same unique table.

The consuming interface takes contiguous `Node` storage. Frontend
operands use that layout with separate position and dependency buffers.
An array of larger records with a `Node` member must never be cast to a
`std::span<Node>`. Internally, rank validation, hashing, equality and
payload preparation read each child through its typed input range. This
avoids building a second array of `NodeView` handles.
`CheckBuiltinOperands` is the check-only counterpart of `CheckBuiltin`
for owning slots. It returns the inferred sort or the same structured
diagnostic and preserves each slot, without allocating a parent. The
frontend uses it to replay rank validation in written order when an
optional operand permutation fails.

### Handle ownership and views

The `Retain*` operations check nonnull and same-state views and return new
owning handles, translating count exhaustion to `kResourceLimit`. A borrowed
view's owner must remain alive throughout the call; these operations cannot
recover a reclaimed object from a stale pointer or raw ID.

`RootRange` has private storage and can be constructed only through typed
`RootRange::From(std::span<Node>)` or
`RootRange::From<&Record::member>(std::span<Record>)` factories, where the
member is a `Node`. Arbitrary user callback pointers are not accepted.
The static batch `ReleaseRoots` accepts a manager and a span of these ranges;
it validates all slots before clearing any; reclamation may attempt a table rebuild.

`ReleaseRoots` consumes a span of owning handles, clearing each slot. Null
slots are allowed. A foreign handle rejects the whole span before any slot
changes. Surviving nodes retain their identities and all zero-reference
nodes in the batch are reclaimed before the call returns.

```cpp
class NodeView {
 public:
  bool IsNull() const noexcept;
  NodeId GetId() const noexcept;
  Kind GetKind() const noexcept;
  SortView GetSort() const noexcept;
  NodeChildren GetChildren() const noexcept;
  NodeIndices GetIndices() const noexcept;
  std::optional<DeclInstanceView> GetDeclaration() const noexcept;
  std::optional<LiteralView> GetValue() const noexcept;
  std::optional<RationalView> GetRationalParameter() const noexcept;
  bool SameManager(NodeView other) const noexcept;
};

class Node {
 public:
  Node() noexcept;
  Node(const Node&);
  Node& operator=(const Node&);
  Node(Node&&) noexcept;
  Node& operator=(Node&&) noexcept;
  ~Node() noexcept;
  NodeView Borrow() const & noexcept;
  NodeView Borrow() const && = delete;
  NodeId GetId() const noexcept;
  Sort GetSort() const;
  Result<Node> Child(std::size_t index) const;
  friend bool operator==(const Node&, const Node&) noexcept;
};
```

`NodeChildren` iterates as borrowed `NodeView` values. It is not a
`span<Node>`: the selected storage holds `NodeData*` slots, not an array of
live `Node` objects. `Child` returns an owning node; an out-of-range index is
`kInvalidParameter`. Null handles may be moved, compared, hashed, and queried
for ID; other accessors require a live, nonnull object. Wrong view lifetime is
a caller contract violation. `NodeIndices` is a borrowed range in operand
index order. Its elements expose an exact `IntegerView` via `Borrow()` and
can format it with `ToDecimal()`. General builtin construction takes
`Integer` objects. Phase 1 also accepts a span of exact unsigned words for
word-sized indices and can reuse the stored payload directly when rebuilding.
`SortView` exposes kind and applicable parameters;
`DeclView` and `DeclInstanceView` expose category, identity, type arguments,
signatures, and datatype links; `LiteralView` exposes its immutable value.

The remaining read-only query contracts are:

| View | Required queries |
| --- | --- |
| `SortView` | Null and manager identity, `SortId`, kind, applicable BV width or FP `e/s`, array index/element, sort constructor, and ordered concrete arguments. |
| `SortConstructorView` | Null and manager identity, ID, arity, uninterpreted/datatype category, and applicable datatype group. |
| `DeclView` | Null and manager identity, ID, category, name hint, type-parameter count, concrete signature or datatype signature recipe, and applicable constructor/field links. |
| `DeclInstanceView` | Null and manager identity, ID, original declaration, ordered concrete type arguments, ordered domain and concrete range. |
| `DatatypeGroupView` | Ordered datatype/constructor/field counts and checked access to their sort-constructor, constructor, selector, and tester declarations. |
| `LiteralView` | Applicable Bool, exact Integer/BV value, FP category/finite bits, or rounding mode, as a read-only view. |

Wrong-category optional queries return `nullopt`; checked indexed group
queries return `kInvalidParameter` for an out-of-range index. View ranges over
stored sort IDs yield `SortView` by value with the owning state as context;
they are not fabricated by reinterpreting an ID array as a `SortView` array.
All views and ranges require their documented owner to remain alive. Indexing
a borrowed range requires `index < size()`.
For `DeclView::GetDomain()`, datatype constructors and selectors expose their
signatures as recipes. A tester has a concrete Bool range, but its domain is
also a recipe and its concrete domain query is empty. Use the corresponding
instance for the complete concrete signature.

```cpp
// SortExpr tokens require the same open builder staging transaction.
// They survive a builder move and expire on successful commit or abort.
struct DatatypeHeader {
  std::string_view name_hint;
  std::uint64_t parameter_count;
};
struct DatatypeFieldSpec {
  std::string_view name_hint;
  SortExpr sort;
};
class DatatypeBuilder {
 public:
  Result<SortExpr> Concrete(SortView sort);
  Result<SortExpr> Parameter(std::uint64_t datatype_slot,
                             std::uint64_t parameter_index);
  Result<SortExpr> Array(SortExpr index, SortExpr element);
  Result<SortExpr> Apply(SortConstructorView constructor,
                         std::span<const SortExpr> arguments);
  Result<SortExpr> Recursive(std::uint64_t datatype_slot,
                             std::span<const SortExpr> arguments);
  Result<void> AddConstructor(std::uint64_t datatype_slot,
                              std::string_view name_hint,
                              std::span<const DatatypeFieldSpec> fields);
  Result<DatatypeGroup> Commit();
  void Abort() noexcept;
};
```

`SortExpr` is a builder-local token, not a published `Sort`. The builder copies
names and recipes. The frontend checks source name conflicts before commit;
the builder checks structural and sort conditions for every caller.


## 9. Implemented query details

The public module in `src/node/node.cppm` supplies the concrete view methods
and range interfaces. `SortRange` and `NodeChildren` provide `size`, `empty`,
indexing and range iteration; their iterators borrow the owning state or DAG.
`SortRecipeView` exposes category, concrete sort, parameter scope/index,
existing constructor or recursive group slot, and checked recipe arguments.
Datatype declarations expose checked domain recipes and an optional range
recipe; ordinary declarations expose concrete signatures. Wrong-category
optional queries return no value. Numeric views expose read-only magnitude
chunks in least-significant-first order. A wide numeric
view requires its backing value to stay alive and unchanged. Replacing, moving
from, or destroying the owning numeric value may invalidate the view.
Each numeric literal exposes its corresponding
[typed value view](#phase-1c-typed-literal-and-sort-api).

`SortRecipeView::GetId() -> std::uint64_t` identifies that recipe only
within its owning datatype group and NodeManager state, for as long as the
borrowed view is valid. Repeated views of the same recipe in that group have
the same ID; different recipe records have different IDs. Equal IDs from
different groups or managers do not identify the same recipe, and separately
created but structurally equal recipes need not have the same ID. The
frontend retains the owning group and uses this ID with the actual SortId for
one constructor-match memo. This query does not change recipe ownership or
expose a mutable recipe. [Node tests](../testing/node.md)

Node events have a null parent and `UINT64_MAX` index; root events carry the
root occurrence index; edge events carry the child, parent and child-slot index.
These are traversal metadata and do not enter a node key. The walker owns all
roots. A moved-from walker reports `kInvalidTraversalState`; a moved-from
manager's factories report `kNullObject`.

`VisitDagPostorder(roots, visit)` calls a nonempty visitor once per reachable
node after its children, in root and stored-child order. It accepts an empty
root range and rejects null or mixed-state roots before the first callback.
It owns root copies for the duration of the call. The callback may retain
views as owning handles. The first visitor error ends the walk.

The normalized Rational invariant holds after failed copy assignment: the
destination is unchanged. Moving an Integer leaves zero; moving a Rational
leaves canonical 0/1. Rational moves do not allocate, are noexcept, and preserve
self-move. Numeric and manager allocation injection exists only in test builds. Normal public factories still catch real `std::bad_alloc` and
size errors, with GMP limb allocation following its independent fatal policy.

## 10. Phase 1C numeric boundary and typed literals

Standalone exact values and their operations belong to
`dandelion.util.numeric`, whose declarations are in `dandelion::numeric`.
Node owns checked expression construction, sort compatibility, interning,
and the storage for published node values. It uses a direct
`import dandelion.util.numeric;`. Callers that name or construct util values
import that module explicitly. The former `dandelion.numeric`
module was removed in Phase 4A.
Public module interfaces carry no test peer declarations or test-only friends.
[Numeric layer contracts](../util/design.md)
[Phase 1C module and test boundary](implementation.md#phase-1c-module-and-test-boundary)

### Phase 1C typed literal and sort API

The signatures below show the affected methods in their owning classes.
`MakeInt` and `GetRationalParameter` retain their contracts using util's
views. `MakeBvWordValue` retains its direct `uint64_t` input and fit check.

```cpp
// NodeManager
Result<Node> MakeInt(numeric::IntegerView value);
Result<Node> MakeBvValue(SortView sort, numeric::BitVectorView value);
Result<Node> MakeBvWordValue(SortView sort, std::uint64_t value);
Result<Node> MakeFpValue(SortView sort, numeric::FloatingPointView value);

// LiteralView
std::optional<numeric::IntegerView> GetInteger() const noexcept;
std::optional<numeric::BitVectorView> GetBitVector() const noexcept;
std::optional<numeric::FloatingPointView> GetFloatingPoint() const noexcept;

// SortView
std::optional<numeric::FpFormat> GetFpFormat() const noexcept;

// NodeView
std::optional<numeric::RationalView> GetRationalParameter() const noexcept;
```

- Validate the explicit sort's ownership and category. A null sort reports
  `kNullObject`, a foreign sort reports `kForeignManager`, and a sort of the
  wrong category reports `kSortMismatch`. For `MakeBvValue`, the width in the
  typed value must equal the BV sort width. For `MakeFpValue`, both format
  fields must equal the sort's exponent and significand widths. A disagreement
  reports `kSortMismatch`. The util value's construction establishes its
  numeric invariants. `MakeBvWordValue` checks the input directly and reports
  `kInvalidLiteral` for a word that exceeds the width.
- `GetInteger()` serves Int literals only. `GetBitVector()` serves BV
  literals and includes their width. `GetFloatingPoint()` serves FP literals
  and includes their format and canonical category, with exact packed bits
  for finite nonzero values. Querying another literal category returns
  `std::nullopt`. `GetFpFormat()` returns both widths for an FP sort and
  `std::nullopt` for other sorts. Accessors on views require a live owner.
- Construct FP values with `numeric::FloatingPointValue::FromBits` or
  `numeric::FloatingPointValue::Special` and pass the view to `MakeFpValue`.
  `GetFloatingPoint()` provides the category and any finite bits.
  `GetFpFormat()` provides the format widths. NaNs intern once per format,
  signed zeros and signed infinities remain distinct, and finite bits retain
  their exact value. Packed bit classification belongs to util.
- The typed and word BV paths select the same canonical key for a matching
  sort and magnitude. Node copies the canonical numeric content on a miss.
  Borrowed results allocate no storage and take no references. A wide result
  borrows directly from the published node and requires an owner of that node
  to remain live. Destroying or changing the original util input after the
  call has no effect on the node. Rational parameters remain exact, normalized
  `to_fp` payloads, and `fp` and `to_fp` applications remain operation nodes.

### Phase 1C error translation

`dandelion.node` defines and exports `ErrorCode`, `ResourceKind`,
`DatatypeReason`, `NodeError`, `Result<T>`, and `ReferenceLimit`.
`dandelion.util.numeric` owns `numeric::NumericError` and
`numeric::NumericResult<T>`. When a util operation inside a checked factory
returns an error, NodeManager translates it before returning a `Result<T>`.

| Util failure | Node result |
| --- | --- |
| `kInvalidText`, `kInvalidArgument`, `kValueOutOfRange` | `kInvalidLiteral` for a literal payload or `kInvalidParameter` for an operator or sort parameter. |
| `kDivisionByZero` | `kInvalidParameter` when preparing an exact numeric parameter requires a nonzero denominator. |
| `kSizeLimit` | `kResourceLimit/kSize`. |
| `kCppAllocation` | `kResourceLimit/kCppAllocation`. |

Node fills an operand or parameter index only when the failing input has a
known position in the Node call. Other positions stay `UINT64_MAX` and the
datatype reason stays `kNone`. Manager and sort mismatches are determined by
Node validation, and util results returned directly to other callers keep
their numeric error type. Checked numeric division concerns concrete
arithmetic. Symbolic Int `div` and `mod` remain valid with a zero divisor.
Factory failures preserve the existing publication and ownership guarantees.
Checked reference limits and actual GMP allocator termination follow the
policies in [Errors and factory guarantees](#7-errors-and-factory-guarantees).

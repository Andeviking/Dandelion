# Expression Nodes and Node Manager

Status: **implemented first-version contracts**. This document
supersedes the earlier node design baseline. The companion
[operator specification](operators.md) and [implementation plan](implementation.md)
fix the initial representation and behavior. [Node testing](../testing/node.md)
sets the test-first acceptance requirements.

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

The selected frontend design retains a closed application of an internal
declaration whose body and formal identities are held in an owning definition
table. That table and the ManagerRef are retained by a sealed query context
and accompany every query or model using the application; the declaration
must not be treated as unconstrained UF. Model-local abstract values can also
use fresh internal constant nodes, distinguished by a model-owned binding
table. Source scope and `:named` closedness use frontend occurrence records;
the frontend does not expand calls or retain a second graph to enforce
catalog fragment shapes. The frontend supplies the environment needed to
interpret a definition call or a model-local proxy.
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

A `Rational` is a normalized numerator and positive denominator with gcd one;
zero is `0/1`. A zero denominator is invalid at the numeric API. The
frontend's rational-to-FP compatibility grammar is specified in
[operators.md](operators.md#6-floating-point).

A BV value is `(width, nonnegative integer)` with `value < 2^width`; creation
checks the bound and never truncates. A valid `(_ bvN w)` input has the same
bound. The `int_to_bv` operation instead has its specified modulo semantics.
Test the bound using sign and bit length rather than allocating `2^width`.

An explicit FP value is its FP sort and `NaN`, positive or negative infinity,
positive or negative zero, or `FiniteBits` containing exact nonzero finite
interchange bits. NaN bit encodings normalize to one abstract NaN per FP sort;
signed zeros remain different under core `=`. `fp` and `to_fp` operation nodes
are retained even for constant children. They may denote the same value as a
literal without having the same structural identity. Rounding modes are RNE,
RNA, RTP, RTN, and RTZ. MPFR is not required by this expression representation.

For `MakeFpBits(F(e,s), bits)`, require nonnegative `bits` with bit length at
most `e+s`. The high bit is sign, the next `e` bits are exponent, and the low
`s-1` bits are fraction. An all-one exponent and nonzero fraction is NaN; an
all-one exponent and zero fraction is signed infinity; an all-zero exponent
and zero fraction is signed zero. Every remaining pattern is finite nonzero
`FiniteBits`, including subnormals. Inspect fields without materializing a
huge all-ones constant merely to recognize a special value. `MakeFpSpecial`
selects the corresponding canonical category directly.

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

The following establishes the first-version API shape; normal special members,
private representation, and range boilerplate are omitted. Returned owning
objects hold their references. Factory inputs are borrowed for the call and
spans, names, and temporary views are not retained as payload.

```cpp
class Integer {
 public:
  explicit Integer(std::int64_t value) noexcept;
  static Result<Integer> ParseDecimal(std::string_view text);
  IntegerView Borrow() const & noexcept;
  IntegerView Borrow() const && = delete;
  std::string ToDecimal() const;
};

class Rational {
 public:
  static Result<Rational> Make(IntegerView numerator,
                               IntegerView denominator);
  RationalView Borrow() const & noexcept;
  RationalView Borrow() const && = delete;
};

struct BuiltinOpView {
  Kind kind;
  std::span<const Integer> indices;
  std::optional<SortView> qualifier;
  std::optional<RationalView> rational;
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
  Result<Sort> MakeBvSort(IntegerView width);
  Result<Sort> MakeFpSort(IntegerView exponent, IntegerView significand);
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
  Result<Node> MakeInt(IntegerView value);
  Result<Node> MakeBvValue(SortView sort, IntegerView value);
  Result<Node> MakeFpBits(SortView sort, IntegerView packed_bits);
  Result<Node> MakeFpSpecial(SortView sort, FpSpecial value);
  Result<Node> MakeRoundingMode(RoundingMode value);
  Result<Node> MakeFreshConstant(std::string_view name_hint, SortView sort);
  Result<Sort> CheckBuiltin(BuiltinOpView op,
                            std::span<const NodeView> children);
  Result<Node> MakeBuiltin(BuiltinOpView op,
                            std::span<const NodeView> children);
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
same mathematical value. Invalid text is `kInvalidLiteral`. `ToDecimal`
returns a canonical signed decimal string and can throw C++ allocation failure.
`Rational::Make` rejects denominator zero as `kInvalidParameter` and copies
and normalizes both values. These numeric APIs do not promise recovery from
GMP limb OOM.

`CheckBuiltin` and `MakeBuiltin` use the same operator rules. The former may
intern a valid result sort but does not create a node. `MakeFpBits` and
`MakeBvValue` require a nonnegative value fitting the target format. Fresh
declarations never intern by name. Source namespace and redeclaration checks
belong to the frontend.

The `Retain*` operations check nonnull and same-state views and return new
owning handles, translating count exhaustion to `kResourceLimit`. A borrowed
view's owner must remain alive throughout the call; these operations cannot
recover a reclaimed object from a stale pointer or raw ID.

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
can format it with `ToDecimal()`. To pass them to a factory, copy the values
that need to be materialized as `Integer` objects.
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
optional queries return no value. Read-only numeric magnitude chunks are in
least-significant-first order and never expose GMP storage. A wide numeric
view requires its backing value to stay alive and unchanged. Replacing, moving
from, or destroying the owning numeric value may invalidate the view.

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

export module dandelion.node;
export import dandelion.numeric;
import std;

namespace dandelion::detail {
struct ManagerState;
struct NodeData;
struct BuilderData;
struct WalkerData;
struct Access;
void RetainState(ManagerState* state);
void ReleaseState(ManagerState* state) noexcept;
}  // namespace dandelion::detail

export namespace dandelion {
using NodeId = std::uint64_t;
using SortId = std::uint64_t;
using SortConstructorId = std::uint64_t;
using DeclId = std::uint64_t;
using DeclInstanceId = std::uint64_t;
using DatatypeGroupId = std::uint64_t;

enum class Kind : std::uint16_t {
  kInvalid = 0,
  kBoolValue,
  kIntValue,
  kBvValue,
  kFpValue,
  kRoundingModeValue,
  kApply,
  kNot,
  kAnd,
  kOr,
  kXor,
  kImplies,
  kEqual,
  kDistinct,
  kIte,
  kIntNeg,
  kIntAdd,
  kIntSub,
  kIntMul,
  kIntDiv,
  kIntMod,
  kIntAbs,
  kIntPow,
  kIntLt,
  kIntLe,
  kIntGt,
  kIntGe,
  kIntDivisible,
  kBvNot,
  kBvNeg,
  kBvAnd,
  kBvOr,
  kBvXor,
  kBvAdd,
  kBvMul,
  kBvNand,
  kBvNor,
  kBvXnor,
  kBvSub,
  kBvUdiv,
  kBvUrem,
  kBvSdiv,
  kBvSrem,
  kBvSmod,
  kBvShl,
  kBvLshr,
  kBvAshr,
  kBvComp,
  kBvUlt,
  kBvUle,
  kBvUgt,
  kBvUge,
  kBvSlt,
  kBvSle,
  kBvSgt,
  kBvSge,
  kBvNego,
  kBvUaddo,
  kBvSaddo,
  kBvUmulo,
  kBvSmulo,
  kBvUsubo,
  kBvSsubo,
  kBvSdivo,
  kBvConcat,
  kBvExtract,
  kBvRepeat,
  kBvZeroExtend,
  kBvSignExtend,
  kBvRotateLeft,
  kBvRotateRight,
  kUbvToInt,
  kSbvToInt,
  kIntToBv,
  kFpConstruct,
  kFpAbs,
  kFpNeg,
  kFpAdd,
  kFpSub,
  kFpMul,
  kFpDiv,
  kFpFma,
  kFpSqrt,
  kFpRoundToIntegral,
  kFpRem,
  kFpMin,
  kFpMax,
  kFpEq,
  kFpLt,
  kFpLeq,
  kFpGt,
  kFpGeq,
  kFpIsNormal,
  kFpIsSubnormal,
  kFpIsZero,
  kFpIsInfinite,
  kFpIsNaN,
  kFpIsNegative,
  kFpIsPositive,
  kFpFromBits,
  kFpConvert,
  kFpFromSbv,
  kFpFromUbv,
  kFpFromRational,
  kFpToSbv,
  kFpToUbv,
  kArraySelect,
  kArrayStore,
  kArrayConst,
};
static_assert(std::to_underlying(Kind::kArrayConst) == 108);
enum class SortKind : std::uint8_t {
  kBool,
  kInt,
  kRoundingMode,
  kBitVector,
  kFloatingPoint,
  kArray,
  kUninterpreted,
  kDatatype,
};
enum class DeclKind : std::uint8_t {
  kFunction,
  kInternalFunction,
  kFormal,
  kDatatypeConstructor,
  kDatatypeSelector,
  kDatatypeTester,
};
enum class SortRecipeKind : std::uint8_t {
  kConcrete,
  kParameter,
  kArray,
  kApply,
  kRecursive,
};
enum class FpSpecial : std::uint8_t {
  kNaN,
  kPositiveInfinity,
  kNegativeInfinity,
  kPositiveZero,
  kNegativeZero,
};
enum class FpCategory : std::uint8_t {
  kNaN,
  kPositiveInfinity,
  kNegativeInfinity,
  kPositiveZero,
  kNegativeZero,
  kFiniteBits,
};

class SortView;
class SortConstructorView;
class DeclView;
class DeclInstanceView;
class DatatypeGroupView;
class SortRecipeView;
class NodeView;
class NodeManager;
#ifdef DANDELION_NODE_TESTING
class NodeTestPeer;
#endif

template <class View>
class StateHandle {
 public:
  StateHandle() noexcept = default;
  StateHandle(const StateHandle& other) : state_(other.state_), id_(other.id_) {
    if (state_) detail::RetainState(state_);
  }
  StateHandle& operator=(const StateHandle& other) {
    if (this != &other) {
      if (other.state_) detail::RetainState(other.state_);
      if (state_) detail::ReleaseState(state_);
      state_ = other.state_;
      id_ = other.id_;
    }
    return *this;
  }
  StateHandle(StateHandle&& other) noexcept
      : state_(std::exchange(other.state_, nullptr)),
        id_(std::exchange(other.id_, 0)) {}
  StateHandle& operator=(StateHandle&& other) noexcept {
    if (this != &other) {
      if (state_) detail::ReleaseState(state_);
      state_ = std::exchange(other.state_, nullptr);
      id_ = std::exchange(other.id_, 0);
    }
    return *this;
  }
  ~StateHandle() noexcept {
    if (state_) detail::ReleaseState(state_);
  }
  View Borrow() const& noexcept { return View(state_, id_); }
  View Borrow() const&& = delete;
  std::uint64_t GetId() const noexcept { return id_; }
  friend bool operator==(const StateHandle& left,
                         const StateHandle& right) noexcept {
    return left.state_ == right.state_ && left.id_ == right.id_;
  }

 private:
  friend struct detail::Access;
#ifdef DANDELION_NODE_TESTING
  friend class NodeTestPeer;
#endif
  StateHandle(detail::ManagerState* state, std::uint64_t id) noexcept
      : state_(state), id_(id) {}
  detail::ManagerState* state_ = nullptr;
  std::uint64_t id_ = 0;
};
using Sort = StateHandle<SortView>;
using SortConstructor = StateHandle<SortConstructorView>;
using Decl = StateHandle<DeclView>;
using DeclInstance = StateHandle<DeclInstanceView>;
using DatatypeGroup = StateHandle<DatatypeGroupView>;

class ManagerRef {
 public:
  ManagerRef() noexcept = default;
  ManagerRef(const ManagerRef& other);
  ManagerRef& operator=(const ManagerRef& other);
  ManagerRef(ManagerRef&& other) noexcept;
  ManagerRef& operator=(ManagerRef&& other) noexcept;
  ~ManagerRef() noexcept;
  bool IsNull() const noexcept;

 private:
  friend class NodeManager;
  friend struct detail::Access;
  explicit ManagerRef(detail::ManagerState* state) noexcept;
  detail::ManagerState* state_ = nullptr;
};

class SortRange {
 public:
  class Iterator {
   public:
    SortView operator*() const noexcept;
    Iterator& operator++() noexcept;
    friend bool operator==(const Iterator& left,
                           const Iterator& right) noexcept;

   private:
    friend class SortRange;
    Iterator(detail::ManagerState* state, const SortId* current) noexcept;
    detail::ManagerState* state_;
    const SortId* current_;
  };
  // NOLINTNEXTLINE(readability-identifier-naming): Standard range interface.
  std::size_t size() const noexcept;
  // NOLINTNEXTLINE(readability-identifier-naming): Standard range interface.
  bool empty() const noexcept;
  // Requires index < size().
  SortView operator[](std::size_t index) const noexcept;
  // NOLINTNEXTLINE(readability-identifier-naming): Standard range interface.
  Iterator begin() const noexcept;
  // NOLINTNEXTLINE(readability-identifier-naming): Standard range interface.
  Iterator end() const noexcept;

 private:
  friend struct detail::Access;
  SortRange(detail::ManagerState* state, std::span<const SortId> ids) noexcept;
  detail::ManagerState* state_;
  std::span<const SortId> ids_;
};
class SortView {
 public:
  SortView() noexcept = default;
  bool IsNull() const noexcept { return state_ == nullptr; }
  std::uint64_t GetId() const noexcept { return id_; }
  bool SameManager(SortView other) const noexcept {
    return state_ && state_ == other.state_;
  }
  SortKind GetKind() const noexcept;
  std::optional<std::uint32_t> GetWidth() const noexcept;
  std::optional<std::uint32_t> GetFpExponentWidth() const noexcept;
  std::optional<std::uint32_t> GetFpSignificandWidth() const noexcept;
  std::optional<SortView> GetArrayIndex() const noexcept;
  std::optional<SortView> GetArrayElement() const noexcept;
  std::optional<SortConstructorView> GetSortConstructor() const noexcept;
  SortRange GetArguments() const noexcept;
  friend bool operator==(const SortView& left, const SortView& right) noexcept;

 private:
  template <class>
  friend class StateHandle;
  friend struct detail::Access;
  SortView(detail::ManagerState* state, std::uint64_t id) noexcept
      : state_(state), id_(id) {}
  detail::ManagerState* state_ = nullptr;
  std::uint64_t id_ = 0;
};
class SortConstructorView {
 public:
  SortConstructorView() noexcept = default;
  bool IsNull() const noexcept { return state_ == nullptr; }
  std::uint64_t GetId() const noexcept { return id_; }
  bool SameManager(SortConstructorView other) const noexcept {
    return state_ && state_ == other.state_;
  }
  std::uint64_t GetArity() const noexcept;
  std::string_view GetNameHint() const noexcept;
  bool IsDatatype() const noexcept;
  std::optional<DatatypeGroupView> GetDatatypeGroup() const noexcept;
  friend bool operator==(const SortConstructorView& left,
                         const SortConstructorView& right) noexcept;

 private:
  template <class>
  friend class StateHandle;
  friend struct detail::Access;
  SortConstructorView(detail::ManagerState* state, std::uint64_t id) noexcept
      : state_(state), id_(id) {}
  detail::ManagerState* state_ = nullptr;
  std::uint64_t id_ = 0;
};
class DeclView {
 public:
  DeclView() noexcept = default;
  bool IsNull() const noexcept { return state_ == nullptr; }
  std::uint64_t GetId() const noexcept { return id_; }
  bool SameManager(DeclView other) const noexcept {
    return state_ && state_ == other.state_;
  }
  DeclKind GetKind() const noexcept;
  std::string_view GetNameHint() const noexcept;
  std::uint64_t GetParameterCount() const noexcept;
  // Datatype domains require a recipe query, including when a tester
  // exposes its concrete Bool range through GetRange().
  SortRange GetDomain() const noexcept;
  std::optional<SortView> GetRange() const noexcept;
  Result<SortRecipeView> GetDomainRecipe(std::size_t index) const noexcept;
  std::optional<SortRecipeView> GetRangeRecipe() const noexcept;
  std::optional<DeclView> GetConstructor() const noexcept;
  std::optional<std::uint64_t> GetFieldOrdinal() const noexcept;
  friend bool operator==(const DeclView& left, const DeclView& right) noexcept;

 private:
  template <class>
  friend class StateHandle;
  friend struct detail::Access;
  DeclView(detail::ManagerState* state, std::uint64_t id) noexcept
      : state_(state), id_(id) {}
  detail::ManagerState* state_ = nullptr;
  std::uint64_t id_ = 0;
};
class DeclInstanceView {
 public:
  DeclInstanceView() noexcept = default;
  bool IsNull() const noexcept { return state_ == nullptr; }
  std::uint64_t GetId() const noexcept { return id_; }
  bool SameManager(DeclInstanceView other) const noexcept {
    return state_ && state_ == other.state_;
  }
  DeclView GetDeclaration() const noexcept;
  SortRange GetTypeArguments() const noexcept;
  SortRange GetDomain() const noexcept;
  SortView GetRange() const noexcept;
  friend bool operator==(const DeclInstanceView& left,
                         const DeclInstanceView& right) noexcept;

 private:
  template <class>
  friend class StateHandle;
  friend struct detail::Access;
  DeclInstanceView(detail::ManagerState* state, std::uint64_t id) noexcept
      : state_(state), id_(id) {}
  detail::ManagerState* state_ = nullptr;
  std::uint64_t id_ = 0;
};
class DatatypeGroupView {
 public:
  DatatypeGroupView() noexcept = default;
  bool IsNull() const noexcept { return state_ == nullptr; }
  std::uint64_t GetId() const noexcept { return id_; }
  bool SameManager(DatatypeGroupView other) const noexcept {
    return state_ && state_ == other.state_;
  }
  std::uint64_t GetDatatypeCount() const noexcept;
  Result<std::uint64_t> GetConstructorCount(
      std::size_t datatype) const noexcept;
  Result<std::uint64_t> GetFieldCount(std::size_t datatype,
                                      std::size_t constructor) const noexcept;
  Result<SortConstructorView> GetSortConstructor(
      std::size_t datatype) const noexcept;
  Result<DeclView> GetConstructor(std::size_t datatype,
                                  std::size_t constructor) const noexcept;
  Result<DeclView> GetSelector(std::size_t datatype, std::size_t constructor,
                               std::size_t field) const noexcept;
  Result<DeclView> GetTester(std::size_t datatype,
                             std::size_t constructor) const noexcept;
  friend bool operator==(const DatatypeGroupView& left,
                         const DatatypeGroupView& right) noexcept;

 private:
  template <class>
  friend class StateHandle;
  friend struct detail::Access;
  DatatypeGroupView(detail::ManagerState* state, std::uint64_t id) noexcept
      : state_(state), id_(id) {}
  detail::ManagerState* state_ = nullptr;
  std::uint64_t id_ = 0;
};

class SortRecipeView {
 public:
  // Identity within the owning group and manager, like other borrowed data.
  std::uint64_t GetId() const noexcept { return index_; }
  SortRecipeKind GetKind() const noexcept;
  std::optional<SortView> GetConcrete() const noexcept;
  std::optional<std::pair<std::uint64_t, std::uint64_t>> GetParameter()
      const noexcept;
  std::optional<SortConstructorView> GetConstructor() const noexcept;
  std::optional<std::uint64_t> GetRecursiveSlot() const noexcept;
  std::size_t GetArgumentCount() const noexcept;
  Result<SortRecipeView> Argument(std::size_t index) const noexcept;

 private:
  friend struct detail::Access;
  SortRecipeView(detail::ManagerState* state, DatatypeGroupId group,
                 std::uint64_t index) noexcept;
  detail::ManagerState* state_;
  DatatypeGroupId group_;
  std::uint64_t index_;
};

class LiteralView {
 public:
  std::optional<bool> GetBool() const noexcept;
  std::optional<IntegerView> GetInteger() const noexcept;
  std::optional<FpCategory> GetFpCategory() const noexcept;
  std::optional<IntegerView> GetFpBits() const noexcept;
  std::optional<RoundingMode> GetRoundingMode() const noexcept;

 private:
  friend class NodeView;
  explicit LiteralView(const detail::NodeData* node) noexcept;
  const detail::NodeData* node_;
};

class NodeChildren {
 public:
  class Iterator {
   public:
    NodeView operator*() const noexcept;
    Iterator& operator++() noexcept;
    friend bool operator==(const Iterator& left,
                           const Iterator& right) noexcept;

   private:
    friend class NodeChildren;
    explicit Iterator(detail::NodeData* const* current) noexcept;
    detail::NodeData* const* current_;
  };
  // NOLINTNEXTLINE(readability-identifier-naming): Standard range interface.
  std::size_t size() const noexcept;
  // NOLINTNEXTLINE(readability-identifier-naming): Standard range interface.
  bool empty() const noexcept;
  // Requires index < size().
  NodeView operator[](std::size_t index) const noexcept;
  // NOLINTNEXTLINE(readability-identifier-naming): Standard range interface.
  Iterator begin() const noexcept;
  // NOLINTNEXTLINE(readability-identifier-naming): Standard range interface.
  Iterator end() const noexcept;

 private:
  friend class NodeView;
  explicit NodeChildren(std::span<detail::NodeData* const> nodes) noexcept;
  std::span<detail::NodeData* const> nodes_;
};

class NodeIndices {
 public:
  class Index {
   public:
    IntegerView Borrow() const noexcept { return view_; }
    std::string ToDecimal() const { return view_.ToDecimal(); }

   private:
    friend class NodeIndices;
    explicit Index(IntegerView view) noexcept : view_(view) {}
    IntegerView view_;
  };
  class Iterator {
   public:
    Index operator*() const noexcept;
    Iterator& operator++() noexcept;
    friend bool operator==(const Iterator& left,
                           const Iterator& right) noexcept;

   private:
    friend class NodeIndices;
    Iterator(const detail::NodeData* node, std::size_t index) noexcept;
    const detail::NodeData* node_;
    std::size_t index_;
  };
  // NOLINTNEXTLINE(readability-identifier-naming): Standard range interface.
  std::size_t size() const noexcept;
  // NOLINTNEXTLINE(readability-identifier-naming): Standard range interface.
  bool empty() const noexcept;
  // Requires index < size().
  Index operator[](std::size_t index) const noexcept;
  // NOLINTNEXTLINE(readability-identifier-naming): Standard range interface.
  Iterator begin() const noexcept;
  // NOLINTNEXTLINE(readability-identifier-naming): Standard range interface.
  Iterator end() const noexcept;

 private:
  friend class NodeView;
  explicit NodeIndices(const detail::NodeData* node) noexcept : node_(node) {}
  const detail::NodeData* node_;
};

class NodeView {
 public:
  NodeView() noexcept = default;
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
  friend bool operator==(NodeView left, NodeView right) noexcept;

 private:
  friend class Node;
  friend struct detail::Access;
  explicit NodeView(detail::NodeData* node) noexcept;
  detail::NodeData* node_ = nullptr;
};

class Node {
 public:
  Node() noexcept = default;
  Node(const Node& other);
  Node& operator=(const Node& other);
  Node(Node&& other) noexcept;
  Node& operator=(Node&& other) noexcept;
  ~Node() noexcept;
  NodeView Borrow() const& noexcept;
  NodeView Borrow() const&& = delete;
  NodeId GetId() const noexcept;
  Sort GetSort() const;
  Result<Node> Child(std::size_t index) const;
  friend bool operator==(const Node& left, const Node& right) noexcept;

 private:
  friend struct detail::Access;
#ifdef DANDELION_NODE_TESTING
  friend class NodeTestPeer;
#endif
  explicit Node(detail::NodeData* node) noexcept;
  detail::NodeData* node_ = nullptr;
};
struct NodeHash {
  std::size_t operator()(const Node& node) const noexcept;
};

struct BuiltinOpView {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  Kind kind;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::span<const Integer> indices;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::optional<SortView> qualifier;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::optional<RationalView> rational;
};

// A token is usable only with the open builder that created it. Moving
// the builder preserves its tokens; commit, abort, and destruction end them.
class SortExpr {
 public:
  SortExpr() noexcept = default;

 private:
  friend class DatatypeBuilder;
  friend struct detail::Access;
  SortExpr(detail::BuilderData* builder, std::uint64_t index) noexcept;
  detail::BuilderData* builder_ = nullptr;
  std::uint64_t index_ = 0;
};
struct DatatypeHeader {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::string_view name_hint;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t parameter_count;
};
struct DatatypeFieldSpec {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::string_view name_hint;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  SortExpr sort;
};

class DatatypeBuilder {
 public:
  DatatypeBuilder() noexcept = default;
  DatatypeBuilder(const DatatypeBuilder&) = delete;
  DatatypeBuilder& operator=(const DatatypeBuilder&) = delete;
  DatatypeBuilder(DatatypeBuilder&& other) noexcept;
  DatatypeBuilder& operator=(DatatypeBuilder&& other) noexcept;
  ~DatatypeBuilder() noexcept;
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

 private:
  friend class NodeManager;
  explicit DatatypeBuilder(detail::BuilderData* data) noexcept;
  detail::BuilderData* data_ = nullptr;
};

// Requested backing storage for published objects and owned arrays. Index
// containers report entries and buckets separately from metadata_bytes.
struct NodeStorageStats {
  // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
  std::size_t live_nodes = 0;
  std::size_t live_edges = 0;
  std::size_t node_bytes = 0;
  std::size_t numeric_backing_bytes = 0;
  std::size_t metadata_bytes = 0;
  std::size_t table_bytes = 0;
  std::size_t peak_table_bytes = 0;
  std::size_t declarations = 0;
  std::size_t instances = 0;
  std::size_t sort_index_entries = 0;
  std::size_t instance_index_entries = 0;
  std::size_t metadata_index_buckets = 0;
  // NOLINTEND(readability-identifier-naming)
};

class NodeManager {
 public:
  NodeManager() noexcept = default;
  NodeManager(const NodeManager&) = delete;
  NodeManager& operator=(const NodeManager&) = delete;
  NodeManager(NodeManager&&) noexcept = default;
  NodeManager& operator=(NodeManager&&) noexcept = default;
  static Result<NodeManager> Create();
  static Result<NodeManager> Attach(const ManagerRef& state);
  Result<NodeStorageStats> GetStorageStats() const noexcept;
  // Reclaims excess slots at a caller-selected maintenance boundary.
  Result<void> CompactNodeTable();
  Result<ManagerRef> RetainState() const;
  Result<Node> RetainNode(NodeView node);
  // Consumes these handles together. Null handles are allowed. A foreign
  // handle rejects the entire batch before any handle is consumed.
  Result<void> ReleaseRoots(std::span<Node> roots);
  Result<Sort> RetainSort(SortView sort);
  Result<SortConstructor> RetainSortConstructor(
      SortConstructorView constructor);
  Result<Decl> RetainDeclaration(DeclView declaration);
  Result<DeclInstance> RetainDeclInstance(DeclInstanceView instance);
  Result<DatatypeGroup> RetainDatatypeGroup(DatatypeGroupView group);
  Result<Sort> BoolSort();
  Result<Sort> IntSort();
  Result<Sort> RoundingModeSort();
  Result<Sort> MakeBvSort(IntegerView width);
  Result<Sort> MakeFpSort(IntegerView exponent, IntegerView significand);
  Result<Sort> MakeArraySort(SortView index, SortView element);
  Result<SortConstructor> DeclareUninterpretedSort(std::string_view name_hint,
                                                   std::uint64_t arity);
  Result<Sort> InstantiateSort(SortConstructorView constructor,
                               std::span<const SortView> arguments);
  Result<Decl> DeclareFunction(std::string_view name_hint,
                               std::span<const SortView> domain,
                               SortView range);
  Result<Decl> FreshFunction(std::string_view name_hint,
                             std::span<const SortView> domain, SortView range);
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

 private:
  friend struct detail::Access;
#ifdef DANDELION_NODE_TESTING
  friend class NodeTestPeer;
#endif
  ManagerRef state_;
};

enum class DagEventKind : std::uint8_t {
  kRootEnter,
  kRootExit,
  kNodeEnter,
  kNodeExit,
  kEdgeEnter,
  kEdgeExit,
};
struct DagEvent {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  DagEventKind kind;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  NodeView node;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  NodeView parent;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t index = std::numeric_limits<std::uint64_t>::max();
};
class DagWalker {
 public:
  DagWalker() noexcept = default;
  DagWalker(const DagWalker&) = delete;
  DagWalker& operator=(const DagWalker&) = delete;
  DagWalker(DagWalker&& other) noexcept;
  DagWalker& operator=(DagWalker&& other) noexcept;
  ~DagWalker() noexcept;
  static Result<DagWalker> Create(std::span<const Node> roots);
  Result<std::optional<DagEvent>> Next();
  Result<void> SkipChildren();

 private:
  detail::WalkerData* data_ = nullptr;
};

// Visits each reachable node once, after its children. The roots stay owned
// for the duration of the call. The first visitor error stops traversal.
Result<void> VisitDagPostorder(
    std::span<const Node> roots,
    const std::function<Result<void>(NodeView)>& visit);

#ifdef DANDELION_NODE_TESTING
struct NodeTestStats {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t sorts;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t sort_constructors;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t declarations;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t decl_instances;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t datatype_groups;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t live_nodes;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t table_capacity;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t next_node_id;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t node_allocations;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t node_reclamations;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t allocation_attempts;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t insertion_searches;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::array<std::uint64_t, 16> probe_histogram;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t probe_slots;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::size_t probe_max;
};
class NodeTestPeer {
 public:
  static void ForceCollisions(NodeManager& manager, std::uint64_t hash);
  static void FailCreationAllocationAfter(std::size_t successful) noexcept;
  static void ClearCreationFailure() noexcept;
  static NodeTestStats GetStats(const NodeManager& manager) noexcept;
  static std::uint64_t TypeEvaluations(const NodeManager& manager) noexcept;
  static std::uint64_t BuiltinShapeChecks(const NodeManager& manager) noexcept;
  static std::size_t DeclarationPayloadUsed(
      const NodeManager& manager) noexcept;
  static std::size_t NonemptyDeclarationDomains(
      const NodeManager& manager) noexcept;
  static void FailAllocationAfter(NodeManager& manager, std::size_t successful);
  static void FailRetainAfter(NodeManager& manager, std::size_t successful);
  static void ClearFailures(NodeManager& manager) noexcept;
  static void SetNextNodeId(NodeManager& manager, NodeId next);
  static void SetIdentityLimit(NodeManager& manager, std::uint64_t limit);
  static void SetReferenceLimit(NodeManager& manager, std::uint64_t limit);
  static void SetTableCapacityLimit(NodeManager& manager, std::size_t limit);
  static void SetSizeLimit(NodeManager& manager, std::size_t limit);
  static std::uint64_t RefCount(const Node& node) noexcept;
  static std::uint64_t StructuralHash(const Node& node) noexcept;
  static std::uint64_t StateRefCount(const NodeManager& manager) noexcept;
  static std::uint64_t DestroyedStates() noexcept;
};
#endif
}  // namespace dandelion

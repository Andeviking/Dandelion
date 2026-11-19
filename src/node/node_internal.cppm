module dandelion.node:internal;
import dandelion.node;
import std;

namespace dandelion::detail {

static_assert(std::numeric_limits<std::size_t>::digits <=
              std::numeric_limits<std::uint64_t>::digits);

struct Failure {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  NodeError error;
};
[[noreturn]] inline void Fail(
    ErrorCode code, ResourceKind resource = ResourceKind::kNone,
    std::uint64_t operand = std::numeric_limits<std::uint64_t>::max(),
    std::uint64_t parameter = std::numeric_limits<std::uint64_t>::max()) {
  throw Failure{NodeError{code, resource, operand, parameter}};
}
inline void Require(
    bool condition, ErrorCode code,
    std::uint64_t operand = std::numeric_limits<std::uint64_t>::max()) {
  if (!condition) Fail(code, ResourceKind::kNone, operand);
}
template <class T, class Function>
Result<T> Guard(Function&& function) {
  try {
    if constexpr (std::is_void_v<T>) {
      function();
      return {};
    } else
      return function();
  } catch (const Failure& failure) {
    return std::unexpected(failure.error);
  } catch (const ReferenceLimit&) {
    return std::unexpected(
        NodeError{ErrorCode::kResourceLimit, ResourceKind::kReferenceCount});
  } catch (const std::bad_alloc&) {
    return std::unexpected(
        NodeError{ErrorCode::kResourceLimit, ResourceKind::kCppAllocation});
  } catch (const std::length_error&) {
    return std::unexpected(
        NodeError{ErrorCode::kResourceLimit, ResourceKind::kSize});
  }
}
inline std::uint64_t Mix(std::uint64_t value) noexcept {
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}
std::uint64_t Append(std::uint64_t hash, std::uint64_t value) noexcept;

template <class T>
std::size_t VectorBytes(const std::vector<T>& values) noexcept {
  return values.capacity() * sizeof(T);
}
inline std::size_t StringBytes(const std::string& value) noexcept {
  const auto offset = reinterpret_cast<std::uintptr_t>(value.data()) -
                      reinterpret_cast<std::uintptr_t>(&value);
  return offset < sizeof(value) ? 0 : value.capacity() + 1;
}

struct SortKey {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  SortKind kind;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t first = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t second = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  SortConstructorId constructor = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::span<const SortId> arguments;
};
struct SortRecord {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  SortId id;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  SortKind kind;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t first;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t second;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  SortConstructorId constructor;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::vector<SortId> arguments;
  SortKey Key() const noexcept {
    return {kind, first, second, constructor, arguments};
  }
};
struct SortHash {
  // NOLINTNEXTLINE(readability-identifier-naming): STL lookup marker.
  using is_transparent = void;
  std::size_t operator()(SortKey key) const noexcept;
  std::size_t operator()(const SortRecord* record) const noexcept {
    return (*this)(record->Key());
  }
};
struct SortEqual {
  // NOLINTNEXTLINE(readability-identifier-naming): STL lookup marker.
  using is_transparent = void;
  bool operator()(SortKey a, SortKey b) const noexcept;
  bool operator()(const SortRecord* a, const SortRecord* b) const noexcept {
    return (*this)(a->Key(), b->Key());
  }
  bool operator()(SortKey a, const SortRecord* b) const noexcept {
    return (*this)(a, b->Key());
  }
  bool operator()(const SortRecord* a, SortKey b) const noexcept {
    return (*this)(a->Key(), b);
  }
};
struct SortConstructorRecord {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  SortConstructorId id;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::string name;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t arity;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  DatatypeGroupId group = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t slot = 0;
};
struct Recipe {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  SortRecipeKind kind;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t first = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t second = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::vector<std::uint64_t> arguments;
};
struct DatatypeDeclInfo {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  DatatypeGroupId group = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t parameter_count = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t field_ordinal = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  DeclId constructor = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::vector<std::uint64_t> domain_recipes;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t range_recipe = 0;
};
struct DeclRecord {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  DeclKind kind;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  SortId range;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::string_view name;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t domain = 0;
  // This cache does not change the published declaration or signature.
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  mutable DeclInstanceId single_instance = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::unique_ptr<DatatypeDeclInfo> datatype;
  std::uint64_t ParameterCount() const noexcept {
    return datatype ? datatype->parameter_count : 0;
  }
};
template <class T>
class DeclarationPayload {
 public:
  struct Mark {
    // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
    std::size_t blocks;
    // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
    std::size_t tail_used;
  };
  std::span<T> Append(ManagerState* state, std::size_t count);
  Mark Position() const noexcept {
    return {blocks_.size(), blocks_.empty() ? 0 : blocks_.back().used};
  }
  void Restore(Mark mark) noexcept {
    while (blocks_.size() > mark.blocks) blocks_.pop_back();
    if (!blocks_.empty()) blocks_.back().used = mark.tail_used;
  }
  std::size_t StorageBytes() const noexcept {
    auto bytes = VectorBytes(blocks_);
    for (const auto& block : blocks_) bytes += block.capacity * sizeof(T);
    return bytes;
  }
#ifdef DANDELION_NODE_TESTING
  std::size_t UsedBytes() const noexcept {
    std::size_t bytes = 0;
    for (const auto& block : blocks_) bytes += block.used * sizeof(T);
    return bytes;
  }
#endif

 private:
  struct Block {
    // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
    std::unique_ptr<T[]> storage;
    // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
    std::size_t capacity;
    // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
    std::size_t used;
  };
  std::vector<Block> blocks_;
};
struct InstanceEntry {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  DeclId declaration;
  // Zero selects the signature in the declaration.
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t substitution = 0;
};
struct ConcreteSignature {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::span<const SortId> domain;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  SortId range;
};
struct InstanceKey {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  DeclId declaration;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::span<const SortId> arguments;
};
struct InstanceRecord {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  DeclInstanceId id;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  DeclId declaration;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::vector<SortId> arguments;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::vector<SortId> domain;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  SortId range = 0;
  InstanceKey Key() const noexcept { return {declaration, arguments}; }
};
struct InstanceHash {
  // NOLINTNEXTLINE(readability-identifier-naming): STL lookup marker.
  using is_transparent = void;
  std::size_t operator()(InstanceKey key) const noexcept;
  std::size_t operator()(const InstanceRecord* record) const noexcept {
    return (*this)(record->Key());
  }
};
struct InstanceEqual {
  // NOLINTNEXTLINE(readability-identifier-naming): STL lookup marker.
  using is_transparent = void;
  bool operator()(InstanceKey a, InstanceKey b) const noexcept;
  bool operator()(const InstanceRecord* a,
                  const InstanceRecord* b) const noexcept {
    return (*this)(a->Key(), b->Key());
  }
  bool operator()(InstanceKey a, const InstanceRecord* b) const noexcept {
    return (*this)(a, b->Key());
  }
  bool operator()(const InstanceRecord* a, InstanceKey b) const noexcept {
    return (*this)(a->Key(), b);
  }
};
using InstanceSet =
    std::unordered_set<const InstanceRecord*, InstanceHash, InstanceEqual>;
struct ConstructorRecord {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  DeclId declaration;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  DeclId tester;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::vector<DeclId> selectors;
};
struct DatatypeRecord {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  SortConstructorId constructor;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::vector<ConstructorRecord> constructors;
};
struct GroupRecord {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  DatatypeGroupId id;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::vector<DatatypeRecord> datatypes;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::vector<Recipe> recipes;
};
struct StagedField {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::string name;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t recipe;
};
struct StagedConstructor {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::string name;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::vector<StagedField> fields;
};
struct StagedDatatype {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::string name;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t arity;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::vector<StagedConstructor> constructors;
};
struct BuilderData {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  ManagerState* state;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::vector<StagedDatatype> datatypes;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::vector<Recipe> recipes;
};

enum class PayloadTag : std::uint8_t {
  kNone,
  kBool,
  kInteger,
  kBvWord,
  kFpSpecial,
  kFpWord,
  kFpWide,
  kRoundingMode,
  kDeclaration,
  kIndex1,
  kIndexWide,
  kIndex2,
  kRationalConversion,
};
struct RationalPayload {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::array<std::uint32_t, 2> indices;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  Rational value;
};
struct NodeData {
  static constexpr auto kExternalChildCount =
      std::numeric_limits<std::uint16_t>::max();
  static constexpr std::uint8_t kRetiring = 0x80;
  static_assert(std::to_underlying(Kind::kArrayConst) < kRetiring);
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  ManagerState* state;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::byte* storage;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t id;
  union {
    // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
    std::uint64_t ref_count;
    // Only active after the last reference, during synchronous reclamation.
    // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
    NodeData* reclaim_next;
  };
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t structural_hash;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  SortId sort_id;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint16_t inline_child_count;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint8_t kind_code;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  PayloadTag payload_tag;
  inline Kind GetKind() const noexcept { return static_cast<Kind>(kind_code); }
  inline bool IsRetiring() const noexcept { return kind_code & kRetiring; }
  inline std::uint64_t ChildCount() const noexcept {
    if (inline_child_count != kExternalChildCount) return inline_child_count;
    return *std::launder(
        reinterpret_cast<const std::uint64_t*>(storage + sizeof(NodeData)));
  }
};
struct QueryKey {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  Kind kind;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  SortId input_sort;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::span<const NodeView> children;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  PayloadTag tag = PayloadTag::kNone;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  bool boolean = false;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  IntegerView integer = IntegerView(0);
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t word = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  FpCategory fp_category = FpCategory::kNaN;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  RoundingMode rounding_mode = RoundingMode::kRne;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  DeclInstanceId declaration = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::array<std::uint32_t, 2> indices{};
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::optional<RationalView> rational = std::nullopt;
};
std::span<NodeData* const> Children(const NodeData* node) noexcept;
const void* Payload(const NodeData* node) noexcept;
void* Payload(NodeData* node) noexcept;
template <class T>
T& Value(NodeData* node) noexcept {
  return *std::launder(static_cast<T*>(Payload(node)));
}
template <class T>
const T& Value(const NodeData* node) noexcept {
  return *std::launder(static_cast<const T*>(Payload(node)));
}
void DestroyPayload(NodeData* node) noexcept;
std::uint64_t HashKey(const QueryKey& key) noexcept;
bool EqualKey(const NodeData* node, const QueryKey& key) noexcept;

class UniqueTable {
 public:
  struct ProbeResult {
    // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
    NodeData* match;
    // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
    std::size_t slot;
  };
  UniqueTable();
  ProbeResult Probe(const QueryKey& key, std::uint64_t hash) noexcept;
  ProbeResult ProbePlain(Kind kind, std::span<const NodeView> children,
                         std::uint64_t hash) noexcept;
  bool EnsureSpace(ManagerState* state);
  std::size_t FindVacant(std::uint64_t hash) noexcept;
  void Insert(std::size_t slot, NodeData* node) noexcept;
  void Erase(NodeData* node) noexcept;
  std::vector<NodeData*> EraseRetired(ManagerState* state, NodeData* retired,
                                      std::size_t count) noexcept;
  void ReleaseEmpty() noexcept;
  void Compact(ManagerState* state);
  std::size_t Capacity() const noexcept { return slots_.size(); }
  inline std::size_t Size() const noexcept { return size_; }
  void MeasureStorage(NodeStorageStats& stats) const noexcept;
#ifdef DANDELION_NODE_TESTING
  void RecordProbe(std::size_t count) noexcept;
  std::uint64_t InsertionSearches() const noexcept {
    return insertion_searches_;
  }
  const auto& ProbeHistogram() const noexcept { return probe_histogram_; }
  std::uint64_t ProbeSlots() const noexcept { return probe_slots_; }
  std::size_t ProbeMax() const noexcept { return probe_max_; }
#endif

 private:
  template <class Matches>
  ProbeResult ProbeMatching(std::uint64_t hash, Matches matches) noexcept;
  std::size_t CompactedCapacity(std::size_t survivors) const noexcept;
  std::vector<NodeData*> Rehash(ManagerState* state, std::size_t capacity,
                                bool skip_retired = false);
  static constexpr std::size_t kInitialCapacity = 16;
  std::vector<NodeData*> slots_;
  std::size_t size_ = 0;
  std::size_t peak_capacity_ = kInitialCapacity;
#ifdef DANDELION_NODE_TESTING
  std::uint64_t insertion_searches_ = 0;
  std::array<std::uint64_t, 16> probe_histogram_{};
  std::uint64_t probe_slots_ = 0;
  std::size_t probe_max_ = 0;
#endif
};

constexpr auto kScalarSortCount =
    std::to_underlying(SortKind::kRoundingMode) + 1;

struct ManagerState {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t references = 1;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::vector<std::unique_ptr<const SortRecord>> sorts;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::array<SortId, kScalarSortCount> scalar_sorts{};
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::unordered_set<const SortRecord*, SortHash, SortEqual> sort_index;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::vector<std::unique_ptr<const SortConstructorRecord>> sort_constructors;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::vector<DeclRecord> declarations;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  DeclarationPayload<char> declaration_names;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  DeclarationPayload<SortId> declaration_domains;
  // Indexed by the nonzero domain IDs in declaration records.
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::vector<std::span<const SortId>> declaration_domain_spans;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::vector<InstanceEntry> instances;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::vector<std::unique_ptr<const InstanceRecord>> substituted_signatures;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  InstanceSet instance_index;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::vector<std::unique_ptr<const GroupRecord>> groups;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  UniqueTable table;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  NodeId next_node_id = 1;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  bool node_ids_exhausted = false;
#ifdef DANDELION_NODE_TESTING
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::optional<std::uint64_t> collision_hash;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::optional<std::size_t> allocation_failure;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::optional<std::size_t> retain_failure;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t identity_limit = std::numeric_limits<std::uint64_t>::max();
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t reference_limit = std::numeric_limits<std::uint64_t>::max();
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::size_t table_limit = std::numeric_limits<std::size_t>::max();
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::size_t size_limit = std::numeric_limits<std::size_t>::max();
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t allocations = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t reclamations = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t allocation_attempts = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t type_evaluations = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t builtin_shape_checks = 0;
#endif
  ~ManagerState() noexcept;
};

class DeclarationTransaction {
 public:
  explicit DeclarationTransaction(ManagerState* state) noexcept
      : state_(state),
        names_(state->declaration_names.Position()),
        domains_(state->declaration_domains.Position()) {}
  DeclarationTransaction(const DeclarationTransaction&) = delete;
  DeclarationTransaction& operator=(const DeclarationTransaction&) = delete;
  ~DeclarationTransaction() {
    if (state_) {
      state_->declaration_names.Restore(names_);
      state_->declaration_domains.Restore(domains_);
    }
  }
  void Commit() noexcept { state_ = nullptr; }

 private:
  ManagerState* state_;
  DeclarationPayload<char>::Mark names_;
  DeclarationPayload<SortId>::Mark domains_;
};
#ifdef DANDELION_NODE_TESTING
extern std::atomic<std::uint64_t> destroyed_states;
extern thread_local std::optional<std::size_t> creation_failure;
inline void CreationAllocationCheckpoint() {
  if (creation_failure) {
    if (*creation_failure == 0) {
      creation_failure.reset();
      throw std::bad_alloc{};
    }
    --*creation_failure;
  }
}
#endif

inline void AllocationCheckpoint(ManagerState* state) {
#ifdef DANDELION_NODE_TESTING
  if (!state) return;
  ++state->allocation_attempts;
  if (state->allocation_failure) {
    if (*state->allocation_failure == 0) {
      state->allocation_failure.reset();
      throw std::bad_alloc{};
    }
    --*state->allocation_failure;
  }
#else
  (void)state;
#endif
}
inline void RetainCheckpoint(ManagerState* state) {
#ifdef DANDELION_NODE_TESTING
  if (state->retain_failure) {
    if (*state->retain_failure == 0) {
      state->retain_failure.reset();
      throw ReferenceLimit{};
    }
    --*state->retain_failure;
  }
#else
  (void)state;
#endif
}
inline std::uint64_t ReferenceCapacity(const ManagerState* state) noexcept {
#ifdef DANDELION_NODE_TESTING
  return state->reference_limit;
#else
  (void)state;
  return std::numeric_limits<std::uint64_t>::max();
#endif
}
inline std::uint64_t IdentityCapacity(const ManagerState* state) noexcept {
#ifdef DANDELION_NODE_TESTING
  return state->identity_limit;
#else
  (void)state;
  return std::numeric_limits<std::uint64_t>::max();
#endif
}
inline std::size_t SizeCapacity(const ManagerState* state) noexcept {
#ifdef DANDELION_NODE_TESTING
  return state->size_limit;
#else
  (void)state;
  return std::numeric_limits<std::size_t>::max();
#endif
}
inline std::size_t TableCapacity(const ManagerState* state) noexcept {
#ifdef DANDELION_NODE_TESTING
  return state->table_limit;
#else
  (void)state;
  return std::numeric_limits<std::size_t>::max();
#endif
}
void RetainNodeData(NodeData* node);
void ReleaseNodeData(NodeData* node) noexcept;
void ReleaseRootBatch(ManagerState* state, std::span<Node> roots) noexcept;
void ReleaseRootBatch(ManagerState* state,
                      std::span<const RootRange> ranges) noexcept;

class StateLease {
 public:
  explicit StateLease(ManagerState* state) : state_(state) {
    RetainState(state);
  }
  StateLease(const StateLease&) = delete;
  StateLease& operator=(const StateLease&) = delete;
  ~StateLease() noexcept {
    if (state_) ReleaseState(state_);
  }
  void Transfer() noexcept { state_ = nullptr; }

 private:
  ManagerState* state_;
};
struct Access {
  static ManagerState* State(const NodeManager& manager) noexcept {
    return manager.state_.state_;
  }
  static ManagerState* State(const ManagerRef& reference) noexcept {
    return reference.state_;
  }
  template <class View>
  static ManagerState* State(View view) noexcept {
    return view.state_;
  }
  static inline NodeData* Data(NodeView view) noexcept { return view.node_; }
  static NodeView ViewNode(NodeData* node) noexcept { return NodeView(node); }
  static Node OwnNode(NodeData* node) noexcept { return Node(node); }
  static NodeData* TakeNode(Node& node) noexcept {
    return std::exchange(node.node_, nullptr);
  }
  template <class View>
  static StateHandle<View> Own(ManagerState* state, std::uint64_t id) noexcept {
    return StateHandle<View>(state, id);
  }
  template <class View>
  static View ViewOf(ManagerState* state, std::uint64_t id) noexcept {
    return View(state, id);
  }
  static ManagerRef OwnState(ManagerState* state) noexcept {
    return ManagerRef(state);
  }
  static SortRange Range(ManagerState* state,
                         std::span<const SortId> ids) noexcept {
    return SortRange(state, ids);
  }
  static SortRecipeView RecipeView(ManagerState* state, DatatypeGroupId group,
                                   std::uint64_t index) noexcept {
    return SortRecipeView(state, group, index);
  }
  static const Recipe& GetRecipe(SortRecipeView view) noexcept {
    return StateGroup(view.state_, view.group_).recipes[view.index_];
  }
  static const GroupRecord& StateGroup(ManagerState* state,
                                       DatatypeGroupId id) noexcept {
    return *state->groups[id - 1];
  }
  static bool ValidRecipe(BuilderData* builder, SortExpr expr) noexcept {
    return expr.builder_ == builder && expr.index_ < builder->recipes.size();
  }
  static std::uint64_t RecipeIndex(SortExpr expr) noexcept {
    return expr.index_;
  }
};

inline ManagerState* CheckedState(const NodeManager& manager) {
  auto* state = Access::State(manager);
  Require(state != nullptr, ErrorCode::kNullObject);
  return state;
}

template <class View>
void CheckView(
    ManagerState* state, View view,
    std::uint64_t operand = std::numeric_limits<std::uint64_t>::max()) {
  Require(!view.IsNull(), ErrorCode::kNullObject, operand);
  Require(Access::State(view) == state, ErrorCode::kForeignManager, operand);
}
inline NodeData* CheckNode(ManagerState* state, NodeView node,
                           std::uint64_t operand) {
  auto* data = Access::Data(node);
  Require(data != nullptr, ErrorCode::kNullObject, operand);
  Require(data->state == state, ErrorCode::kForeignManager, operand);
  return data;
}
inline const SortRecord& SortAt(ManagerState* state, SortId id) noexcept {
  return *state->sorts[id - 1];
}
inline const SortConstructorRecord& ConstructorAt(
    ManagerState* state, SortConstructorId id) noexcept {
  return *state->sort_constructors[id - 1];
}
inline const DeclRecord& DeclAt(ManagerState* state, DeclId id) noexcept {
  return state->declarations[id - 1];
}
inline std::span<const SortId> DomainAt(ManagerState* state,
                                        const DeclRecord& record) noexcept {
  if (record.domain == 0) return {};
  return state->declaration_domain_spans[record.domain - 1];
}
inline InstanceEntry InstanceAt(ManagerState* state,
                                DeclInstanceId id) noexcept {
  return state->instances[id - 1];
}
inline const InstanceRecord& SubstitutionAt(ManagerState* state,
                                            std::uint64_t id) noexcept {
  return *state->substituted_signatures[id - 1];
}
inline ConcreteSignature SignatureAt(ManagerState* state,
                                     DeclInstanceId id) noexcept {
  const auto [decl, substitution] = InstanceAt(state, id);
  if (substitution) {
    const auto& record = SubstitutionAt(state, substitution);
    return {record.domain, record.range};
  }
  const auto& record = DeclAt(state, decl);
  return {DomainAt(state, record), record.range};
}
inline const GroupRecord& GroupAt(ManagerState* state,
                                  DatatypeGroupId id) noexcept {
  return *state->groups[id - 1];
}

template <class T>
void ReserveAppend(ManagerState* state, std::vector<T>& vector,
                   std::size_t count = 1) {
  if (count > vector.max_size() - vector.size())
    Fail(ErrorCode::kResourceLimit, ResourceKind::kSize);
  AllocationCheckpoint(state);
  const auto needed = vector.size() + count;
  if (needed <= vector.capacity()) return;
  const auto grown = vector.capacity() > vector.max_size() / 2
                         ? vector.max_size()
                         : vector.capacity() * 2;
  vector.reserve(std::max(needed, grown));
}
template <class T>
std::span<T> DeclarationPayload<T>::Append(ManagerState* state,
                                           std::size_t count) {
  if (count == 0) return {};
  if (blocks_.empty() ||
      count > blocks_.back().capacity - blocks_.back().used) {
    constexpr std::size_t kMaxChunk = 65536 / sizeof(T);
    const auto preferred =
        blocks_.empty() ? std::size_t{64}
                        : 2 * std::min(blocks_.back().capacity, kMaxChunk / 2);
    const auto capacity = std::max(count, preferred);
    AllocationCheckpoint(state);
    auto storage = std::make_unique_for_overwrite<T[]>(capacity);
    ReserveAppend(state, blocks_);
    blocks_.push_back({std::move(storage), capacity, 0});
  }
  auto& block = blocks_.back();
  const auto start = block.used;
  block.used += count;
  return {block.storage.get() + start, count};
}
inline std::uint64_t NextStoreId(ManagerState* state, std::size_t size,
                                 std::size_t count = 1) {
  const auto limit = IdentityCapacity(state);
  if (size > limit || count > limit - size)
    Fail(ErrorCode::kResourceLimit, ResourceKind::kIdentitySpace);
  return static_cast<std::uint64_t>(size) + 1;
}
std::uint32_t PositiveWidth(IntegerView value, std::uint32_t minimum = 1);
// Both format components have already been validated.
std::uint32_t CheckedFpWidth(std::uint32_t exponent, std::uint32_t significand);
SortId InternSort(ManagerState* state, SortKey key);
// Only primitive sorts with no parameters use this cache.
inline SortId ScalarSortId(ManagerState* state, SortKind kind) {
  auto& id = state->scalar_sorts[std::to_underlying(kind)];
  if (id == 0) id = InternSort(state, {kind, 0, 0, 0, {}});
  return id;
}
SortId InstantiateSortId(ManagerState* state, SortConstructorId constructor,
                         std::span<const SortId> arguments);
DeclId Declare(ManagerState* state, DeclKind kind, std::string_view name,
               std::span<const SortView> domain, SortView range);
DeclRecord PrepareDeclaration(ManagerState* state, DeclKind kind,
                              std::string_view name, SortId range);
inline void PublishSingleInstance(ManagerState* state, DeclId declaration,
                                  const DeclRecord& record,
                                  DeclInstanceId id) noexcept {
  state->instances.push_back({declaration});
  record.single_instance = id;
}
QueryKey BuiltinKey(ManagerState* state, BuiltinOpView op,
                    std::span<const NodeView> children);
void CheckPlainBuiltinShape(ManagerState* state, Kind kind,
                            std::size_t child_count);
SortId InferBuiltinSort(ManagerState* state, const QueryKey& key);
SortId CompleteKeySort(ManagerState* state, const QueryKey& key);
Node InternNode(ManagerState* state, const QueryKey& key);
Node InternPlainBuiltin(ManagerState* state, Kind kind,
                        std::span<const NodeView> children);
struct CandidateDeleter {
  void operator()(NodeData* node) const noexcept;
};
using Candidate = std::unique_ptr<NodeData, CandidateDeleter>;
Candidate PrepareNode(ManagerState* state, const QueryKey& key,
                      SortId result_sort, std::uint64_t hash);
Node PublishNode(ManagerState* state, std::size_t slot,
                 Candidate candidate) noexcept;
void CheckNodeIdentity(ManagerState* state);

struct WalkFrame {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  NodeData* node;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t next_child = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint8_t phase = 0;
};
enum class VisitState : std::uint8_t {
  kUnseen = 0,
  kEntered = 1,
  kFinished = 3,
};

class WalkVisited {
 public:
  VisitState Get(NodeId id) const noexcept;
  bool Mark(ManagerState* state, NodeId id);
  void Finish(NodeId id) noexcept;

 private:
  struct Entry {
    // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
    NodeId group = 0;
    // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
    std::uint64_t states = 0;
  };
  static std::size_t Slot(std::span<const Entry> entries,
                          NodeId group) noexcept;
  std::vector<Entry> entries_;
  std::size_t group_count_ = 0;
};
struct WalkerData {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  ManagerState* state = nullptr;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::vector<Node> roots;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::vector<WalkFrame> stack;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  WalkVisited visited;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::size_t next_root = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint8_t root_phase = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  bool can_skip = false;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  bool failed = false;
};

}  // namespace dandelion::detail

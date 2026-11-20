export module dandelion.node_workload;
import std;
import dandelion.node;
import dandelion.util.numeric;

namespace dandelion::node_bench {

enum class Action : std::uint8_t { kMake, kHold, kDrop };

struct Instruction {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  Action action;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  Kind kind = Kind::kInvalid;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint8_t payload_count = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::size_t slot;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::size_t source = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::size_t auxiliary = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::size_t child_begin = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::size_t payload_begin = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::size_t child_count = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::size_t line = 0;
};

}  // namespace dandelion::node_bench

export namespace dandelion::node_bench {

class Workload {
 public:
  static Workload Read(std::istream& input);
  static Workload Load(const std::filesystem::path& path);
  bool IsGraph() const noexcept { return graph_; }
  bool IsLookup() const noexcept { return LookupCount() != 0; }
  std::size_t LookupCount() const noexcept;
  std::size_t RequestCount() const noexcept { return requests_; }
  std::size_t OperationCount() const noexcept { return instructions_.size(); }
  std::size_t RootCount() const noexcept { return roots_.size(); }
  std::size_t PeakSlots() const noexcept { return peak_slots_; }
  std::size_t StorageBytes() const noexcept;

 private:
  friend class Replay;
  void AppendLookup(std::size_t slot);
  bool graph_ = false;
  std::size_t requests_ = 0;
  std::size_t slots_ = 0;
  std::size_t peak_slots_ = 0;
  std::size_t max_children_ = 0;
  std::size_t sort_count_ = 0;
  std::size_t instance_count_ = 0;
  std::vector<std::string> metadata_;
  std::vector<std::size_t> metadata_ends_;
  std::vector<Instruction> instructions_;
  std::vector<std::size_t> operands_;
  std::vector<numeric::Integer> payloads_;
  std::vector<numeric::Rational> rationals_;
  std::vector<std::size_t> roots_;
  std::variant<std::vector<std::uint32_t>, std::vector<std::size_t>> lookups_;
};

class Replay {
 public:
  explicit Replay(const Workload& workload, bool verify_results = false);
  bool Step();
  void Run();
  // Call after Run on a lookup workload. The seed remains owned by Replay.
  Node Lookup(std::size_t request);
  void RunLookups();
  // Call after Run. The returned handles take ownership of the root slots.
  std::vector<Node> TakeRoots();
  // Release the root slots after Run and rewind to replay in the same manager.
  void Reset();
  NodeView GetNode(std::size_t slot) const;
  NodeManager& GetManager() noexcept { return manager_; }
  std::size_t StorageBytes() const noexcept;

 private:
  void PrepareMetadata();
  Node Make(const Instruction& instruction);
  Node LookupSlot(std::size_t slot);

  const Workload& workload_;
  bool verify_results_;
  NodeManager manager_;
  std::vector<SortView> sorts_;
  std::vector<DeclInstanceView> instances_;
  std::vector<Node> slots_;
  std::vector<NodeView> children_;
  std::size_t next_ = 0;
};

class WalkStats {
 public:
  std::size_t Roots() const noexcept { return roots_; }
  std::size_t Nodes() const noexcept { return nodes_; }
  std::size_t Edges() const noexcept { return edges_; }
  std::size_t Depth() const noexcept { return depth_; }
  std::size_t MaxFanout() const noexcept { return max_fanout_; }

 private:
  friend WalkStats Walk(std::span<const Node>, bool);
  std::size_t roots_ = 0;
  std::size_t nodes_ = 0;
  std::size_t edges_ = 0;
  std::size_t depth_ = 0;
  std::size_t max_fanout_ = 0;
};

WalkStats Walk(std::span<const Node> roots, bool describe_shape = true);

// Typed fixtures for the parser-independent folding benchmark.
std::vector<Node> BuildFoldingDag(NodeManager& manager);
// Only the fixture's operator set is evaluated. Other nodes are rebuilt
// with their rewritten children, including operations with unresolved results.
std::vector<Node> FoldDag(NodeManager& manager, std::span<const Node> roots);

}  // namespace dandelion::node_bench

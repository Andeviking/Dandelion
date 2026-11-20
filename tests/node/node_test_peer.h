#ifndef DANDELION_TESTS_NODE_NODE_TEST_PEER_H
#define DANDELION_TESTS_NODE_NODE_TEST_PEER_H

// Include after importing std, dandelion.node, and dandelion.util.numeric.
extern "C++" {
namespace dandelion {

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
  std::uint64_t candidate_frees;
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
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t hash_computations;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t sort_table_lookups;
};
enum class NumericPayloadTag {
  kIntWord,
  kIntWidePositive,
  kIntWideNegative,
  kBvWord,
  kBvWide,
  kFpWord,
  kFpWide,
  kFpNaN,
  kFpPositiveInfinity,
  kFpNegativeInfinity,
  kFpPositiveZero,
  kFpNegativeZero,
  kRationalConversion,
  kRationalWidePositive,
  kRationalWideNegative,
};

struct NumericNodeLayout {
  // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
  NumericPayloadTag tag;
  std::size_t header_bytes;
  std::size_t payload_offset;
  std::size_t limb_count_offset;
  std::uint32_t literal_limbs;
  const std::byte* block;
  // NOLINTEND(readability-identifier-naming)
};

class NodeTestPeer {
 public:
  static void ForceCollisions(NodeManager& manager, std::uint64_t hash);
  static void FailCreationAllocationAfter(std::size_t successful) noexcept;
  static void ClearCreationFailure() noexcept;
  static NodeTestStats GetStats(const NodeManager& manager) noexcept;
  static std::uint64_t TypeEvaluations(const NodeManager& manager) noexcept;
  static std::uint64_t BuiltinShapeChecks(const NodeManager& manager) noexcept;
  static SortId CachedBvSortId(const NodeManager& manager,
                               std::uint32_t width) noexcept;
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
  // Observe the published block. The returned address borrows the node.
  static NumericNodeLayout InspectNumericNode(const Node& node) noexcept;
  // Exercise the factory's layout calculation without provisioning input limbs.
  static Result<std::size_t> CheckNumericLayout(
      NumericPayloadTag tag, std::uint64_t numerator_or_literal_limbs,
      std::uint64_t denominator_limbs, std::size_t size_limit);
  static Result<std::size_t> CheckNumericTailSize(std::size_t prefix_bytes,
                                                  std::uint32_t limbs,
                                                  std::size_t size_limit);
  // Call the same translation used by checked factories.
  static NodeError TranslateNumericError(
      numeric::NumericError error, bool literal,
      std::uint64_t operand_index = std::numeric_limits<std::uint64_t>::max(),
      std::uint64_t parameter_index =
          std::numeric_limits<std::uint64_t>::max()) noexcept;
};

}  // namespace dandelion
}  // extern "C++"

#endif

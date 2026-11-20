module;
#include <cstddef>

module dandelion.node;
import :internal;
import std;
import dandelion.util.numeric;

#include "node_test_peer.h"

extern "C++" {

namespace dandelion {
using namespace detail;
void NodeTestPeer::FailCreationAllocationAfter(
    std::size_t successful) noexcept {
  creation_failure = successful;
}
void NodeTestPeer::ClearCreationFailure() noexcept { creation_failure.reset(); }
void NodeTestPeer::ForceCollisions(NodeManager& manager, std::uint64_t hash) {
  auto* state = CheckedState(manager);
  Require(state->allocations == 0, ErrorCode::kInvalidParameter);
  state->collision_hash = hash;
}
NodeTestStats NodeTestPeer::GetStats(const NodeManager& manager) noexcept {
  const auto* state = Access::State(manager);
  return {state->sorts.size(),
          state->sort_constructors.size(),
          state->declarations.size(),
          state->instances.size(),
          state->groups.size(),
          state->table.Size(),
          state->table.Capacity(),
          state->next_node_id,
          state->allocations,
          state->reclamations,
          state->candidate_frees,
          state->allocation_attempts,
          state->table.InsertionSearches(),
          state->table.ProbeHistogram(),
          state->table.ProbeSlots(),
          state->table.ProbeMax(),
          state->hash_computations,
          state->sort_table_lookups};
}
void NodeTestPeer::FailAllocationAfter(NodeManager& manager,
                                       std::size_t successful) {
  CheckedState(manager)->allocation_failure = successful;
}
std::uint64_t NodeTestPeer::TypeEvaluations(
    const NodeManager& manager) noexcept {
  return Access::State(manager)->type_evaluations;
}
std::uint64_t NodeTestPeer::BuiltinShapeChecks(
    const NodeManager& manager) noexcept {
  return Access::State(manager)->builtin_shape_checks;
}
SortId NodeTestPeer::CachedBvSortId(const NodeManager& manager,
                                    std::uint32_t width) noexcept {
  return Access::State(manager)->bv_sorts.at(width - 1);
}
std::size_t NodeTestPeer::DeclarationPayloadUsed(
    const NodeManager& manager) noexcept {
  const auto* state = Access::State(manager);
  return state->declaration_names.UsedBytes() +
         state->declaration_domains.UsedBytes();
}
std::size_t NodeTestPeer::NonemptyDeclarationDomains(
    const NodeManager& manager) noexcept {
  return Access::State(manager)->declaration_domain_spans.size();
}
void NodeTestPeer::FailRetainAfter(NodeManager& manager,
                                   std::size_t successful) {
  CheckedState(manager)->retain_failure = successful;
}
void NodeTestPeer::ClearFailures(NodeManager& manager) noexcept {
  auto* state = Access::State(manager);
  state->allocation_failure.reset();
  state->retain_failure.reset();
}
void NodeTestPeer::SetNextNodeId(NodeManager& manager, NodeId next) {
  auto* state = CheckedState(manager);
  Require(
      next >= state->next_node_id && next != 0 && !state->node_ids_exhausted,
      ErrorCode::kInvalidParameter);
  state->next_node_id = next;
}
void NodeTestPeer::SetIdentityLimit(NodeManager& manager, std::uint64_t limit) {
  CheckedState(manager)->identity_limit = limit;
}
void NodeTestPeer::SetReferenceLimit(NodeManager& manager,
                                     std::uint64_t limit) {
  CheckedState(manager)->reference_limit = limit;
}
void NodeTestPeer::SetTableCapacityLimit(NodeManager& manager,
                                         std::size_t limit) {
  CheckedState(manager)->table_limit = limit;
}
void NodeTestPeer::SetSizeLimit(NodeManager& manager, std::size_t limit) {
  CheckedState(manager)->size_limit = limit;
}
std::uint64_t NodeTestPeer::StructuralHash(const Node& node) noexcept {
  return Access::Data(node)->structural_hash;
}
std::uint64_t NodeTestPeer::RefCount(const Node& node) noexcept {
  return Access::Data(node)->ref_count;
}
std::uint64_t NodeTestPeer::StateRefCount(const NodeManager& manager) noexcept {
  return Access::State(manager)->references;
}
std::uint64_t NodeTestPeer::DestroyedStates() noexcept {
  return destroyed_states.load();
}
namespace {

constexpr std::array kNumericPayloadTags{
    std::pair{NumericPayloadTag::kIntWord, PayloadTag::kIntWord},
    std::pair{NumericPayloadTag::kIntWidePositive,
              PayloadTag::kIntWidePositive},
    std::pair{NumericPayloadTag::kIntWideNegative,
              PayloadTag::kIntWideNegative},
    std::pair{NumericPayloadTag::kBvWord, PayloadTag::kBvWord},
    std::pair{NumericPayloadTag::kBvWide, PayloadTag::kBvWide},
    std::pair{NumericPayloadTag::kFpWord, PayloadTag::kFpWord},
    std::pair{NumericPayloadTag::kFpWide, PayloadTag::kFpWide},
    std::pair{NumericPayloadTag::kFpNaN, PayloadTag::kFpNaN},
    std::pair{NumericPayloadTag::kFpPositiveInfinity,
              PayloadTag::kFpPositiveInfinity},
    std::pair{NumericPayloadTag::kFpNegativeInfinity,
              PayloadTag::kFpNegativeInfinity},
    std::pair{NumericPayloadTag::kFpPositiveZero, PayloadTag::kFpPositiveZero},
    std::pair{NumericPayloadTag::kFpNegativeZero, PayloadTag::kFpNegativeZero},
    std::pair{NumericPayloadTag::kRationalConversion,
              PayloadTag::kRationalConversion},
    std::pair{NumericPayloadTag::kRationalWidePositive,
              PayloadTag::kRationalWidePositive},
    std::pair{NumericPayloadTag::kRationalWideNegative,
              PayloadTag::kRationalWideNegative},
};

}  // namespace

NumericNodeLayout NodeTestPeer::InspectNumericNode(const Node& node) noexcept {
  const auto* data = Access::Data(node);
  for (const auto& [test_tag, tag] : kNumericPayloadTags) {
    if (data->payload_tag != tag) continue;

    const auto* payload = static_cast<const std::byte*>(Payload(data));
    return {test_tag,
            sizeof(NodeData),
            static_cast<std::size_t>(payload - data->storage),
            offsetof(NodeData, literal_limb_count),
            data->literal_limb_count,
            data->storage};
  }
  std::terminate();
}

Result<std::size_t> NodeTestPeer::CheckNumericLayout(
    NumericPayloadTag tag, std::uint64_t numerator_or_literal_limbs,
    std::uint64_t denominator_limbs, std::size_t size_limit) {
  for (const auto& [test_tag, internal_tag] : kNumericPayloadTags) {
    if (test_tag == tag) {
      return detail::CheckNumericNodeSize(internal_tag,
                                          numerator_or_literal_limbs,
                                          denominator_limbs, size_limit);
    }
  }
  std::terminate();
}

Result<std::size_t> NodeTestPeer::CheckNumericTailSize(std::size_t prefix_bytes,
                                                       std::uint32_t limbs,
                                                       std::size_t size_limit) {
  return detail::CheckNumericTailSize(prefix_bytes, limbs, size_limit);
}

NodeError NodeTestPeer::TranslateNumericError(
    numeric::NumericError error, bool literal, std::uint64_t operand_index,
    std::uint64_t parameter_index) noexcept {
  return detail::TranslateNumericError(error, literal, operand_index,
                                       parameter_index);
}

}  // namespace dandelion
}  // extern "C++"

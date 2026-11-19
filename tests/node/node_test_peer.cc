module dandelion.node;
import :internal;
import std;

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
          state->allocation_attempts,
          state->table.InsertionSearches(),
          state->table.ProbeHistogram(),
          state->table.ProbeSlots(),
          state->table.ProbeMax()};
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
  return node.node_->structural_hash;
}
std::uint64_t NodeTestPeer::RefCount(const Node& node) noexcept {
  return node.node_->ref_count;
}
std::uint64_t NodeTestPeer::StateRefCount(const NodeManager& manager) noexcept {
  return Access::State(manager)->references;
}
std::uint64_t NodeTestPeer::DestroyedStates() noexcept {
  return destroyed_states.load();
}
}  // namespace dandelion

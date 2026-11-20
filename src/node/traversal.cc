module dandelion.node;
import :internal;
import std;

namespace dandelion::detail {
namespace {
constexpr auto kStatesPerWord = 32;

// Shift the word index by one to keep zero available for empty entries.
NodeId VisitGroup(NodeId id) noexcept { return id / kStatesPerWord + 1; }
unsigned VisitShift(NodeId id) noexcept {
  return 2 * static_cast<unsigned>(id % kStatesPerWord);
}
}  // namespace

std::size_t WalkVisited::Slot(std::span<const Entry> entries,
                              NodeId group) noexcept {
  const auto mask = entries.size() - 1;
  auto slot = static_cast<std::size_t>(Mix(group)) & mask;
  while (entries[slot].group && entries[slot].group != group)
    slot = (slot + 1) & mask;
  return slot;
}

VisitState WalkVisited::Get(NodeId id) const noexcept {
  if (entries_.empty()) return VisitState::kUnseen;
  const auto& entry = entries_[Slot(entries_, VisitGroup(id))];
  return static_cast<VisitState>((entry.states >> VisitShift(id)) & 3);
}

bool WalkVisited::Mark(ManagerState* state, NodeId id) {
  const auto group = VisitGroup(id);
  const auto capacity = entries_.size();
  auto slot = capacity ? Slot(entries_, group) : 0;
  if ((!capacity || !entries_[slot].group) &&
      group_count_ == capacity - capacity / 4) {
    if (capacity > entries_.max_size() / 2)
      Fail(ErrorCode::kResourceLimit, ResourceKind::kSize);
    AllocationCheckpoint(state);
    std::vector<Entry> slots(capacity ? capacity * 2 : 16);
    for (const auto& entry : entries_) {
      if (entry.group) slots[Slot(slots, entry.group)] = entry;
    }
    entries_.swap(slots);
    slot = Slot(entries_, group);
  }
  auto& entry = entries_[slot];
  if (!entry.group) {
    entry.group = group;
    ++group_count_;
  }
  const auto bit = std::uint64_t{1} << VisitShift(id);
  if (entry.states & bit) return false;
  entry.states |= bit;
  return true;
}

void WalkVisited::Finish(NodeId id) noexcept {
  auto& entry = entries_[Slot(entries_, VisitGroup(id))];
  entry.states |= std::uint64_t{3} << VisitShift(id);
}
}  // namespace dandelion::detail

namespace dandelion {
using namespace detail;
namespace {
ManagerState* ValidateWalkRoots(std::span<const Node> roots) {
  ManagerState* state = nullptr;
  for (std::size_t i = 0; i < roots.size(); ++i) {
    Require(roots[i].GetId() != 0, ErrorCode::kNullObject, i);
    auto* owner = Access::Data(roots[i].Borrow())->state;
    if (!state) state = owner;
    Require(owner == state, ErrorCode::kForeignManager, i);
  }
  return state;
}

struct PostorderFrame {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  NodeData* node;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::span<NodeData* const> remaining;
};
}  // namespace

DagWalker::DagWalker(DagWalker&& other) noexcept
    : data_(std::exchange(other.data_, nullptr)) {}
DagWalker& DagWalker::operator=(DagWalker&& other) noexcept {
  if (this != &other) {
    delete data_;
    data_ = std::exchange(other.data_, nullptr);
  }
  return *this;
}
DagWalker::~DagWalker() noexcept { delete data_; }
Result<DagWalker> DagWalker::Create(std::span<const Node> roots) {
  return Guard<DagWalker>([&] {
    auto* state = ValidateWalkRoots(roots);
    AllocationCheckpoint(state);
    auto data = std::make_unique<WalkerData>();
    data->state = state;
    AllocationCheckpoint(state);
    data->roots.assign(roots.begin(), roots.end());
    DagWalker walker;
    walker.data_ = data.release();
    return walker;
  });
}
Result<std::optional<DagEvent>> DagWalker::Next() {
  if (!data_ || data_->failed)
    return std::unexpected(NodeError{ErrorCode::kInvalidTraversalState});
  data_->can_skip = false;
  auto result =
      Guard<std::optional<DagEvent>>([&]() -> std::optional<DagEvent> {
        auto& data = *data_;
        const auto event = [](DagEventKind kind, NodeData* node,
                              NodeData* parent = nullptr,
                              std::uint64_t index =
                                  std::numeric_limits<std::uint64_t>::max()) {
          return DagEvent{kind, Access::ViewNode(node),
                          Access::ViewNode(parent), index};
        };
        for (;;) {
          if (data.stack.empty()) {
            if (data.next_root == data.roots.size()) return std::nullopt;
            auto* root = Access::Data(data.roots[data.next_root].Borrow());
            const auto index = data.next_root;
            if (data.root_phase == 0) {
              data.root_phase = 1;
              return event(DagEventKind::kRootEnter, root, nullptr, index);
            }
            if (data.root_phase == 2 ||
                data.visited.Get(root->id) != VisitState::kUnseen) {
              data.root_phase = 0;
              ++data.next_root;
              return event(DagEventKind::kRootExit, root, nullptr, index);
            }
            AllocationCheckpoint(data.state);
            data.stack.push_back({root, 0, 0});
            data.root_phase = 2;
          }
          auto& frame = data.stack.back();
          auto* node = frame.node;
          if (frame.phase == 0) {
            data.visited.Mark(data.state, node->id);
            frame.phase = 1;
            data.can_skip = true;
            return event(DagEventKind::kNodeEnter, node);
          }
          if (frame.phase == 1) {
            if (frame.next_child == node->ChildCount()) {
              data.visited.Finish(node->id);
              data.stack.pop_back();
              return event(DagEventKind::kNodeExit, node);
            }
            frame.phase = 2;
            return event(DagEventKind::kEdgeEnter,
                         Children(node)[frame.next_child], node,
                         frame.next_child);
          }
          auto* child = Children(node)[frame.next_child];
          if (frame.phase == 2) {
            const auto status = data.visited.Get(child->id);
            if (status == VisitState::kUnseen) {
              frame.phase = 3;
              AllocationCheckpoint(data.state);
              data.stack.push_back({child, 0, 0});
              continue;
            }
            if (status == VisitState::kEntered)
              std::terminate();  // Immutable expressions cannot contain a back
                                 // edge.
          }
          const auto index = frame.next_child++;
          frame.phase = 1;
          return event(DagEventKind::kEdgeExit, child, node, index);
        }
      });
  if (!result) data_->failed = true;
  return result;
}
Result<void> DagWalker::SkipChildren() {
  if (!data_ || data_->failed || !data_->can_skip)
    return std::unexpected(NodeError{ErrorCode::kInvalidTraversalState});
  data_->stack.back().next_child = data_->stack.back().node->ChildCount();
  data_->can_skip = false;
  return {};
}

Result<void> VisitDagPostorder(
    std::span<const Node> roots,
    const std::function<Result<void>(NodeView)>& visit) {
  return Guard<void>([&] {
    auto* state = ValidateWalkRoots(roots);
    Require(static_cast<bool>(visit), ErrorCode::kInvalidTraversalState);
    if (roots.empty()) return;
    AllocationCheckpoint(state);
    const std::vector<Node> owned_roots(roots.begin(), roots.end());
    WalkVisited visited;
    std::vector<PostorderFrame> stack;
    const auto descend = [&](NodeData* node) {
      // A previously discovered child cannot be on the active path in an
      // immutable DAG, so one discovery bit is sufficient.
      if (!visited.Mark(state, node->id)) return;
      if (stack.size() == stack.capacity()) AllocationCheckpoint(state);
      stack.push_back({node, Children(node)});
    };
    for (const auto& root : owned_roots) {
      descend(Access::Data(root.Borrow()));
      while (!stack.empty()) {
        auto& frame = stack.back();
        if (frame.remaining.empty()) {
          const auto node = Access::ViewNode(frame.node);
          stack.pop_back();
          if (auto result = visit(node); !result) throw Failure{result.error()};
        } else {
          auto* child = frame.remaining.front();
          frame.remaining = frame.remaining.subspan(1);
          descend(child);
        }
      }
    }
  });
}
}  // namespace dandelion

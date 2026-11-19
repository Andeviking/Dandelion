module dandelion.frontend;
import :internal;
import std;

namespace dandelion::frontend::detail {

std::span<NodeView> CommutativeOperands(Kind kind,
                                        std::span<NodeView> operands) noexcept {
  using enum Kind;
  switch (kind) {
    case kAnd:
    case kOr:
    case kXor:
    case kEqual:
    case kDistinct:
    case kIntAdd:
    case kIntMul:
    case kBvAnd:
    case kBvOr:
    case kBvXor:
    case kBvAdd:
    case kBvMul:
    case kFpEq:
    case kBvNand:
    case kBvNor:
    case kBvXnor:
    case kBvComp:
    case kBvUaddo:
    case kBvSaddo:
    case kBvUmulo:
    case kBvSmulo:
      return operands;
    case kFpAdd:
    case kFpMul:
    case kFpFma:
      return operands.subspan(1, 2);
    default:
      return {};
  }
}

bool OrderCommutative(std::span<NodeView> operands) noexcept {
  const auto swap = [&](std::size_t left, std::size_t right) {
    if (operands[left].GetId() <= operands[right].GetId()) return false;
    std::swap(operands[left], operands[right]);
    return true;
  };
  switch (operands.size()) {
    case 0:
    case 1:
      return false;
    case 2:
      return swap(0, 1);
    case 3: {
      const bool first = swap(0, 1);
      const bool second = swap(1, 2);
      const bool third = swap(0, 1);
      return first || second || third;
    }
    default:
      if (std::ranges::is_sorted(operands, {}, &NodeView::GetId)) return false;
      std::ranges::sort(operands, {}, &NodeView::GetId);
      return true;
  }
}

namespace {
class Canonicalizer {
 public:
  explicit Canonicalizer(const ManagerRef& manager)
      : manager_(Take(NodeManager::Attach(manager), {})) {}

  Node Rewrite(NodeView root) {
    // Validate manager identity before using same-manager IDs as memo keys.
    auto owner = Take(manager_.RetainNode(root), {});
    work_.push_back({root, 0});
    while (!work_.empty()) {
      auto& [node, next] = work_.back();
      const auto id = node.GetId();
      if (memo_.contains(id)) {
        work_.pop_back();
        continue;
      }
      const auto children = node.GetChildren();
      if (next < children.size()) {
        const auto child = children[next++];
        if (!memo_.contains(child.GetId())) work_.push_back({child, 0});
        continue;
      }
      memo_.emplace(id, Rebuild(node));
      work_.pop_back();
    }
    return memo_.at(root.GetId());
  }

 private:
  Node Rebuild(NodeView node) {
    operands_.clear();
    bool changed = false;
    for (auto child : node.GetChildren()) {
      const auto rewritten = memo_.at(child.GetId()).Borrow();
      changed |= rewritten != child;
      operands_.push_back(rewritten);
    }
    changed |= OrderCommutative(CommutativeOperands(node.GetKind(), operands_));
    if (!changed) return Take(manager_.RetainNode(node), {});
    if (const auto decl = node.GetDeclaration())
      return Take(manager_.MakeApply(*decl, operands_), {});
    indices_.clear();
    for (const auto index : node.GetIndices())
      indices_.push_back(index.Borrow().Copy());
    const auto qualifier = node.GetKind() == Kind::kArrayConst
                               ? std::optional(node.GetSort())
                               : std::nullopt;
    return Take(manager_.MakeBuiltin({node.GetKind(), indices_, qualifier,
                                      node.GetRationalParameter()},
                                     operands_),
                {});
  }

  NodeManager manager_;
  std::vector<std::pair<NodeView, std::size_t>> work_;
  std::unordered_map<NodeId, Node> memo_;
  std::vector<NodeView> operands_;
  std::vector<Integer> indices_;
};
}  // namespace
}  // namespace dandelion::frontend::detail

namespace dandelion::frontend {
Outcome<std::vector<Node>> CanonicalizeCommutative(
    const ManagerRef& manager, std::span<const Node> roots) {
  return detail::Guard({}, [&] {
    detail::Canonicalizer pass(manager);
    std::vector<Node> result;
    result.reserve(roots.size());
    for (const auto& root : roots)
      result.push_back(pass.Rewrite(root.Borrow()));
    return result;
  });
}
Outcome<Node> CanonicalizeCommutative(const ManagerRef& manager,
                                      NodeView root) {
  return detail::Guard(
      {}, [&] { return detail::Canonicalizer(manager).Rewrite(root); });
}
}  // namespace dandelion::frontend

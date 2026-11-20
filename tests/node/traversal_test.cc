#include "test_support.h"

namespace dandelion::testing {
namespace {

class Traversal : public NodeTest {};

using EventSummary = std::tuple<DagEventKind, NodeId, NodeId, std::uint64_t>;

std::vector<EventSummary> Events(DagWalker& walker) {
  std::vector<EventSummary> events;
  while (const auto event = Take(walker.Next())) {
    if (event->kind != DagEventKind::kNodeEnter) {
      Error(walker.SkipChildren(), ErrorCode::kInvalidTraversalState);
    }
    events.emplace_back(event->kind, event->node.GetId(), event->parent.GetId(),
                        event->index);
  }
  return events;
}

TEST_F(Traversal, EmptyRootsAndExactDiamondRepeatedEdgesAndRoots) {
  auto empty = Take(DagWalker::Create({}));
  EXPECT_FALSE(Take(empty.Next()));
  Error(empty.SkipChildren(), ErrorCode::kInvalidTraversalState);
  const auto x = Take(manager_.MakeBool(true));
  const auto a = Op(Kind::kNot, {x.Borrow()});
  const auto b = Op(Kind::kAnd, {a.Borrow(), a.Borrow(), x.Borrow()});
  const std::array roots = {b, a, b};
  auto walker = Take(DagWalker::Create(roots));
  const auto events = Events(walker);
  using enum DagEventKind;
  const auto n = UINT64_MAX;
  const std::vector<EventSummary> expected = {
      {kRootEnter, b.GetId(), 0, 0},
      {kNodeEnter, b.GetId(), 0, n},
      {kEdgeEnter, a.GetId(), b.GetId(), 0},
      {kNodeEnter, a.GetId(), 0, n},
      {kEdgeEnter, x.GetId(), a.GetId(), 0},
      {kNodeEnter, x.GetId(), 0, n},
      {kNodeExit, x.GetId(), 0, n},
      {kEdgeExit, x.GetId(), a.GetId(), 0},
      {kNodeExit, a.GetId(), 0, n},
      {kEdgeExit, a.GetId(), b.GetId(), 0},
      {kEdgeEnter, a.GetId(), b.GetId(), 1},
      {kEdgeExit, a.GetId(), b.GetId(), 1},
      {kEdgeEnter, x.GetId(), b.GetId(), 2},
      {kEdgeExit, x.GetId(), b.GetId(), 2},
      {kNodeExit, b.GetId(), 0, n},
      {kRootExit, b.GetId(), 0, 0},
      {kRootEnter, a.GetId(), 0, 1},
      {kRootExit, a.GetId(), 0, 1},
      {kRootEnter, b.GetId(), 0, 2},
      {kRootExit, b.GetId(), 0, 2}};
  EXPECT_EQ(events, expected);
  EXPECT_FALSE(Take(walker.Next()));
}

TEST_F(Traversal, PostorderPinsRootsAndVisitsEachSharedNodeOnce) {
  auto leaf = Take(manager_.MakeBool(true));
  const auto leaf_id = leaf.GetId();
  NodeTestPeer::SetNextNodeId(manager_, std::uint64_t{1} << 63);
  auto branch = Op(Kind::kNot, {leaf.Borrow()});
  const auto branch_id = branch.GetId();
  auto root = Op(Kind::kAnd, {branch.Borrow(), branch.Borrow(), leaf.Borrow()});
  const auto root_id = root.GetId();
  std::array roots = {std::move(root), std::move(branch), Node{}};
  roots[2] = roots[0];
  leaf = {};
  std::vector<NodeId> seen;
  Take(VisitDagPostorder(roots, [&](NodeView node) -> Result<void> {
    seen.push_back(node.GetId());
    if (node.GetId() == leaf_id) {
      std::size_t nested = 0;
      Take(VisitDagPostorder(std::span{roots}.first(1), [&](NodeView) {
        ++nested;
        return Result<void>{};
      }));
      EXPECT_EQ(nested, 3);
      roots = {};
    }
    return {};
  }));
  EXPECT_EQ(seen, (std::vector{leaf_id, branch_id, root_id}));
  EXPECT_EQ(NodeTestPeer::GetStats(manager_).live_nodes, 0);
}

TEST_F(Traversal, PostorderValidatesBeforeCallingAndPropagatesFailure) {
  const auto leaf = Take(manager_.MakeBool(true));
  const auto root = Op(Kind::kNot, {leaf.Borrow()});
  auto other = Take(NodeManager::Create());
  const std::array roots = {root};
  std::size_t calls = 0;
  const auto visit = [&](NodeView) -> Result<void> {
    ++calls;
    return std::unexpected(NodeError{ErrorCode::kInvalidParameter});
  };
  Take(VisitDagPostorder({}, visit));
  Error(VisitDagPostorder(roots, {}), ErrorCode::kInvalidTraversalState);
  Error(VisitDagPostorder(std::array{root, Node{}}, visit),
        ErrorCode::kNullObject);
  Error(VisitDagPostorder(std::array{root, Take(other.MakeBool(true))}, visit),
        ErrorCode::kForeignManager);
  NodeTestPeer::FailRetainAfter(manager_, 0);
  Error(VisitDagPostorder(roots, visit), ErrorCode::kResourceLimit);
  NodeTestPeer::ClearFailures(manager_);
  NodeTestPeer::FailAllocationAfter(manager_, 2);
  Error(VisitDagPostorder(roots, visit), ErrorCode::kResourceLimit);
  NodeTestPeer::ClearFailures(manager_);
  EXPECT_EQ(calls, 0);
  Error(VisitDagPostorder(roots, visit), ErrorCode::kInvalidParameter);
  EXPECT_EQ(calls, 1);
  EXPECT_EQ(NodeTestPeer::GetStats(manager_).live_nodes, 2);
}

TEST_F(Traversal, PostorderUsesAnExplicitStackForDeepChains) {
  constexpr std::size_t size = 20000;
  auto root = Take(manager_.MakeBool(true));
  const auto first = root.GetId();
  for (std::size_t i = 1; i < size; ++i) root = Op(Kind::kNot, {root.Borrow()});
  NodeId expected = first;
  Take(VisitDagPostorder(std::span{&root, 1},
                         [&](NodeView node) -> Result<void> {
                           EXPECT_EQ(node.GetId(), expected++);
                           return {};
                         }));
  EXPECT_EQ(expected, first + size);
}

TEST_F(Traversal,
       SkipOnlyImmediatelyAfterEnterAndNeverExpandSkippedSharedNode) {
  const auto x = Take(manager_.MakeBool(true));
  const auto a = Op(Kind::kNot, {x.Borrow()});
  const auto root = Op(Kind::kAnd, {a.Borrow(), a.Borrow()});
  const std::array roots = {root};
  auto walker = Take(DagWalker::Create(roots));
  Error(walker.SkipChildren(), ErrorCode::kInvalidTraversalState);
  EXPECT_EQ(Take(walker.Next())->kind, DagEventKind::kRootEnter);
  Error(walker.SkipChildren(), ErrorCode::kInvalidTraversalState);
  EXPECT_EQ(Take(walker.Next())->kind, DagEventKind::kNodeEnter);
  EXPECT_EQ(Take(walker.Next())->kind, DagEventKind::kEdgeEnter);
  EXPECT_EQ(Take(walker.Next())->node.GetId(), a.GetId());
  Take(walker.SkipChildren());
  Error(walker.SkipChildren(), ErrorCode::kInvalidTraversalState);
  const auto events = Events(walker);
  EXPECT_EQ(std::count_if(events.begin(), events.end(),
                          [](const auto& event) {
                            return std::get<0>(event) ==
                                   DagEventKind::kEdgeEnter;
                          }),
            1);  // Only the second root→a edge, no a→x.
  for (const auto& event : events) EXPECT_NE(std::get<1>(event), x.GetId());
}

TEST_F(Traversal, SharedChildExitsBeforeParentAndViewsSurviveGrowth) {
  const auto b = Take(manager_.MakeBool(true));
  const auto a = Op(Kind::kNot, {b.Borrow()});
  const auto root = Op(Kind::kAnd, {a.Borrow(), b.Borrow()});
  const std::array roots = {root};
  auto walker = Take(DagWalker::Create(roots));
  const auto first = Take(walker.Next());
  std::vector<Node> extra;
  for (int i = 0; i < 100; ++i) extra.push_back(Int(i));
  EXPECT_EQ(first->node.GetId(), root.GetId());
  std::vector<NodeId> postorder;
  while (const auto event = Take(walker.Next())) {
    if (event->kind == DagEventKind::kNodeExit)
      postorder.push_back(event->node.GetId());
  }
  EXPECT_EQ(postorder,
            (std::vector<NodeId>{b.GetId(), a.GetId(), root.GetId()}));
  const std::array null_roots = {Node{}};
  Error(DagWalker::Create(null_roots), ErrorCode::kNullObject);
  auto other = Take(NodeManager::Create());
  const std::array foreign_roots = {root, Take(other.MakeBool(true))};
  Error(DagWalker::Create(foreign_roots), ErrorCode::kForeignManager);
}

TEST_F(Traversal, SparseIdsAndInterleavedWalkers) {
  std::vector<Node> chain;
  chain.push_back(Take(manager_.MakeBool(true)));
  for (std::uint32_t i = 0; i < 128; ++i) {
    NodeTestPeer::SetNextNodeId(
        manager_, (std::uint64_t{1} << 63) + (std::uint64_t{i} << 24));
    chain.push_back(Op(Kind::kNot, {chain.back().Borrow()}));
  }
  const std::array roots = {chain.back(), chain.front(), chain.back()};
  auto first = Take(DagWalker::Create(roots));
  auto second = Take(DagWalker::Create(roots));
  using enum DagEventKind;
  const auto leaf = chain.front().GetId();
  const auto root = chain.back().GetId();
  const auto no_index = UINT64_MAX;
  std::vector<EventSummary> expected = {{kRootEnter, root, 0, 0}};
  for (std::size_t i = chain.size(); i-- > 0;) {
    expected.emplace_back(kNodeEnter, chain[i].GetId(), 0, no_index);
    if (i != 0)
      expected.emplace_back(kEdgeEnter, chain[i - 1].GetId(), chain[i].GetId(),
                            0);
  }
  for (std::size_t i = 0; i < chain.size(); ++i) {
    expected.emplace_back(kNodeExit, chain[i].GetId(), 0, no_index);
    if (i + 1 != chain.size())
      expected.emplace_back(kEdgeExit, chain[i].GetId(), chain[i + 1].GetId(),
                            0);
  }
  expected.insert(expected.end(), {{kRootExit, root, 0, 0},
                                   {kRootEnter, leaf, 0, 1},
                                   {kRootExit, leaf, 0, 1},
                                   {kRootEnter, root, 0, 2},
                                   {kRootExit, root, 0, 2}});
  for (const auto& item : expected) {
    for (auto* walker : {&first, &second}) {
      const auto event = Take(walker->Next());
      ASSERT_TRUE(event);
      EXPECT_EQ((EventSummary{event->kind, event->node.GetId(),
                              event->parent.GetId(), event->index}),
                item);
    }
  }
  EXPECT_FALSE(Take(first.Next()));
  EXPECT_FALSE(Take(second.Next()));
}

TEST_F(Traversal, AdjacentIdsAndLastLegalIdHaveIndependentVisitStates) {
  NodeTestPeer::SetNextNodeId(manager_, 31);
  const auto last_in_word = Take(manager_.MakeBool(true));
  const auto first_in_word = Op(Kind::kNot, {last_in_word.Borrow()});
  NodeTestPeer::SetNextNodeId(manager_, std::numeric_limits<NodeId>::max());
  const auto root =
      Op(Kind::kAnd, {first_in_word.Borrow(), last_in_word.Borrow()});
  auto walker = Take(DagWalker::Create(std::array{root, first_in_word}));
  std::vector<NodeId> preorder;
  std::vector<NodeId> postorder;
  while (const auto event = Take(walker.Next())) {
    if (event->kind == DagEventKind::kNodeEnter)
      preorder.push_back(event->node.GetId());
    if (event->kind == DagEventKind::kNodeExit)
      postorder.push_back(event->node.GetId());
  }
  EXPECT_EQ(preorder, (std::vector<NodeId>{root.GetId(), 32, 31}));
  EXPECT_EQ(postorder, (std::vector<NodeId>{31, 32, root.GetId()}));
}

}  // namespace

TEST_F(Traversal, SparseVisitTableFailuresKeepExactEventPrefixAndRoots) {
  constexpr std::size_t kLeaves = 48;
  std::vector<Node> leaves;
  std::vector<NodeView> children;
  for (std::size_t i = 0; i < kLeaves; ++i) {
    NodeTestPeer::SetNextNodeId(manager_, (std::uint64_t{1} << 63) + (i << 12));
    leaves.push_back(Int(static_cast<std::int64_t>(i)));
    children.push_back(leaves.back().Borrow());
  }
  const auto root =
      Take(manager_.MakeBuiltin({Kind::kIntAdd, {}, {}, {}}, children));
  const std::array roots{root, leaves.front(), root};
  using enum DagEventKind;
  const auto top = root.GetId();
  const auto first = leaves.front().GetId();
  std::vector<EventSummary> expected{{kRootEnter, top, 0, 0},
                                     {kNodeEnter, top, 0, UINT64_MAX}};
  for (std::size_t i = 0; i < kLeaves; ++i) {
    const auto id = leaves[i].GetId();
    expected.insert(expected.end(), {{kEdgeEnter, id, top, i},
                                     {kNodeEnter, id, 0, UINT64_MAX},
                                     {kNodeExit, id, 0, UINT64_MAX},
                                     {kEdgeExit, id, top, i}});
  }
  expected.insert(expected.end(), {{kNodeExit, top, 0, UINT64_MAX},
                                   {kRootExit, top, 0, 0},
                                   {kRootEnter, first, 0, 1},
                                   {kRootExit, first, 0, 1},
                                   {kRootEnter, top, 0, 2},
                                   {kRootExit, top, 0, 2}});
  const auto root_refs = NodeTestPeer::RefCount(root);
  const auto leaf_refs = NodeTestPeer::RefCount(leaves.front());
  bool success = false;
  std::vector<std::size_t> failure_prefixes;
  for (std::size_t checkpoint = 0; checkpoint < 96 && !success; ++checkpoint) {
    {
      auto walker = Take(DagWalker::Create(roots));
      NodeTestPeer::FailAllocationAfter(manager_, checkpoint);
      std::vector<EventSummary> prefix;
      for (;;) {
        const auto next = walker.Next();
        if (!next) {
          Resource(next, ResourceKind::kCppAllocation);
          failure_prefixes.push_back(prefix.size());
          Error(walker.Next(), ErrorCode::kInvalidTraversalState);
          Error(walker.SkipChildren(), ErrorCode::kInvalidTraversalState);
          break;
        }
        if (!*next) {
          success = true;
          break;
        }
        const auto& event = **next;
        prefix.emplace_back(event.kind, event.node.GetId(),
                            event.parent.GetId(), event.index);
      }
      NodeTestPeer::ClearFailures(manager_);
      EXPECT_EQ(NodeTestPeer::RefCount(root), root_refs + 2);
      EXPECT_EQ(NodeTestPeer::RefCount(leaves.front()), leaf_refs + 1);
      ASSERT_LE(prefix.size(), expected.size());
      EXPECT_TRUE(
          std::ranges::equal(prefix, std::span{expected}.first(prefix.size())));
      if (success) {
        EXPECT_EQ(prefix, expected);
      }
    }
    EXPECT_EQ(NodeTestPeer::RefCount(root), root_refs);
    EXPECT_EQ(NodeTestPeer::RefCount(leaves.front()), leaf_refs);
  }
  EXPECT_TRUE(success);
  EXPECT_GE(failure_prefixes.size(), 3);
  EXPECT_TRUE(std::ranges::any_of(failure_prefixes, [&](std::size_t n) {
    return n > 8 && n < expected.size();
  }));
}

TEST_F(Traversal, PostorderRetainsChildAndReturnsFullCallbackError) {
  Node kept;
  const NodeError expected{ErrorCode::kInvalidDatatype, ResourceKind::kSize,
                           std::uint64_t{1} << 40, 7,
                           DatatypeReason::kNonproductive};
  {
    const auto child = Take(manager_.MakeBool(true));
    const auto root = Op(Kind::kNot, {child.Borrow()});
    std::size_t calls = 0;
    const auto result = VisitDagPostorder(
        std::span{&root, 1}, [&](NodeView view) -> Result<void> {
          EXPECT_EQ(view, child.Borrow());
          kept = Take(manager_.RetainNode(view));
          ++calls;
          return std::unexpected(expected);
        });
    ASSERT_FALSE(result);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(result.error().code, expected.code);
    EXPECT_EQ(result.error().resource, expected.resource);
    EXPECT_EQ(result.error().operand_index, expected.operand_index);
    EXPECT_EQ(result.error().parameter_index, expected.parameter_index);
    EXPECT_EQ(result.error().datatype_reason, expected.datatype_reason);
  }
  EXPECT_EQ(kept.Borrow().GetValue()->GetBool(), true);
  EXPECT_EQ(NodeTestPeer::RefCount(kept), 1);
  EXPECT_EQ(NodeTestPeer::GetStats(manager_).live_nodes, 1);
}

}  // namespace dandelion::testing

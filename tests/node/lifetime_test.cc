#include "test_support.h"

namespace dandelion::testing {
namespace {

class Lifetime : public NodeTest {};

template <class Handle>
void CheckHandleSpecialMembers(const Handle& original) {
  const auto id = original.GetId();
  auto copy = original;
  EXPECT_EQ(copy.GetId(), id);
  const auto& alias = copy;
  copy = alias;
  auto& move_alias = copy;
  copy = std::move(move_alias);
  EXPECT_EQ(copy.GetId(), id);
  auto moved = std::move(copy);
  EXPECT_EQ(copy.GetId(), 0);
  EXPECT_EQ(moved.GetId(), id);
  copy = original;
  moved = std::move(copy);
  EXPECT_EQ(copy.GetId(), 0);
  EXPECT_EQ(moved.GetId(), id);
}

TEST_F(Lifetime, RepeatedEdgesCopyMovesAssignmentAndOwningChild) {
  auto x = Take(manager_.MakeBool(true));
  const auto initial = NodeTestPeer::RefCount(x);
  auto parent = Op(Kind::kAnd, {x.Borrow(), x.Borrow()});
  EXPECT_EQ(NodeTestPeer::RefCount(x), initial + 2);
  CheckHandleSpecialMembers(x);
  CheckHandleSpecialMembers(parent);
  auto child = Take(parent.Child(0));
  Error(parent.Child(2), ErrorCode::kInvalidParameter);
  parent = Node{};
  EXPECT_EQ(NodeTestPeer::RefCount(x), initial + 1);
  child = Node{};
  EXPECT_EQ(NodeTestPeer::RefCount(x), initial);
  auto old = Op(Kind::kNot, {x.Borrow()});
  const auto child_view = old.Borrow().GetChildren()[0];
  old = Take(manager_.RetainNode(child_view));
  EXPECT_EQ(old, x);  // Retain before releasing the old parent.
  const auto z = Take(manager_.IntSort());
  const auto u = Take(manager_.DeclareUninterpretedSort("U", 0));
  const auto d = Take(manager_.DeclareFunction("d", {}, z.Borrow()));
  const auto i = Take(manager_.InstantiateDeclaration(d.Borrow(), {}));
  CheckHandleSpecialMembers(z);
  CheckHandleSpecialMembers(u);
  CheckHandleSpecialMembers(d);
  CheckHandleSpecialMembers(i);
}

TEST_F(Lifetime, BatchReleaseValidatesEveryRootBeforeConsuming) {
  auto other = Take(NodeManager::Create());
  std::array roots = {Take(manager_.MakeBool(true)), Node{},
                      Take(other.MakeBool(true))};
  const auto local_id = roots[0].GetId();
  const auto foreign_id = roots[2].GetId();
  Error(manager_.ReleaseRoots(roots), ErrorCode::kForeignManager);
  EXPECT_EQ(roots[0].GetId(), local_id);
  EXPECT_EQ(roots[2].GetId(), foreign_id);
  NodeManager moved_from;
  Error(moved_from.ReleaseRoots(roots), ErrorCode::kNullObject);
  Take(manager_.ReleaseRoots(std::span{roots}.first(2)));
  EXPECT_EQ(roots[0].GetId(), 0);
  EXPECT_EQ(roots[1].GetId(), 0);
  EXPECT_EQ(roots[2].GetId(), foreign_id);
  Take(manager_.ReleaseRoots({}));
}

TEST_F(Lifetime, BatchReleasePreservesSharedEdgesAndReclaimsAllLastOwners) {
  const auto sort = Take(manager_.BoolSort());
  auto leaf = Take(manager_.MakeFreshConstant("symbol", sort.Borrow()));
  const auto instance =
      Take(manager_.RetainDeclInstance(*leaf.Borrow().GetDeclaration()));
  const auto original = leaf.GetId();
  auto parent = Op(Kind::kAnd, {leaf.Borrow(), leaf.Borrow()});
  auto other = Op(Kind::kNot, {leaf.Borrow()});
  const auto parent_id = parent.GetId();
  std::array roots = {std::move(leaf), parent, std::move(parent), Node{}};
  Take(manager_.ReleaseRoots(roots));
  for (const auto& consumed : roots) EXPECT_EQ(consumed.GetId(), 0);
  EXPECT_EQ(NodeTestPeer::GetStats(manager_).live_nodes, 2);
  auto borrowed = other.Borrow().GetChildren()[0];
  EXPECT_EQ(borrowed.GetId(), original);
  {
    const auto later = Op(Kind::kAnd, {borrowed, borrowed});
    EXPECT_GT(later.GetId(), parent_id);
    EXPECT_EQ(later, Op(Kind::kAnd, {borrowed, borrowed}));
  }
  Take(manager_.ReleaseRoots(std::span{&other, 1}));
  EXPECT_EQ(NodeTestPeer::GetStats(manager_).live_nodes, 0);
  EXPECT_EQ(instance.Borrow().GetDeclaration().GetNameHint(), "symbol");
  const auto new_leaf = Take(manager_.MakeApply(instance.Borrow(), {}));
  EXPECT_GT(new_leaf.GetId(), original);
}

TEST_F(Lifetime, BatchReleaseRebuildsSurvivorsWithAllocationFailureFallback) {
  for (const bool fail_allocation : {false, true}) {
    SCOPED_TRACE(fail_allocation);
    auto local = Take(NodeManager::Create());
    NodeTestPeer::ForceCollisions(local, UINT64_MAX);
    std::vector<Node> nodes;
    for (std::int64_t i = 0; i < 256; ++i)
      nodes.push_back(Take(local.MakeInt(IntegerView(i))));
    const auto before = NodeTestPeer::GetStats(local);
    if (fail_allocation) NodeTestPeer::FailAllocationAfter(local, 0);
    Take(local.ReleaseRoots(std::span{nodes}.subspan(4)));
    const auto after = NodeTestPeer::GetStats(local);
    EXPECT_EQ(after.live_nodes, 4);
    EXPECT_EQ(after.node_reclamations, 252);
    EXPECT_EQ(after.allocation_attempts, before.allocation_attempts + 1);
    if (!fail_allocation) {
      EXPECT_LT(after.table_capacity, before.table_capacity);
    }
    for (std::int64_t i = 0; i < 4; ++i)
      EXPECT_EQ(Take(local.MakeInt(IntegerView(i))),
                nodes[static_cast<std::size_t>(i)]);
    Take(local.ReleaseRoots(nodes));
    EXPECT_EQ(NodeTestPeer::GetStats(local).live_nodes, 0);
    EXPECT_EQ(NodeTestPeer::GetStats(local).node_reclamations, 256);
    EXPECT_NE(Take(local.MakeInt(IntegerView(1))).GetId(), 2);
  }
}

TEST_F(Lifetime, BatchReleaseMatchesSequentialReleaseWithMostlyLiveNodes) {
  const auto exercise = [](bool batch) {
    auto manager = Take(NodeManager::Create());
    std::vector<Node> survivors;
    for (std::int64_t i = 0; i < 48; ++i)
      survivors.push_back(Take(manager.MakeInt(IntegerView(i))));
    const std::array children = {survivors[0].Borrow(), survivors[0].Borrow()};
    auto parent =
        Take(manager.MakeBuiltin({Kind::kIntAdd, {}, {}, {}}, children));
    std::array roots = {survivors[0], parent, std::move(parent), survivors[47]};
    const auto before = NodeTestPeer::GetStats(manager);
    if (batch) {
      Take(manager.ReleaseRoots(roots));
    } else {
      for (auto& root : roots) root = {};
    }
    const auto after = NodeTestPeer::GetStats(manager);
    EXPECT_EQ(after.live_nodes, survivors.size());
    EXPECT_EQ(after.allocation_attempts, before.allocation_attempts);
    EXPECT_EQ(after.table_capacity, before.table_capacity);
    for (auto& survivor : survivors) {
      EXPECT_EQ(NodeTestPeer::RefCount(survivor), 1);
      survivor = {};
    }
    return NodeTestPeer::GetStats(manager).node_reclamations;
  };
  EXPECT_EQ(exercise(true), exercise(false));
}

TEST_F(Lifetime, DeepTwentyThousandChainWalkAndReleaseAreIterative) {
  auto root = Take(manager_.MakeBool(true));
  for (int i = 0; i < 20000; ++i) root = Op(Kind::kNot, {root.Borrow()});
  EXPECT_EQ(NodeTestPeer::GetStats(manager_).live_nodes, 20001);
  {
    const std::array roots = {root};
    auto walk = Take(DagWalker::Create(roots));
    std::size_t entered = 0;
    std::size_t exited = 0;
    while (const auto event = Take(walk.Next())) {
      if (event->kind == DagEventKind::kNodeEnter) ++entered;
      if (event->kind == DagEventKind::kNodeExit) ++exited;
    }
    EXPECT_EQ(entered, 20001);
    EXPECT_EQ(exited, 20001);
  }
  root = Node{};
  EXPECT_EQ(NodeTestPeer::GetStats(manager_).live_nodes, 0);
  EXPECT_EQ(NodeTestPeer::GetStats(manager_).node_reclamations, 20001);
}

TEST_F(Lifetime, FacadeBeforeAllHandleCategoriesAndFinalStateDestruction) {
  const auto before = NodeTestPeer::DestroyedStates();
  Node node;
  Sort sort;
  Decl decl;
  DeclInstance instance;
  SortConstructor ctor;
  DatatypeGroup group;
  ManagerRef external_owner;
  {
    auto local = Take(NodeManager::Create());
    const auto b = Take(local.BoolSort());
    sort = Take(local.MakeArraySort(b.Borrow(), b.Borrow()));
    node = Take(local.MakeFreshConstant("array", sort.Borrow()));
    decl = Take(local.DeclareFunction("f", {}, sort.Borrow()));
    instance = Take(local.InstantiateDeclaration(decl.Borrow(), {}));
    const std::array headers = {DatatypeHeader{"Color", 0}};
    auto builder = Take(local.BeginDatatypeGroup(headers));
    Take(builder.AddConstructor(0, "red", {}));
    group = Take(builder.Commit());
    ctor = Take(local.RetainSortConstructor(
        Take(group.Borrow().GetSortConstructor(0))));
    external_owner = Take(local.RetainState());
  }
  EXPECT_EQ(NodeTestPeer::DestroyedStates(), before);
  EXPECT_EQ(node.GetSort(), sort);
  EXPECT_EQ(decl.Borrow().GetNameHint(), "f");
  EXPECT_EQ(instance.Borrow().GetRange().GetId(), sort.GetId());
  EXPECT_EQ(ctor.Borrow().GetArity(), 0);
  CheckHandleSpecialMembers(group);
  auto attached = Take(NodeManager::Attach(external_owner));
  EXPECT_EQ(sort, Take(attached.RetainSort(sort.Borrow())));
  EXPECT_EQ(decl, Take(attached.RetainDeclaration(decl.Borrow())));
  EXPECT_EQ(instance, Take(attached.RetainDeclInstance(instance.Borrow())));
  EXPECT_EQ(group, Take(attached.RetainDatatypeGroup(group.Borrow())));
  attached = NodeManager{};
  external_owner = ManagerRef{};
  node = Node{};
  sort = Sort{};
  decl = Decl{};
  instance = DeclInstance{};
  group = DatatypeGroup{};
  EXPECT_EQ(NodeTestPeer::DestroyedStates(), before);
  ctor = SortConstructor{};
  EXPECT_EQ(NodeTestPeer::DestroyedStates(), before + 1);
}

TEST_F(Lifetime, LastNodeOwnsStateAndBorrowedChildCanBeRetained) {
  const auto before = NodeTestPeer::DestroyedStates();
  Node retained;
  {
    auto local = Take(NodeManager::Create());
    const auto leaf = Take(local.MakeBool(true));
    const std::array children = {leaf.Borrow()};
    const auto root =
        Take(local.MakeBuiltin({Kind::kNot, {}, {}, {}}, children));
    const std::array roots = {root};
    auto walker = Take(DagWalker::Create(roots));
    while (const auto event = Take(walker.Next())) {
      if (event->kind == DagEventKind::kNodeEnter &&
          event->node.GetKind() == Kind::kBoolValue) {
        retained = Take(local.RetainNode(event->node));
      }
    }
    Error(manager_.RetainNode(leaf.Borrow()), ErrorCode::kForeignManager);
    Error(local.RetainNode({}), ErrorCode::kNullObject);
  }
  EXPECT_EQ(NodeTestPeer::DestroyedStates(), before);
  EXPECT_EQ(retained.Borrow().GetValue()->GetBool(), true);
  EXPECT_EQ(View(retained.GetSort()).GetKind(), SortKind::kBool);
  retained = Node{};
  EXPECT_EQ(NodeTestPeer::DestroyedStates(), before + 1);
}

}  // namespace
}  // namespace dandelion::testing

namespace dandelion::testing {

TEST_F(Lifetime, BuilderOutlivesFacadeAndCommitTransfersItsStateReference) {
  const auto before = NodeTestPeer::DestroyedStates();
  DatatypeBuilder builder;
  {
    auto local = Take(NodeManager::Create());
    const std::array headers = {DatatypeHeader{"D", 0}};
    builder = Take(local.BeginDatatypeGroup(headers));
    NodeTestPeer::SetReferenceLimit(local, NodeTestPeer::StateRefCount(local));
  }
  EXPECT_EQ(NodeTestPeer::DestroyedStates(), before);
  Take(builder.AddConstructor(0, "base", {}));
  auto group = Take(builder.Commit());
  EXPECT_EQ(group.Borrow().GetDatatypeCount(), 1);
  group = DatatypeGroup{};
  EXPECT_EQ(NodeTestPeer::DestroyedStates(), before + 1);
}

TEST_F(Lifetime,
       WalkerAndFacadeMovesPreserveOwnershipAndReclamationDoesNotAllocate) {
  auto local = Take(NodeManager::Create());
  const auto leaf = Take(local.MakeBool(true));
  const std::array roots = {leaf};
  auto walker = Take(DagWalker::Create(roots));
  auto moved = std::move(walker);
  Error(walker.Next(), ErrorCode::kInvalidTraversalState);
  auto& alias = moved;
  moved = std::move(alias);
  EXPECT_EQ(Take(moved.Next())->kind, DagEventKind::kRootEnter);
  auto facade = std::move(local);
  Error(local.BoolSort(), ErrorCode::kNullObject);
  auto& facade_alias = facade;
  facade = std::move(facade_alias);
  const std::array children = {leaf.Borrow(), leaf.Borrow()};
  auto parent = Take(facade.MakeBuiltin({Kind::kAnd, {}, {}, {}}, children));
  const auto attempts = NodeTestPeer::GetStats(facade).allocation_attempts;
  NodeTestPeer::FailAllocationAfter(facade, 0);
  parent = Node{};
  EXPECT_EQ(NodeTestPeer::GetStats(facade).allocation_attempts, attempts);
  NodeTestPeer::ClearFailures(facade);
}

TEST_F(Lifetime, BatchAndSequentialReleaseAgreeWithFewOrNoSurvivors) {
  const auto exercise = [](bool batch, bool fail, bool keep_last) {
    auto local = Take(NodeManager::Create());
    auto leaf = Take(local.MakeBool(true));
    auto root = leaf;
    for (int i = 0; i < 128; ++i) {
      root = Take(local.MakeBuiltin({Kind::kAnd, {}, {}, {}},
                                    std::array{root.Borrow(), root.Borrow()}));
    }
    const auto id = leaf.GetId();
    auto survivor = keep_last ? leaf : Node{};
    std::array owners{std::move(root), std::move(leaf)};
    if (fail) NodeTestPeer::FailAllocationAfter(local, 0);
    const auto before = NodeTestPeer::GetStats(local);
    if (batch) {
      Take(local.ReleaseRoots(owners));
    } else {
      for (auto& owner : owners) owner = {};
    }
    NodeTestPeer::ClearFailures(local);
    const auto after = NodeTestPeer::GetStats(local);
    EXPECT_EQ(after.live_nodes, keep_last ? 1 : 0);
    EXPECT_EQ(after.node_reclamations, keep_last ? 128 : 129);
    EXPECT_EQ(after.allocation_attempts,
              before.allocation_attempts + (batch && keep_last ? 1 : 0));
    if (keep_last) {
      EXPECT_EQ(NodeTestPeer::RefCount(survivor), 1);
      EXPECT_EQ(Take(local.MakeBool(true)), survivor);
    } else {
      EXPECT_EQ(after.table_capacity, 0);
      EXPECT_GT(Take(local.MakeBool(true)).GetId(), id);
    }
    return after.node_reclamations;
  };
  for (const bool survivor : {false, true}) {
    EXPECT_EQ(exercise(true, false, survivor),
              exercise(false, false, survivor));
    EXPECT_EQ(exercise(true, true, survivor), exercise(false, false, survivor));
  }
}

TEST_F(Lifetime, EachMetadataOwnerCanBeTheLastStateReference) {
  const auto check = [](const auto& make_owner, const auto& query) {
    const auto before = NodeTestPeer::DestroyedStates();
    {
      const auto owner = [&] {
        auto local = Take(NodeManager::Create());
        return make_owner(local);
      }();
      EXPECT_EQ(NodeTestPeer::DestroyedStates(), before);
      query(owner);
    }
    EXPECT_EQ(NodeTestPeer::DestroyedStates(), before + 1);
  };
  check([](NodeManager& m) { return Take(m.IntSort()); },
        [](const Sort& owner) {
          EXPECT_EQ(owner.Borrow().GetKind(), SortKind::kInt);
        });
  check(
      [](NodeManager& m) {
        return Take(m.DeclareFunction("f", {}, View(Take(m.IntSort()))));
      },
      [](const Decl& owner) { EXPECT_EQ(owner.Borrow().GetNameHint(), "f"); });
  check(
      [](NodeManager& m) {
        const auto d =
            Take(m.DeclareFunction("f", {}, View(Take(m.IntSort()))));
        return Take(m.InstantiateDeclaration(d.Borrow(), {}));
      },
      [](const DeclInstance& owner) {
        EXPECT_EQ(owner.Borrow().GetRange().GetKind(), SortKind::kInt);
      });
  check(
      [](NodeManager& m) {
        auto builder =
            Take(m.BeginDatatypeGroup(std::array{DatatypeHeader{"D", 0}}));
        Take(builder.AddConstructor(0, "base", {}));
        return Take(builder.Commit());
      },
      [](const DatatypeGroup& owner) {
        EXPECT_EQ(owner.Borrow().GetDatatypeCount(), 1);
      });
  check([](NodeManager& m) { return Take(m.RetainState()); },
        [](const ManagerRef& owner) {
          auto attached = Take(NodeManager::Attach(owner));
          EXPECT_EQ(NodeTestPeer::StateRefCount(attached), 2);
          EXPECT_TRUE(attached.BoolSort());
        });
}

TEST_F(Lifetime, AttachAndChildOverflowLeaveOwnershipUnchanged) {
  const auto state = Take(manager_.RetainState());
  const auto child = Take(manager_.MakeBool(true));
  const auto parent = Op(Kind::kNot, {child.Borrow()});
  const auto state_refs = NodeTestPeer::StateRefCount(manager_);
  NodeTestPeer::SetReferenceLimit(manager_, state_refs);
  Resource(NodeManager::Attach(state), ResourceKind::kReferenceCount);
  EXPECT_EQ(NodeTestPeer::StateRefCount(manager_), state_refs);
  NodeTestPeer::SetReferenceLimit(manager_, NodeTestPeer::RefCount(child));
  Resource(parent.Child(0), ResourceKind::kReferenceCount);
  EXPECT_EQ(NodeTestPeer::RefCount(child), 2);
  NodeTestPeer::SetReferenceLimit(manager_, UINT64_MAX);
  EXPECT_EQ(Take(parent.Child(0)), child);
  EXPECT_TRUE(NodeManager::Attach(state));
}

TEST_F(Lifetime, BuilderMoveAssignmentPreservesStagedRecipeIdentity) {
  const std::array headers{DatatypeHeader{"Box", 1}};
  auto source = Take(manager_.BeginDatatypeGroup(headers));
  const auto param = Take(source.Parameter(0, 0));
  const auto array = Take(source.Array(param, param));
  auto target = Take(manager_.BeginDatatypeGroup(headers));
  target = std::move(source);
  Error(source.Commit(), ErrorCode::kBuilderClosed);
  Take(target.AddConstructor(0, "box",
                             std::array{DatatypeFieldSpec{"items", array}}));
  const auto group = Take(target.Commit());
  Error(target.Commit(), ErrorCode::kBuilderClosed);
  const auto z = Take(manager_.IntSort());
  const auto expected = Take(manager_.MakeArraySort(z.Borrow(), z.Borrow()));
  const auto declaration = Take(group.Borrow().GetConstructor(0, 0));
  const auto instance = Take(
      manager_.InstantiateDeclaration(declaration, std::array{z.Borrow()}));
  ASSERT_EQ(instance.Borrow().GetDomain().size(), 1);
  EXPECT_EQ(instance.Borrow().GetDomain()[0], expected.Borrow());
}

}  // namespace dandelion::testing

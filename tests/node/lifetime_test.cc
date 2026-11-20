#include "test_support.h"

namespace dandelion::testing {
namespace {

class Lifetime : public NodeTest {};

struct EntryA {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  Node term;
};

struct EntryB {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  int tag;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  Node body;
};

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

TEST_F(Lifetime, RootRangesReleaseDirectSlotsAndHeterogeneousMembers) {
  const auto state = Take(manager_.RetainState());
  std::array roots = {Int(1), Int(2)};
  std::array terms = {EntryA{Int(3)}, EntryA{Int(4)}};
  std::array bodies = {EntryB{11, Int(5)}, EntryB{22, Int(6)}};
  const std::span<Node> direct_roots = roots;
  const std::span<EntryA> term_records = terms;
  const std::span<EntryB> body_records = bodies;
  static_assert(noexcept(RootRange::From(direct_roots)));
  static_assert(noexcept(RootRange::From<&EntryA::term>(term_records)));
  static_assert(noexcept(RootRange::From<&EntryB::body>(body_records)));
  static_assert(
      requires(const ManagerRef& manager, std::span<const RootRange> ranges) {
        {
          NodeManager::ReleaseRoots(manager, ranges)
        } noexcept -> std::same_as<Result<void>>;
      });

  const std::array ranges = {RootRange::From(direct_roots),
                             RootRange::From<&EntryA::term>(term_records),
                             RootRange::From<&EntryB::body>(body_records)};
  const auto untouched = Int(7);
  const auto before = NodeTestPeer::GetStats(manager_).node_reclamations;
  Take(NodeManager::ReleaseRoots(state, ranges));
  for (const auto& root : roots) EXPECT_EQ(root.GetId(), 0);
  for (const auto& entry : terms) EXPECT_EQ(entry.term.GetId(), 0);
  for (const auto& entry : bodies) EXPECT_EQ(entry.body.GetId(), 0);
  EXPECT_EQ(bodies[0].tag, 11);
  EXPECT_EQ(bodies[1].tag, 22);
  EXPECT_EQ(Int(7), untouched);
  EXPECT_EQ(NodeTestPeer::GetStats(manager_).node_reclamations, before + 6);
}

TEST_F(Lifetime, MultiRangeReleaseRejectsNullManager) {
  const ManagerRef state;
  std::array terms = {EntryA{Int(1)}};
  const auto id = terms[0].term.GetId();
  const std::array ranges = {
      RootRange::From<&EntryA::term>(std::span<EntryA>{terms})};

  Error(NodeManager::ReleaseRoots(state, {}), ErrorCode::kNullObject);
  Error(NodeManager::ReleaseRoots(state, ranges), ErrorCode::kNullObject);
  ASSERT_EQ(terms[0].term.GetId(), id);
  EXPECT_EQ(NodeTestPeer::RefCount(terms[0].term), 1);
}

template <class Range, auto Member, class Record>
concept ProjectsRoot = requires(std::span<Record> records) {
  Range::template From<Member>(records);
};

using RootAccessor = Node* (*)(void*, std::size_t) noexcept;
static_assert(!std::is_aggregate_v<RootRange>);
static_assert(
    !std::is_constructible_v<RootRange, void*, std::size_t, RootAccessor>);
static_assert(!ProjectsRoot<RootRange, &EntryB::tag, EntryB>);
static_assert(!ProjectsRoot<RootRange, &EntryA::term, const EntryA>);
static_assert(
    !ProjectsRoot<RootRange, static_cast<Node EntryA::*>(nullptr), EntryA>);

TEST_F(Lifetime, MultiRangeReleaseRejectsForeignRootBeforeConsuming) {
  const auto state = Take(manager_.RetainState());
  auto other = Take(NodeManager::Create());
  std::array terms = {EntryA{Int(1)}, EntryA{}};
  std::array bodies = {EntryB{11, Int(2)},
                       EntryB{22, Take(other.MakeInt(IntegerView(1)))}};
  const auto first_id = terms[0].term.GetId();
  const auto second_id = bodies[0].body.GetId();
  const auto foreign_id = bodies[1].body.GetId();
  ASSERT_EQ(foreign_id, first_id);
  const std::array ranges = {
      RootRange::From<&EntryA::term>(std::span<EntryA>{terms}),
      RootRange::From<&EntryB::body>(std::span<EntryB>{bodies})};

  Error(NodeManager::ReleaseRoots(state, ranges), ErrorCode::kForeignManager);
  ASSERT_EQ(terms[0].term.GetId(), first_id);
  EXPECT_EQ(terms[1].term.GetId(), 0);
  ASSERT_EQ(bodies[0].body.GetId(), second_id);
  ASSERT_EQ(bodies[1].body.GetId(), foreign_id);
  EXPECT_EQ(NodeTestPeer::RefCount(terms[0].term), 1);
  EXPECT_EQ(NodeTestPeer::RefCount(bodies[0].body), 1);
  EXPECT_EQ(NodeTestPeer::RefCount(bodies[1].body), 1);
}

TEST_F(Lifetime, MultiRangeReleaseAcceptsEmptyRangesAndNullHandles) {
  const auto state = Take(manager_.RetainState());
  const auto untouched = Int(1);
  std::array bodies = {EntryB{11, {}}};
  const std::array ranges = {
      RootRange{}, RootRange::From<&EntryA::term>(std::span<EntryA>{}),
      RootRange::From<&EntryB::body>(std::span<EntryB>{bodies})};
  const auto before = NodeTestPeer::GetStats(manager_);

  Take(NodeManager::ReleaseRoots(state, {}));
  Take(NodeManager::ReleaseRoots(state, ranges));
  const auto after = NodeTestPeer::GetStats(manager_);
  EXPECT_EQ(after.live_nodes, before.live_nodes);
  EXPECT_EQ(after.node_reclamations, before.node_reclamations);
  EXPECT_EQ(after.allocation_attempts, before.allocation_attempts);
  EXPECT_EQ(bodies[0].body.GetId(), 0);
  EXPECT_EQ(bodies[0].tag, 11);
  EXPECT_EQ(Int(1), untouched);
}

TEST_F(Lifetime, MultiRangeReleasePreservesExternalOwnerAndSharedChild) {
  const auto state = Take(manager_.RetainState());
  auto leaf = Take(manager_.MakeBool(true));
  auto retiring = Op(Kind::kAnd, {leaf.Borrow(), leaf.Borrow()});
  const auto survivor = Op(Kind::kNot, {leaf.Borrow()});
  const auto leaf_id = leaf.GetId();
  const auto retiring_id = retiring.GetId();
  std::array terms = {EntryA{std::move(leaf)}, EntryA{retiring}, EntryA{}};
  std::array bodies = {EntryB{11, std::move(retiring)}, EntryB{22, survivor},
                       EntryB{33, {}}};
  const std::array ranges = {
      RootRange::From<&EntryA::term>(std::span<EntryA>{terms}), RootRange{},
      RootRange::From<&EntryB::body>(std::span<EntryB>{bodies})};

  Take(NodeManager::ReleaseRoots(state, ranges));
  for (const auto& entry : terms) EXPECT_EQ(entry.term.GetId(), 0);
  for (const auto& entry : bodies) EXPECT_EQ(entry.body.GetId(), 0);
  EXPECT_EQ(bodies[0].tag, 11);
  EXPECT_EQ(bodies[1].tag, 22);
  EXPECT_EQ(bodies[2].tag, 33);
  const auto after = NodeTestPeer::GetStats(manager_);
  ASSERT_EQ(after.live_nodes, 2);
  EXPECT_EQ(after.node_reclamations, 1);
  EXPECT_EQ(NodeTestPeer::RefCount(survivor), 1);
  const auto retained_leaf = Take(survivor.Child(0));
  EXPECT_EQ(retained_leaf.GetId(), leaf_id);
  // The surviving edge and the handle returned by Child each own a reference.
  EXPECT_EQ(NodeTestPeer::RefCount(retained_leaf), 2);
  EXPECT_EQ(Take(manager_.MakeBool(true)), retained_leaf);
  const auto recreated =
      Op(Kind::kAnd, {retained_leaf.Borrow(), retained_leaf.Borrow()});
  EXPECT_GT(recreated.GetId(), retiring_id);
}

TEST_F(Lifetime, MultiRangeReleaseDetachesGrownTableWhenAllNodesRetire) {
  const auto state = Take(manager_.RetainState());
  std::array<EntryA, 128> terms;
  std::int64_t value = 0;
  for (auto& entry : terms) entry.term = Int(value++);
  std::array bodies = {EntryB{11, Int(value)}};
  const auto last_id = bodies[0].body.GetId();
  const auto before = NodeTestPeer::GetStats(manager_);
  const std::array ranges = {
      RootRange::From<&EntryA::term>(std::span<EntryA>{terms}),
      RootRange::From<&EntryB::body>(std::span<EntryB>{bodies})};
  manager_ = NodeManager{};

  Take(NodeManager::ReleaseRoots(state, ranges));
  for (const auto& entry : terms) EXPECT_EQ(entry.term.GetId(), 0);
  EXPECT_EQ(bodies[0].body.GetId(), 0);
  auto attached = Take(NodeManager::Attach(state));
  const auto after = NodeTestPeer::GetStats(attached);
  EXPECT_EQ(after.live_nodes, 0);
  EXPECT_EQ(after.node_reclamations, terms.size() + bodies.size());
  EXPECT_EQ(after.table_capacity, 0);
  EXPECT_EQ(after.allocation_attempts, before.allocation_attempts);
  EXPECT_EQ(NodeTestPeer::StateRefCount(attached), 2);
  EXPECT_GT(Take(attached.MakeInt(IntegerView(value))).GetId(), last_id);
}

TEST_F(Lifetime, MultiRangeReleaseRebuildsWithAllocationFailureFallback) {
  for (const bool fail_allocation : {false, true}) {
    SCOPED_TRACE(fail_allocation);
    auto local = Take(NodeManager::Create());
    NodeTestPeer::ForceCollisions(local, UINT64_MAX);
    const auto state = Take(local.RetainState());
    std::vector<EntryA> terms;
    std::vector<EntryB> bodies;
    std::vector<std::pair<std::int64_t, Node>> survivors;
    for (std::int64_t value = 0; value < 256; ++value) {
      auto node = Take(local.MakeInt(IntegerView(value)));
      if (value % 64 == 32) {
        survivors.emplace_back(value, std::move(node));
      } else if (value < 128) {
        terms.push_back({std::move(node)});
      } else {
        bodies.push_back({11, std::move(node)});
      }
    }
    const auto first_id = terms.front().term.GetId();
    const auto last_id = bodies.back().body.GetId();
    const std::array ranges = {
        RootRange::From<&EntryA::term>(std::span<EntryA>{terms}),
        RootRange::From<&EntryB::body>(std::span<EntryB>{bodies})};
    const auto before = NodeTestPeer::GetStats(local);
    if (fail_allocation) NodeTestPeer::FailAllocationAfter(local, 0);

    const auto result = NodeManager::ReleaseRoots(state, ranges);
    NodeTestPeer::ClearFailures(local);
    Take(result);
    for (const auto& entry : terms) EXPECT_EQ(entry.term.GetId(), 0);
    for (const auto& entry : bodies) EXPECT_EQ(entry.body.GetId(), 0);
    const auto after = NodeTestPeer::GetStats(local);
    ASSERT_EQ(after.live_nodes, 4);
    EXPECT_EQ(after.node_reclamations, 252);
    EXPECT_EQ(after.allocation_attempts, before.allocation_attempts + 1);
    if (fail_allocation) {
      EXPECT_EQ(after.table_capacity, before.table_capacity);
    } else {
      EXPECT_LT(after.table_capacity, before.table_capacity);
    }
    for (const auto& [value, owner] : survivors) {
      EXPECT_EQ(NodeTestPeer::RefCount(owner), 1);
      EXPECT_EQ(Take(local.MakeInt(IntegerView(value))), owner);
    }
    EXPECT_GT(Take(local.MakeInt(IntegerView(0))).GetId(), first_id);
    EXPECT_GT(Take(local.MakeInt(IntegerView(255))).GetId(), last_id);
  }
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

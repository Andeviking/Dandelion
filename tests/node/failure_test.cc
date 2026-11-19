#include "test_support.h"

namespace dandelion::testing {
namespace {

class Failure : public NodeTest {};

TEST_F(Failure, NodeIdExhaustionKeepsHitsUsable) {
  NodeTestPeer::SetNextNodeId(manager_, UINT64_MAX);
  const auto last = Take(manager_.MakeBool(true));
  EXPECT_EQ(last.GetId(), UINT64_MAX);
  Resource(manager_.MakeBool(false), ResourceKind::kIdentitySpace);
  EXPECT_EQ(Take(manager_.MakeBool(true)), last);
}

TEST_F(Failure, ReferenceOverflowThrowsBeforeCopyAssignmentChangesDestination) {
  const auto a = Take(manager_.MakeBool(true));
  auto b = Take(manager_.MakeBool(false));
  const auto old_id = b.GetId();
  NodeTestPeer::SetReferenceLimit(manager_, 1);
  EXPECT_THROW(b = a, ReferenceLimit);
  EXPECT_EQ(b.GetId(), old_id);
  Resource(manager_.RetainNode(a.Borrow()), ResourceKind::kReferenceCount);
  EXPECT_THROW((void)a.GetSort(), ReferenceLimit);
  NodeTestPeer::SetReferenceLimit(manager_, UINT64_MAX);
  EXPECT_EQ(b.Borrow().GetValue()->GetBool(), false);
  const auto sort = Take(manager_.BoolSort());
  NodeTestPeer::SetReferenceLimit(manager_,
                                  NodeTestPeer::StateRefCount(manager_));
  EXPECT_THROW((void)Sort(sort), ReferenceLimit);
  Resource(manager_.RetainSort(sort.Borrow()), ResourceKind::kReferenceCount);
  Resource(manager_.RetainState(), ResourceKind::kReferenceCount);
  NodeTestPeer::SetReferenceLimit(manager_, UINT64_MAX);
  try {
    throw ReferenceLimit{};
  } catch (const ReferenceLimit& error) {
    EXPECT_NE(std::string_view(error.what()).size(), 0);
  }
}

TEST_F(Failure, EveryRetainBoundaryRollsBackRepeatedChildPrefix) {
  const auto child = Take(manager_.MakeBool(true));
  const std::array children = {child.Borrow(), child.Borrow()};
  const auto refs = NodeTestPeer::RefCount(child);
  const auto before = NodeTestPeer::GetStats(manager_);
  bool success = false;
  std::size_t failures = 0;
  for (std::size_t checkpoint = 0; checkpoint < 16; ++checkpoint) {
    NodeTestPeer::FailRetainAfter(manager_, checkpoint);
    auto result = manager_.MakeBuiltin({Kind::kAnd, {}, {}, {}}, children);
    NodeTestPeer::ClearFailures(manager_);
    if (result) {
      success = true;
      EXPECT_EQ(NodeTestPeer::RefCount(child), refs + 2);
      break;
    }
    ++failures;
    Resource(result, ResourceKind::kReferenceCount);
    EXPECT_EQ(NodeTestPeer::RefCount(child), refs);
    EXPECT_EQ(NodeTestPeer::GetStats(manager_).live_nodes, before.live_nodes);
    EXPECT_EQ(NodeTestPeer::GetStats(manager_).next_node_id,
              before.next_node_id);
  }
  EXPECT_TRUE(success);
  EXPECT_GE(failures, 3);  // State plus two separate child edges.
  EXPECT_EQ(NodeTestPeer::RefCount(child), refs);
}

TEST_F(Failure, WideIndexAndOperandRollbackPreserveLiveEntries) {
  const auto sort = Bv(8);
  const auto operand = Const(sort.Borrow());
  const std::array children{operand.Borrow()};
  const std::array indices{Take(Integer::ParseDecimal("18446744073709551617"))};
  const BuiltinOpView op{Kind::kBvRotateLeft, indices, {}, {}};
  const auto refs = NodeTestPeer::RefCount(operand);
  for (const auto resource :
       {ResourceKind::kCppAllocation, ResourceKind::kReferenceCount}) {
    const auto before = NodeTestPeer::GetStats(manager_);
    bool published = false;
    std::size_t failures = 0;
    for (std::size_t checkpoint = 0; checkpoint < 16; ++checkpoint) {
      if (resource == ResourceKind::kCppAllocation)
        NodeTestPeer::FailAllocationAfter(manager_, checkpoint);
      else
        NodeTestPeer::FailRetainAfter(manager_, checkpoint);
      const auto result = manager_.MakeBuiltin(op, children);
      NodeTestPeer::ClearFailures(manager_);
      if (result) {
        ASSERT_EQ(result->Borrow().GetChildren().size(), 1);
        EXPECT_EQ(result->Borrow().GetChildren()[0], operand.Borrow());
        ASSERT_EQ(result->Borrow().GetIndices().size(), 1);
        EXPECT_EQ(result->Borrow().GetIndices()[0].Borrow().Compare(
                      indices[0].Borrow()),
                  0);
        EXPECT_EQ(result->GetId(), before.next_node_id);
        EXPECT_EQ(NodeTestPeer::RefCount(operand), refs + 1);
        EXPECT_EQ(Take(manager_.MakeBuiltin(op, children)), *result);
        published = true;
        break;
      }
      Resource(result, resource);
      const auto after = NodeTestPeer::GetStats(manager_);
      EXPECT_EQ(after.next_node_id, before.next_node_id);
      EXPECT_EQ(after.live_nodes, before.live_nodes);
      EXPECT_EQ(NodeTestPeer::RefCount(operand), refs);
      ++failures;
    }
    EXPECT_GE(failures, 2);
    EXPECT_TRUE(published);
    EXPECT_EQ(NodeTestPeer::RefCount(operand), refs);
  }
}

TEST_F(Failure, EveryFreshConstantAllocationAndRetainIsAtomic) {
  const auto sort = Take(manager_.IntSort());
  for (bool allocation : {true, false}) {
    bool success = false;
    std::size_t failures = 0;
    for (std::size_t checkpoint = 0; checkpoint < 128; ++checkpoint) {
      const auto before = NodeTestPeer::GetStats(manager_);
      if (allocation)
        NodeTestPeer::FailAllocationAfter(manager_, checkpoint);
      else
        NodeTestPeer::FailRetainAfter(manager_, checkpoint);
      auto result =
          manager_.MakeFreshConstant(std::string(256, 'x'), sort.Borrow());
      NodeTestPeer::ClearFailures(manager_);
      if (result) {
        success = true;
        EXPECT_EQ(NodeTestPeer::GetStats(manager_).declarations,
                  before.declarations + 1);
        EXPECT_EQ(NodeTestPeer::GetStats(manager_).decl_instances,
                  before.decl_instances + 1);
        break;
      }
      ++failures;
      Resource(result, allocation ? ResourceKind::kCppAllocation
                                  : ResourceKind::kReferenceCount);
      const auto after = NodeTestPeer::GetStats(manager_);
      EXPECT_EQ(after.declarations, before.declarations);
      EXPECT_EQ(after.decl_instances, before.decl_instances);
      EXPECT_EQ(after.next_node_id, before.next_node_id);
      EXPECT_EQ(after.live_nodes, before.live_nodes);
    }
    EXPECT_TRUE(success);
    EXPECT_GT(failures, 0);
  }
}

TEST_F(Failure, StoreAndDatatypeAllocationCheckpointsPreservePublication) {
  const auto z = Take(manager_.IntSort());
  const auto b = Take(manager_.BoolSort());
  for (int operation = 0; operation < 5; ++operation) {
    bool success = false;
    std::size_t failures = 0;
    for (std::size_t checkpoint = 0; checkpoint < 128; ++checkpoint) {
      NodeTestPeer::FailAllocationAfter(manager_, checkpoint);
      Result<void> result;
      if (operation == 0) {
        auto value = manager_.MakeArraySort(z.Borrow(), b.Borrow());
        if (!value) result = std::unexpected(value.error());
      } else if (operation == 1) {
        auto value =
            manager_.DeclareUninterpretedSort(std::string(256, 'u'), 0);
        if (!value) result = std::unexpected(value.error());
      } else if (operation == 2) {
        const std::array domain = {z.Borrow(), b.Borrow()};
        auto value =
            manager_.DeclareFunction(std::string(256, 'f'), domain, b.Borrow());
        if (!value) result = std::unexpected(value.error());
      } else if (operation == 3) {
        auto value =
            manager_.BeginDatatypeGroup(std::array{DatatypeHeader{"D", 0}});
        if (!value) result = std::unexpected(value.error());
      } else {
        const auto large = Number("340282366920938463463374607431768211456");
        auto value = manager_.MakeInt(large.Borrow());
        if (!value) result = std::unexpected(value.error());
      }
      NodeTestPeer::ClearFailures(manager_);
      if (result) {
        success = true;
        break;
      }
      ++failures;
      Resource(result, ResourceKind::kCppAllocation);
      EXPECT_EQ(Take(manager_.IntSort()), z);
      EXPECT_EQ(NodeTestPeer::GetStats(manager_).live_nodes, 0);
    }
    EXPECT_TRUE(success);
    EXPECT_GT(failures, 0);
  }
  const std::array headers = {DatatypeHeader{"D", 0}};
  auto builder = Take(manager_.BeginDatatypeGroup(headers));
  Take(builder.AddConstructor(0, "base", {}));
  bool success = false;
  std::size_t failures = 0;
  for (std::size_t checkpoint = 0; checkpoint < 128; ++checkpoint) {
    const auto before = NodeTestPeer::GetStats(manager_);
    NodeTestPeer::FailAllocationAfter(manager_, checkpoint);
    auto result = builder.Commit();
    NodeTestPeer::ClearFailures(manager_);
    if (result) {
      success = true;
      break;
    }
    ++failures;
    Resource(result, ResourceKind::kCppAllocation);
    const auto after = NodeTestPeer::GetStats(manager_);
    EXPECT_EQ(after.declarations, before.declarations);
    EXPECT_EQ(after.sort_constructors, before.sort_constructors);
    EXPECT_EQ(after.datatype_groups, before.datatype_groups);
  }
  EXPECT_TRUE(success);
  EXPECT_GT(failures, 0);
}

TEST_F(Failure, CapacitySizeAndStoreIdentityLimitsAreChecked) {
  const auto z = Take(manager_.IntSort());
  NodeTestPeer::SetIdentityLimit(manager_, 1);
  Resource(manager_.BoolSort(), ResourceKind::kIdentitySpace);
  EXPECT_EQ(Take(manager_.IntSort()), z);
  NodeTestPeer::SetIdentityLimit(manager_, UINT32_MAX);
  NodeTestPeer::SetSizeLimit(manager_, 1);
  Resource(manager_.MakeBool(true), ResourceKind::kSize);
  NodeTestPeer::SetSizeLimit(manager_, std::numeric_limits<std::size_t>::max());
  NodeTestPeer::SetTableCapacityLimit(manager_, 16);
  std::vector<Node> nodes;
  for (int i = 0; i < 12; ++i) nodes.push_back(Int(i));
  const Integer next(12);
  Resource(manager_.MakeInt(next.Borrow()), ResourceKind::kSize);
  EXPECT_EQ(Int(0), nodes[0]);
}

TEST_F(Failure, TraversalAllocationFailureIsTerminalAndReleasesRoots) {
  const auto child = Take(manager_.MakeBool(true));
  const auto root = Op(Kind::kNot, {child.Borrow()});
  const std::array roots = {root};
  bool create_succeeded = false;
  for (std::size_t checkpoint = 0; checkpoint < 32; ++checkpoint) {
    const auto before = NodeTestPeer::RefCount(root);
    NodeTestPeer::FailAllocationAfter(manager_, checkpoint);
    auto result = DagWalker::Create(roots);
    NodeTestPeer::ClearFailures(manager_);
    if (result) {
      create_succeeded = true;
      break;
    }
    Resource(result, ResourceKind::kCppAllocation);
    EXPECT_EQ(NodeTestPeer::RefCount(root), before);
  }
  EXPECT_TRUE(create_succeeded);
  std::size_t failed_walks = 0;
  bool finished = false;
  for (std::size_t checkpoint = 0; checkpoint < 32; ++checkpoint) {
    auto walker = Take(DagWalker::Create(roots));
    NodeTestPeer::FailAllocationAfter(manager_, checkpoint);
    bool failed = false;
    while (true) {
      auto event = walker.Next();
      if (!event) {
        Resource(event, ResourceKind::kCppAllocation);
        failed = true;
        NodeTestPeer::ClearFailures(manager_);
        Error(walker.Next(), ErrorCode::kInvalidTraversalState);
        break;
      }
      if (!*event) break;
    }
    NodeTestPeer::ClearFailures(manager_);
    if (!failed) {
      finished = true;
      break;
    }
    ++failed_walks;
  }
  EXPECT_GT(failed_walks, 0);
  EXPECT_TRUE(finished);
}

}  // namespace
}  // namespace dandelion::testing

namespace dandelion::testing {
namespace {

template <class T>
Result<void> Discard(Result<T> result) {
  if (!result) return std::unexpected(result.error());
  return {};
}

TEST_F(Failure, EveryFactoryCheckpointFromAnIdenticalFreshState) {
  // Recreate setup at every countdown so caches/capacity cannot skip
  // checkpoints.
  for (int operation = 0; operation < 25; ++operation) {
    SCOPED_TRACE(operation);
    bool success = false;
    std::size_t failures = 0;
    for (std::size_t checkpoint = 0; checkpoint < 256; ++checkpoint) {
      auto local = Take(NodeManager::Create());
      const auto b = Take(local.BoolSort());
      const auto z = Take(local.IntSort());
      const auto rm_sort = Take(local.RoundingModeSort());
      const Integer eight(8), twenty_four(24), one(1), two(2);
      const auto v = Take(local.MakeBvSort(eight.Borrow()));
      const auto fp =
          Take(local.MakeFpSort(eight.Borrow(), twenty_four.Borrow()));
      const auto x = Take(local.MakeInt(one.Borrow()));
      const auto rm = Take(local.MakeRoundingMode(RoundingMode::kRne));
      const auto u = Take(local.DeclareUninterpretedSort("U", 1));
      const std::array domain = {z.Borrow(), b.Borrow()};
      const auto declaration =
          Take(local.DeclareFunction("f", domain, z.Borrow()));
      const std::array headers = {DatatypeHeader{"D", 1}};
      auto builder = Take(local.BeginDatatypeGroup(headers));
      const auto p = Take(builder.Parameter(0, 0));
      const auto concrete = Take(builder.Concrete(z.Borrow()));
      const std::array recipe_args = {p};
      const auto recursive = Take(builder.Recursive(0, recipe_args));
      Take(builder.AddConstructor(0, "base", {}));
      const std::array fields = {DatatypeFieldSpec{"head", p},
                                 DatatypeFieldSpec{"tail", recursive}};
      Take(builder.AddConstructor(0, "cons", fields));
      const auto large = Number("18446744073709551617");
      const auto rational = Take(Rational::Make(one.Borrow(), two.Borrow()));
      std::optional<DatatypeGroup> committed;
      std::optional<Decl> dt_declaration;
      std::vector<Node> cluster;
      if (operation == 22) {
        committed.emplace(Take(builder.Commit()));
        dt_declaration.emplace(Take(local.RetainDeclaration(
            Take(committed->Borrow().GetConstructor(0, 1)))));
      }
      if (operation == 24) {
        for (int i = 2; i < 12; ++i) {
          const Integer number(i);
          cluster.push_back(Take(local.MakeInt(number.Borrow())));
        }
      }
      const auto before = NodeTestPeer::GetStats(local);
      const auto x_refs = NodeTestPeer::RefCount(x);
      const auto state_refs = NodeTestPeer::StateRefCount(local);
      NodeTestPeer::FailAllocationAfter(local, checkpoint);
      Result<void> result;
      switch (operation) {
        case 0:
          result = Discard(local.MakeBool(true));
          break;
        case 1:
          result = Discard(local.MakeInt(large.Borrow()));
          break;
        case 2:
          result = Discard(local.MakeBvValue(v.Borrow(), one.Borrow()));
          break;
        case 3:
          result = Discard(local.MakeFpBits(fp.Borrow(), one.Borrow()));
          break;
        case 4:
          result = Discard(local.MakeFpSpecial(fp.Borrow(), FpSpecial::kNaN));
          break;
        case 5:
          result = Discard(local.MakeRoundingMode(RoundingMode::kRtz));
          break;
        case 6: {
          const std::array indices = {large};
          const std::array children = {x.Borrow()};
          result = Discard(local.MakeBuiltin(
              {Kind::kIntDivisible, indices, {}, {}}, children));
          break;
        }
        case 7: {
          const std::array indices = {eight, twenty_four};
          const std::array children = {rm.Borrow()};
          result = Discard(local.MakeBuiltin(
              {Kind::kFpFromRational, indices, {}, rational.Borrow()},
              children));
          break;
        }
        case 8:
          result = Discard(
              local.DeclareFunction(std::string(256, 'f'), domain, z.Borrow()));
          break;
        case 9:
          result = Discard(
              local.FreshFunction(std::string(256, 'f'), domain, z.Borrow()));
          break;
        case 10:
          result = Discard(local.MakeFormal(std::string(256, 'p'), z.Borrow()));
          break;
        case 11: {
          const std::array args = {v.Borrow()};
          result = Discard(local.InstantiateSort(u.Borrow(), args));
          break;
        }
        case 12:
          result =
              Discard(local.InstantiateDeclaration(declaration.Borrow(), {}));
          break;
        case 13:
          result = Discard(local.MakeArraySort(fp.Borrow(), v.Borrow()));
          break;
        case 14:
          result = Discard(local.BeginDatatypeGroup(headers));
          break;
        case 15:
          result = Discard(builder.Concrete(v.Borrow()));
          break;
        case 16:
          result = Discard(builder.Parameter(0, 0));
          break;
        case 17:
          result = Discard(builder.Array(concrete, p));
          break;
        case 18:
          result = Discard(builder.Apply(u.Borrow(), recipe_args));
          break;
        case 19:
          result = Discard(builder.Recursive(0, recipe_args));
          break;
        case 20:
          result = builder.AddConstructor(0, std::string(256, 'c'), fields);
          break;
        case 21:
          result = Discard(builder.Commit());
          break;
        case 22: {
          const std::array args = {v.Borrow()};
          result = Discard(
              local.InstantiateDeclaration(dt_declaration->Borrow(), args));
          break;
        }
        case 23:
          result = Discard(
              local.MakeFreshConstant(std::string(256, 'x'), v.Borrow()));
          break;
        case 24: {
          const Integer number(99);
          result = Discard(local.MakeInt(number.Borrow()));
          break;
        }
      }
      NodeTestPeer::ClearFailures(local);
      if (result) {
        success = true;
        break;
      }
      ++failures;
      Resource(result, ResourceKind::kCppAllocation);
      const auto after = NodeTestPeer::GetStats(local);
      EXPECT_EQ(after.live_nodes, before.live_nodes);
      EXPECT_EQ(after.next_node_id, before.next_node_id);
      EXPECT_EQ(after.declarations, before.declarations);
      EXPECT_EQ(after.decl_instances, before.decl_instances);
      EXPECT_EQ(after.sort_constructors, before.sort_constructors);
      EXPECT_EQ(after.datatype_groups, before.datatype_groups);
      EXPECT_EQ(NodeTestPeer::RefCount(x), x_refs);
      EXPECT_EQ(NodeTestPeer::StateRefCount(local), state_refs);
      EXPECT_EQ(x.Borrow().GetValue()->GetInteger()->ToDecimal(), "1");
    }
    EXPECT_TRUE(success);
    EXPECT_GT(failures, 0);
  }
}

}  // namespace
}  // namespace dandelion::testing

namespace dandelion::testing {

TEST_F(Failure, DatatypeCommitStagesItsImplicitBoolSortWithoutPartialIds) {
  bool success = false;
  std::size_t failures = 0;
  for (std::size_t checkpoint = 0; checkpoint < 128; ++checkpoint) {
    auto local = Take(NodeManager::Create());
    const std::array headers = {DatatypeHeader{"D", 0}};
    auto builder = Take(local.BeginDatatypeGroup(headers));
    Take(builder.AddConstructor(0, "base", {}));
    const auto before = NodeTestPeer::GetStats(local);
    NodeTestPeer::FailAllocationAfter(local, checkpoint);
    auto result = builder.Commit();
    NodeTestPeer::ClearFailures(local);
    if (result) {
      success = true;
      break;
    }
    ++failures;
    Resource(result, ResourceKind::kCppAllocation);
    const auto after = NodeTestPeer::GetStats(local);
    EXPECT_EQ(after.sorts, before.sorts);
    EXPECT_EQ(after.sort_constructors, before.sort_constructors);
    EXPECT_EQ(after.declarations, before.declarations);
    EXPECT_EQ(after.datatype_groups, before.datatype_groups);
  }
  EXPECT_TRUE(success);
  EXPECT_GT(failures, 0);
}

TEST_F(Failure, EveryStoreIdentitySpaceChecksItsOwnBoundaryAndKeepsHits) {
  const auto sort = Take(manager_.IntSort());
  const auto constructor = Take(manager_.DeclareUninterpretedSort("U", 0));
  const auto first = Take(manager_.DeclareFunction("a", {}, sort.Borrow()));
  const auto second = Take(manager_.DeclareFunction("b", {}, sort.Borrow()));
  const auto instance =
      Take(manager_.InstantiateDeclaration(first.Borrow(), {}));
  NodeTestPeer::SetIdentityLimit(manager_, 1);
  Resource(manager_.DeclareUninterpretedSort("V", 0),
           ResourceKind::kIdentitySpace);
  Resource(manager_.DeclareFunction("c", {}, sort.Borrow()),
           ResourceKind::kIdentitySpace);
  Resource(manager_.InstantiateDeclaration(second.Borrow(), {}),
           ResourceKind::kIdentitySpace);
  EXPECT_EQ(Take(manager_.InstantiateDeclaration(first.Borrow(), {})),
            instance);
  EXPECT_EQ(Take(manager_.IntSort()), sort);
  NodeTestPeer::SetIdentityLimit(manager_, UINT32_MAX);
  const std::array headers = {DatatypeHeader{"D", 0}};
  auto builder = Take(manager_.BeginDatatypeGroup(headers));
  Take(builder.AddConstructor(0, "base", {}));
  const auto group = Take(builder.Commit());
  auto next = Take(manager_.BeginDatatypeGroup(headers));
  Take(next.AddConstructor(0, "base", {}));
  NodeTestPeer::SetIdentityLimit(manager_, 1);
  const auto before = NodeTestPeer::GetStats(manager_);
  Resource(next.Commit(), ResourceKind::kIdentitySpace);
  EXPECT_EQ(NodeTestPeer::GetStats(manager_).datatype_groups,
            before.datatype_groups);
  NodeTestPeer::SetIdentityLimit(manager_, UINT32_MAX);
  EXPECT_EQ(group.Borrow().GetDatatypeCount(), 1);
}

TEST_F(Failure, WalkerRootRetainFailuresRollBackEveryCopiedPrefix) {
  const auto root = Take(manager_.MakeBool(true));
  const std::array roots = {root, root, root};
  const auto count = NodeTestPeer::RefCount(root);
  for (std::size_t prefix = 0; prefix < roots.size(); ++prefix) {
    NodeTestPeer::FailRetainAfter(manager_, prefix);
    const auto result = DagWalker::Create(roots);
    NodeTestPeer::ClearFailures(manager_);
    Resource(result, ResourceKind::kReferenceCount);
    EXPECT_EQ(NodeTestPeer::RefCount(root), count);
  }
}

}  // namespace dandelion::testing

namespace dandelion::testing {

TEST_F(Failure, ManagerCreationFailuresPublishNoStateOrFacade) {
  const auto before = NodeTestPeer::DestroyedStates();
  for (std::size_t checkpoint = 0; checkpoint < 2; ++checkpoint) {
    NodeTestPeer::FailCreationAllocationAfter(checkpoint);
    const auto result = NodeManager::Create();
    NodeTestPeer::ClearCreationFailure();
    Resource(result, ResourceKind::kCppAllocation);
    EXPECT_EQ(NodeTestPeer::DestroyedStates(), before);
  }
  {
    NodeTestPeer::FailCreationAllocationAfter(2);
    auto result = NodeManager::Create();
    NodeTestPeer::ClearCreationFailure();
    ASSERT_TRUE(result);
    EXPECT_TRUE(result->MakeBool(true));
  }
  EXPECT_EQ(NodeTestPeer::DestroyedStates(), before + 1);
}

TEST_F(Failure, WideContractsAndDiagnosticIndices) {
  static_assert(std::same_as<SortId, std::uint64_t>);
  static_assert(std::same_as<SortConstructorId, std::uint64_t>);
  static_assert(std::same_as<DeclId, std::uint64_t>);
  static_assert(std::same_as<DeclInstanceId, std::uint64_t>);
  static_assert(std::same_as<DatatypeGroupId, std::uint64_t>);
  static_assert(std::same_as<decltype(DagEvent::index), std::uint64_t>);
  static_assert(
      std::same_as<decltype(NodeError::operand_index), std::uint64_t>);
  static_assert(
      std::same_as<decltype(NodeError::parameter_index), std::uint64_t>);
  static_assert(
      std::same_as<decltype(DatatypeHeader::parameter_count), std::uint64_t>);
  static_assert(std::same_as<decltype(std::declval<SortRecipeView>().GetId()),
                             std::uint64_t>);
  static_assert(std::same_as<
                decltype(std::declval<DatatypeGroupView>().GetDatatypeCount()),
                std::uint64_t>);
  constexpr auto kArity = std::uint64_t{1} << 32;
  const auto wide_sort =
      Take(manager_.DeclareUninterpretedSort("wide", kArity));
  EXPECT_EQ(wide_sort.Borrow().GetArity(), kArity);
  Error(manager_.InstantiateSort(wide_sort.Borrow(), {}),
        ErrorCode::kInvalidArity);
  auto builder = Take(manager_.BeginDatatypeGroup(
      std::array{DatatypeHeader{"Wide", kArity + 1}}));
  EXPECT_TRUE(builder.Parameter(0, kArity));
  Error(builder.Parameter(0, kArity + 1), ErrorCode::kInvalidParameter);

  const auto bv8 = Const(View(Bv(8)));
  const auto bv16 = Const(View(Bv(16)));
  const auto bad_operand = manager_.CheckBuiltin(
      {Kind::kBvAdd, {}, {}, {}},
      std::array{bv8.Borrow(), bv8.Borrow(), bv16.Borrow()});
  ASSERT_FALSE(bad_operand);
  EXPECT_EQ(bad_operand.error().code, ErrorCode::kSortMismatch);
  EXPECT_EQ(bad_operand.error().operand_index, 2);
  EXPECT_EQ(bad_operand.error().parameter_index, UINT64_MAX);
  const std::array indices{Integer(5), Integer(-1)};
  const auto bad_parameter = manager_.MakeBuiltin(
      {Kind::kBvExtract, indices, {}, {}}, std::array{bv8.Borrow()});
  ASSERT_FALSE(bad_parameter);
  EXPECT_EQ(bad_parameter.error().code, ErrorCode::kInvalidParameter);
  EXPECT_EQ(bad_parameter.error().operand_index, UINT64_MAX);
  EXPECT_EQ(bad_parameter.error().parameter_index, 1);
  EXPECT_EQ(NodeError{}.operand_index, UINT64_MAX);
  EXPECT_EQ(NodeError{}.parameter_index, UINT64_MAX);
}

TEST_F(Failure, NumericPayloadCopiesPublishOnlyOnSuccess) {
  const auto wide = Number("18446744073709551617");
  const auto denominator = Number("18446744073709551616");
  const auto fraction =
      Take(Rational::Make(wide.Borrow(), denominator.Borrow()));
  const auto bv = Bv(65);
  const auto fp = Fp(2, 64);
  const auto bv_operand = Const(bv.Borrow());
  const auto rm = Take(manager_.MakeRoundingMode(RoundingMode::kRne));
  const std::array index{wide};
  const std::array format{Integer(2), Integer(64)};
  const auto check = [&](std::size_t expected_failures, const auto& make) {
    const auto before = NodeTestPeer::GetStats(manager_);
    const auto storage = Take(manager_.GetStorageStats());
    const auto state_refs = NodeTestPeer::StateRefCount(manager_);
    const auto bv_refs = NodeTestPeer::RefCount(bv_operand);
    const auto rm_refs = NodeTestPeer::RefCount(rm);
    for (std::size_t checkpoint = 0; checkpoint <= expected_failures;
         ++checkpoint) {
      SCOPED_TRACE(checkpoint);
      NumericTestPeer::FailAllocationAfter(checkpoint);
      const auto result = make();
      NumericTestPeer::ClearFailures();
      if (checkpoint == expected_failures) {
        ASSERT_TRUE(result);
        EXPECT_EQ(result->GetId(), before.next_node_id);
        EXPECT_EQ(Take(make()), *result);
      } else {
        Resource(result, ResourceKind::kCppAllocation);
        const auto after = NodeTestPeer::GetStats(manager_);
        EXPECT_EQ(after.next_node_id, before.next_node_id);
        EXPECT_EQ(after.live_nodes, before.live_nodes);
        EXPECT_EQ(Take(manager_.GetStorageStats()).numeric_backing_bytes,
                  storage.numeric_backing_bytes);
        EXPECT_EQ(NodeTestPeer::StateRefCount(manager_), state_refs);
        EXPECT_EQ(NodeTestPeer::RefCount(bv_operand), bv_refs);
        EXPECT_EQ(NodeTestPeer::RefCount(rm), rm_refs);
      }
    }
  };
  check(1, [&] { return manager_.MakeInt(wide.Borrow()); });
  check(1, [&] { return manager_.MakeBvValue(bv.Borrow(), wide.Borrow()); });
  check(1, [&] { return manager_.MakeFpBits(fp.Borrow(), wide.Borrow()); });
  check(1, [&] {
    return manager_.MakeBuiltin({Kind::kBvRotateLeft, index, {}, {}},
                                std::array{bv_operand.Borrow()});
  });
  check(2, [&] {
    return manager_.MakeBuiltin(
        {Kind::kFpFromRational, format, {}, fraction.Borrow()},
        std::array{rm.Borrow()});
  });
}

TEST_F(Failure, LateExternalChildRetainRollsBackEveryEdge) {
  const auto first = Take(manager_.MakeBool(false));
  const auto last = Take(manager_.MakeBool(true));
  std::vector<NodeView> children(65536, first.Borrow());
  children[32768] = last.Borrow();
  children.back() = last.Borrow();
  const BuiltinOpView op{Kind::kAnd, {}, {}, {}};
  const auto before = NodeTestPeer::GetStats(manager_);
  const auto refs = NodeTestPeer::StateRefCount(manager_);
  NodeTestPeer::FailRetainAfter(manager_, children.size());
  const auto failed = manager_.MakeBuiltin(op, children);
  NodeTestPeer::ClearFailures(manager_);
  Resource(failed, ResourceKind::kReferenceCount);
  EXPECT_EQ(NodeTestPeer::GetStats(manager_).next_node_id, before.next_node_id);
  EXPECT_EQ(NodeTestPeer::GetStats(manager_).live_nodes, before.live_nodes);
  EXPECT_EQ(Take(manager_.GetStorageStats()).live_edges, 0);
  EXPECT_EQ(NodeTestPeer::StateRefCount(manager_), refs);
  EXPECT_EQ(NodeTestPeer::RefCount(first), 1);
  EXPECT_EQ(NodeTestPeer::RefCount(last), 1);
  const auto node = Take(manager_.MakeBuiltin(op, children));
  EXPECT_EQ(node.GetId(), before.next_node_id);
  ExpectOrderedViews(node.Borrow().GetChildren(), children);
  EXPECT_EQ(NodeTestPeer::RefCount(last), 3);
}

TEST_F(Failure, DatatypeCommitExhaustsEachDestinationIndependently) {
  for (const auto exhausted : {"constructor", "declaration", "Bool"}) {
    SCOPED_TRACE(exhausted);
    auto local = Take(NodeManager::Create());
    const std::string_view store(exhausted);
    const auto z = Take(local.IntSort());
    Decl constructor;
    DeclInstance instance;
    if (store != "Bool") {
      auto prior =
          Take(local.BeginDatatypeGroup(std::array{DatatypeHeader{"Box", 1}}));
      const auto parameter = Take(prior.Parameter(0, 0));
      Take(prior.AddConstructor(
          0, "box", std::array{DatatypeFieldSpec{"value", parameter}}));
      const auto group = Take(prior.Commit());
      constructor = Take(
          local.RetainDeclaration(Take(group.Borrow().GetConstructor(0, 0))));
      instance = Take(local.InstantiateDeclaration(constructor.Borrow(),
                                                   std::array{z.Borrow()}));
    }
    constexpr std::uint64_t kLimit = 8;
    if (store == "constructor") {
      for (std::uint64_t i = 1; i < kLimit; ++i)
        Take(local.DeclareUninterpretedSort("U", 0));
    } else if (store == "declaration") {
      for (std::uint64_t i = 3; i < kLimit; ++i)
        Take(local.DeclareFunction("f", {}, z.Borrow()));
    } else {
      for (std::uint64_t i = 1; i < kLimit; ++i)
        Take(local.MakeBvSort(IntegerView::FromUnsigned(i)));
    }
    auto next =
        Take(local.BeginDatatypeGroup(std::array{DatatypeHeader{"D", 0}}));
    Take(next.AddConstructor(0, "base", {}));
    const auto before = NodeTestPeer::GetStats(local);
    NodeTestPeer::SetIdentityLimit(local, kLimit);
    const auto failed = next.Commit();
    Resource(failed, ResourceKind::kIdentitySpace);
    const auto after = NodeTestPeer::GetStats(local);
    EXPECT_EQ(after.sorts, before.sorts);
    EXPECT_EQ(after.sort_constructors, before.sort_constructors);
    EXPECT_EQ(after.declarations, before.declarations);
    EXPECT_EQ(after.datatype_groups, before.datatype_groups);
    EXPECT_EQ(after.decl_instances, before.decl_instances);
    const auto check_instance = [&] {
      if (store == "Bool") return;
      EXPECT_EQ(Take(local.InstantiateDeclaration(constructor.Borrow(),
                                                  std::array{z.Borrow()})),
                instance);
      EXPECT_EQ(instance.Borrow().GetDomain()[0], z.Borrow());
    };
    check_instance();
    NodeTestPeer::SetIdentityLimit(local, UINT64_MAX);
    const auto committed = Take(next.Commit());
    EXPECT_EQ(committed.GetId(), before.datatype_groups + 1);
    EXPECT_EQ(Take(committed.Borrow().GetSortConstructor(0)).GetId(),
              before.sort_constructors + 1);
    EXPECT_EQ(Take(committed.Borrow().GetConstructor(0, 0)).GetId(),
              before.declarations + 1);
    if (store == "Bool") {
      EXPECT_EQ(Take(local.BoolSort()).GetId(), before.sorts + 1);
    }
    check_instance();
  }
}

TEST(NumericCapacity, GmpImportsPowersShiftsAndProductsCheckLimbCapacity) {
  const auto wide = Number("18446744073709551617");
  const auto other = Number("18446744073709551619");
  const auto dividend = Take(Rational::Make(wide.Borrow(), IntegerView(1)));
  const auto divisor = Take(Rational::Make(IntegerView(1), other.Borrow()));
  const auto cancelling = Take(Rational::Make(wide.Borrow(), other.Borrow()));
  const auto one = Take(Rational::Make(IntegerView(1), IntegerView(1)));
  {
    struct ResetCapacity {
      ~ResetCapacity() {
        NumericTestPeer::SetLimbCapacity(std::numeric_limits<int>::max());
      }
    };
    const ResetCapacity reset;
    NumericTestPeer::SetLimbCapacity(2);
    EXPECT_EQ(Take(Integer::ParseDecimal("00042")).ToDecimal(), "42");
    Resource(Integer::ParseDecimal("18446744073709551617"),
             ResourceKind::kSize);
    Resource(Integer::ParseRadix("10000000000000001", 16), ResourceKind::kSize);
    EXPECT_EQ(Take(Integer::ParseRadix("00a", 16)).ToDecimal(), "10");
    Resource(RoundRationalToFp(8, 65, RoundingMode::kRne, one.Borrow()),
             ResourceKind::kSize);
    const auto eighth = Take(Rational::ParseDecimalFraction("0.125"));
    EXPECT_EQ(eighth.Borrow().Numerator().ToDecimal(), "1");
    EXPECT_EQ(eighth.Borrow().Denominator().ToDecimal(), "8");
    const auto cancelled =
        Take(Rational::Divide(cancelling.Borrow(), cancelling.Borrow()));
    EXPECT_EQ(cancelled.Borrow().Numerator().ToDecimal(), "1");
    EXPECT_EQ(cancelled.Borrow().Denominator().ToDecimal(), "1");
    NumericTestPeer::SetLimbCapacity(3);
    EXPECT_EQ(Take(Integer::ParseDecimal("18446744073709551617")).ToDecimal(),
              "18446744073709551617");
    Resource(Rational::Divide(dividend.Borrow(), divisor.Borrow()),
             ResourceKind::kSize);
    Resource(Rational::Divide(divisor.Borrow(), dividend.Borrow()),
             ResourceKind::kSize);
    NumericTestPeer::SetLimbCapacity(4);
    const std::string fraction = "0." + std::string(79, '0') + "1";
    Resource(Rational::ParseDecimalFraction(fraction), ResourceKind::kSize);
  }
  EXPECT_EQ(dividend.Borrow().Numerator().ToDecimal(), "18446744073709551617");
  EXPECT_EQ(View(Take(Rational::Divide(dividend.Borrow(), divisor.Borrow())))
                .Numerator()
                .BitLength(),
            129);
}

class NumericCapacityBoundary : public ::testing::Test {
 protected:
  void TearDown() override {
    NumericTestPeer::SetLimbCapacity(std::numeric_limits<int>::max());
  }
};

TEST_F(NumericCapacityBoundary, UnsignedRadixWordsRequireLimbCapacity) {
  const std::string binary = "1" + std::string(63, '0');
  NumericTestPeer::SetLimbCapacity(0);
  EXPECT_EQ(Take(Integer::ParseRadix("7fffffffffffffff", 16)).ToDecimal(),
            "9223372036854775807");
  Resource(Integer::ParseRadix("8000000000000000", 16), ResourceKind::kSize);
  Resource(Integer::ParseRadix(binary, 2), ResourceKind::kSize);

  NumericTestPeer::SetLimbCapacity(1);
  EXPECT_EQ(Take(Integer::ParseRadix("8000000000000000", 16)).ToDecimal(),
            "9223372036854775808");
  EXPECT_EQ(Take(Integer::ParseRadix(binary, 2)).ToDecimal(),
            "9223372036854775808");
  EXPECT_EQ(Take(Integer::ParseRadix("ffffffffffffffff", 16)).ToDecimal(),
            "18446744073709551615");
}

TEST_F(NumericCapacityBoundary, WideRadixImportsMatchGmpLimbSizing) {
  const std::string binary = "1" + std::string(127, '0');
  const std::string hex = "8" + std::string(31, '0');
  NumericTestPeer::SetLimbCapacity(2);
  Resource(Integer::ParseRadix(binary, 2), ResourceKind::kSize);
  Resource(Integer::ParseRadix(hex, 16), ResourceKind::kSize);

  NumericTestPeer::SetLimbCapacity(3);
  EXPECT_EQ(Take(Integer::ParseRadix(binary, 2)).ToDecimal(),
            "170141183460469231731687303715884105728");
  EXPECT_EQ(Take(Integer::ParseRadix(hex, 16)).ToDecimal(),
            "170141183460469231731687303715884105728");
}

TEST_F(NumericCapacityBoundary, DecimalImportsCountOnlySignificantDigits) {
  NumericTestPeer::SetLimbCapacity(2);
  EXPECT_EQ(Take(Integer::ParseDecimal("0009223372036854775808")).ToDecimal(),
            "9223372036854775808");
  Resource(Integer::ParseDecimal("18446744073709551617"), ResourceKind::kSize);
  const auto fraction =
      Take(Rational::ParseDecimalFraction("000922337203685477580.8000"));
  EXPECT_EQ(fraction.Borrow().Numerator().ToDecimal(), "4611686018427387904");
  EXPECT_EQ(fraction.Borrow().Denominator().ToDecimal(), "5");
  Resource(Rational::ParseDecimalFraction("1844674407370955161.7"),
           ResourceKind::kSize);

  NumericTestPeer::SetLimbCapacity(3);
  const auto wider =
      Take(Rational::ParseDecimalFraction("1844674407370955161.7"));
  EXPECT_EQ(wider.Borrow().Numerator().ToDecimal(), "18446744073709551617");
  EXPECT_EQ(wider.Borrow().Denominator().ToDecimal(), "10");
}

TEST_F(NumericCapacityBoundary, NearestRoundingChecksSubtractionSpareLimb) {
  const auto numerator = Number("18446744073709551615");
  const auto denominator = Number("18446744073709551617");
  const auto value =
      Take(Rational::Make(numerator.Borrow(), denominator.Borrow()));
  NumericTestPeer::SetLimbCapacity(2);
  Resource(RoundRationalToFp(8, 2, RoundingMode::kRne, value.Borrow()),
           ResourceKind::kSize);
  const auto truncated =
      Take(RoundRationalToFp(8, 2, RoundingMode::kRtz, value.Borrow()));
  EXPECT_FALSE(truncated.negative);
  EXPECT_FALSE(truncated.infinite);
  EXPECT_EQ(truncated.exponent.ToDecimal(), "126");
  EXPECT_EQ(truncated.fraction.ToDecimal(), "1");

  NumericTestPeer::SetLimbCapacity(3);
  const auto rounded =
      Take(RoundRationalToFp(8, 2, RoundingMode::kRne, value.Borrow()));
  EXPECT_FALSE(rounded.negative);
  EXPECT_FALSE(rounded.infinite);
  EXPECT_EQ(rounded.exponent.ToDecimal(), "127");
  EXPECT_EQ(rounded.fraction.ToDecimal(), "0");
  EXPECT_EQ(value.Borrow().Numerator().ToDecimal(), "18446744073709551615");
  EXPECT_EQ(value.Borrow().Denominator().ToDecimal(), "18446744073709551617");
}

TEST_F(NumericCapacityBoundary, RoundingChecksIncrementAndExponentSpareLimbs) {
  const auto seven_thirds =
      Take(Rational::Make(IntegerView(7), IntegerView(3)));
  const auto one = Take(Rational::Make(IntegerView(1), IntegerView(1)));
  NumericTestPeer::SetLimbCapacity(2);
  Resource(RoundRationalToFp(8, 65, RoundingMode::kRne, seven_thirds.Borrow()),
           ResourceKind::kSize);
  Resource(RoundRationalToFp(66, 2, RoundingMode::kRtz, one.Borrow()),
           ResourceKind::kSize);
  const auto truncated =
      Take(RoundRationalToFp(8, 65, RoundingMode::kRtz, seven_thirds.Borrow()));
  EXPECT_EQ(truncated.exponent.ToDecimal(), "128");
  EXPECT_EQ(truncated.fraction.ToDecimal(), "3074457345618258602");

  NumericTestPeer::SetLimbCapacity(3);
  const auto rounded =
      Take(RoundRationalToFp(8, 65, RoundingMode::kRne, seven_thirds.Borrow()));
  EXPECT_EQ(rounded.exponent.ToDecimal(), "128");
  EXPECT_EQ(rounded.fraction.ToDecimal(), "3074457345618258603");
  const auto wide_exponent =
      Take(RoundRationalToFp(66, 2, RoundingMode::kRtz, one.Borrow()));
  EXPECT_EQ(wide_exponent.exponent.ToDecimal(), "36893488147419103231");
  EXPECT_EQ(wide_exponent.fraction.ToDecimal(), "0");
}

TEST_F(NumericCapacityBoundary, FiniteOverflowChecksFractionLimbCapacity) {
  const auto four = Take(Rational::Make(IntegerView(4), IntegerView(1)));
  NumericTestPeer::SetLimbCapacity(2);
  Resource(RoundRationalToFp(2, 130, RoundingMode::kRtz, four.Borrow()),
           ResourceKind::kSize);

  NumericTestPeer::SetLimbCapacity(3);
  const auto largest_finite =
      Take(RoundRationalToFp(2, 130, RoundingMode::kRtz, four.Borrow()));
  EXPECT_FALSE(largest_finite.negative);
  EXPECT_FALSE(largest_finite.infinite);
  EXPECT_EQ(largest_finite.exponent.ToDecimal(), "2");
  EXPECT_EQ(largest_finite.fraction.ToDecimal(),
            "680564733841876926926749214863536422911");
}

}  // namespace dandelion::testing

#include "test_support.h"

namespace dandelion::testing {
namespace {

class Table : public NodeTest {};

TEST_F(Table, PlainHitSkipsShapeChecksWithoutAcceptingMissingFields) {
  NodeTestPeer::ForceCollisions(manager_, 15);
  const auto truth = Take(manager_.MakeBool(true));
  const std::array children{truth.Borrow(), truth.Borrow()};
  const BuiltinOpView plain{Kind::kAnd, {}, {}, {}};
  const auto node = Take(manager_.MakeBuiltin(plain, children));
  const auto before = NodeTestPeer::BuiltinShapeChecks(manager_);
  const auto types = NodeTestPeer::TypeEvaluations(manager_);
  EXPECT_EQ(Take(manager_.MakeBuiltin(plain, children)), node);
  EXPECT_EQ(NodeTestPeer::BuiltinShapeChecks(manager_), before);
  EXPECT_EQ(NodeTestPeer::TypeEvaluations(manager_), types);
  EXPECT_EQ(Take(manager_.CheckBuiltin(plain, children)), node.GetSort());
  EXPECT_EQ(NodeTestPeer::BuiltinShapeChecks(manager_), before + 1);
  const auto b = Take(manager_.BoolSort());
  const auto array = Take(manager_.MakeArraySort(b.Borrow(), b.Borrow()));
  const auto qualified =
      Op(Kind::kArrayConst, {truth.Borrow()}, {}, array.Borrow());
  const auto bv8 = Bv(8);
  const auto x = Const(bv8.Borrow());
  const std::array indices{Integer(7), Integer(0)};
  const auto indexed = Op(Kind::kBvExtract, {x.Borrow()}, indices);
  Error(manager_.MakeBuiltin({Kind::kArrayConst, {}, {}, {}},
                             std::array{truth.Borrow()}),
        ErrorCode::kInvalidParameter);
  Error(manager_.MakeBuiltin({Kind::kBvExtract, {}, {}, {}},
                             std::array{x.Borrow()}),
        ErrorCode::kInvalidParameter);
  Error(manager_.MakeBuiltin({Kind::kAnd, indices, {}, {}}, children),
        ErrorCode::kInvalidParameter);
  Error(manager_.MakeBuiltin(plain, std::span{children}.first(1)),
        ErrorCode::kInvalidArity);
  Error(manager_.MakeBuiltin({Kind::kApply, {}, {}, {}}, children),
        ErrorCode::kInvalidKind);
  auto other = Take(NodeManager::Create());
  const auto foreign = Take(other.MakeBool(true));
  Error(
      manager_.MakeBuiltin(plain, std::array{truth.Borrow(), foreign.Borrow()}),
      ErrorCode::kForeignManager);
  Error(manager_.MakeBuiltin(plain, std::array{truth.Borrow(), NodeView{}}),
        ErrorCode::kNullObject);
  EXPECT_EQ(Op(Kind::kArrayConst, {truth.Borrow()}, {}, array.Borrow()),
            qualified);
  EXPECT_EQ(Op(Kind::kBvExtract, {x.Borrow()}, indices), indexed);
}

TEST_F(Table, InputKeyHitReusesThePublishedTypeConclusion) {
  NodeTestPeer::ForceCollisions(manager_, 15);
  const auto bv8 = Bv(8);
  const auto x = Const(bv8.Borrow());
  const std::array children{x.Borrow()};
  const std::array indices{Integer(7), Integer(4)};
  const BuiltinOpView extract{Kind::kBvExtract, indices, {}, {}};
  const auto bv4 = Bv(4);
  const auto first = Take(manager_.MakeBuiltin(extract, children));
  const auto after_miss = NodeTestPeer::TypeEvaluations(manager_);
  EXPECT_EQ(first.GetSort(), bv4);
  EXPECT_EQ(Take(manager_.MakeBuiltin(extract, children)), first);
  EXPECT_EQ(NodeTestPeer::TypeEvaluations(manager_), after_miss);
  const std::array lower{Integer(3), Integer(0)};
  EXPECT_NE(Op(Kind::kBvExtract, {x.Borrow()}, lower), first);
  EXPECT_GT(NodeTestPeer::TypeEvaluations(manager_), after_miss);

  const auto bool_sort = Take(manager_.BoolSort());
  const auto int_sort = Take(manager_.IntSort());
  const auto bool_index =
      Take(manager_.MakeArraySort(bool_sort.Borrow(), bv4.Borrow()));
  const auto int_index =
      Take(manager_.MakeArraySort(int_sort.Borrow(), bv4.Borrow()));
  const auto a =
      Op(Kind::kArrayConst, {first.Borrow()}, {}, bool_index.Borrow());
  const auto b =
      Op(Kind::kArrayConst, {first.Borrow()}, {}, int_index.Borrow());
  EXPECT_NE(a, b);
  EXPECT_EQ(a.GetSort(), bool_index);
  EXPECT_EQ(b.GetSort(), int_index);
  const auto function = Take(
      manager_.DeclareFunction("f", std::array{bv8.Borrow()}, bv4.Borrow()));
  const auto instance =
      Take(manager_.InstantiateDeclaration(function.Borrow(), {}));
  const auto f = Take(manager_.MakeApply(instance.Borrow(), children));
  const auto after_apply = NodeTestPeer::TypeEvaluations(manager_);
  EXPECT_EQ(Take(manager_.MakeApply(instance.Borrow(), children)), f);
  EXPECT_EQ(NodeTestPeer::TypeEvaluations(manager_), after_apply);

  auto other = Take(NodeManager::Create());
  const auto foreign_sort = Take(other.MakeBvSort(IntegerView(8)));
  const auto foreign =
      Take(other.MakeFreshConstant("x", foreign_sort.Borrow()));
  Error(manager_.MakeBuiltin(extract, std::array{foreign.Borrow()}),
        ErrorCode::kForeignManager);
  Error(manager_.MakeBuiltin(extract, std::array{NodeView{}}),
        ErrorCode::kNullObject);
  Error(manager_.MakeBuiltin(
            {Kind::kBvExtract, indices, int_index.Borrow(), {}}, children),
        ErrorCode::kInvalidParameter);
  EXPECT_EQ(Take(manager_.MakeBuiltin(extract, children)), first);
}

TEST_F(Table, StorageStatsMeasureNodeBytesEdgesAndResizePeak) {
  NodeManager null_manager;
  Error(null_manager.GetStorageStats(), ErrorCode::kNullObject);
  const auto empty = manager_.GetStorageStats().value();
  EXPECT_EQ(empty.live_nodes, 0);
  EXPECT_EQ(empty.node_bytes, 0);
  std::vector<Node> nodes;
  for (int i = 0; i < 13; ++i) nodes.push_back(Int(i));
  const auto grown = manager_.GetStorageStats().value();
  EXPECT_EQ(grown.live_nodes, 13);
  EXPECT_EQ(grown.live_edges, 0);
  EXPECT_GT(grown.node_bytes, 13 * sizeof(Node));
  EXPECT_EQ(grown.table_bytes, 2 * empty.table_bytes);
  EXPECT_EQ(grown.peak_table_bytes, empty.table_bytes + grown.table_bytes);
}

TEST_F(Table, CompactionPreservesLookupAndEmptyTableReleasesWithoutAllocating) {
  NodeManager null_manager;
  Error(null_manager.CompactNodeTable(), ErrorCode::kNullObject);
  NodeTestPeer::ForceCollisions(manager_, 15);
  std::vector<Node> nodes;
  for (int i = 0; i < 100; ++i) nodes.push_back(Int(i));
  nodes.resize(20);
  const auto before = manager_.GetStorageStats().value();
  NodeTestPeer::FailAllocationAfter(manager_, 0);
  const auto failed = manager_.CompactNodeTable();
  NodeTestPeer::ClearFailures(manager_);
  Resource(failed, ResourceKind::kCppAllocation);
  EXPECT_EQ(manager_.GetStorageStats()->table_bytes, before.table_bytes);
  Take(manager_.CompactNodeTable());
  EXPECT_LT(manager_.GetStorageStats()->table_bytes, before.table_bytes);
  for (int i = 0; i < 20; ++i)
    EXPECT_EQ(Int(i), nodes[static_cast<std::size_t>(i)]);
  const auto old_id = nodes[0].GetId();
  NodeTestPeer::FailAllocationAfter(manager_, 0);
  nodes.clear();
  EXPECT_EQ(manager_.GetStorageStats()->table_bytes, 0);
  auto failed_reuse = manager_.MakeInt(IntegerView(0));
  NodeTestPeer::ClearFailures(manager_);
  Resource(failed_reuse, ResourceKind::kCppAllocation);
  EXPECT_GT(Int(0).GetId(), old_id);
}

TEST_F(Table, WideAndInlineChildCountsPreserveExactArity) {
  const auto first = Take(manager_.MakeBool(true));
  const auto last = Take(manager_.MakeBool(false));
  const auto previous_bytes = manager_.GetStorageStats()->node_bytes;
  for (const auto count : std::array<std::size_t, 3>{65534, 65535, 65536}) {
    std::vector<NodeView> children(count, first.Borrow());
    children.back() = last.Borrow();
    auto node = Take(manager_.MakeBuiltin({Kind::kAnd, {}, {}, {}}, children));
    EXPECT_EQ(Take(manager_.MakeBuiltin({Kind::kAnd, {}, {}, {}}, children)),
              node);
    ASSERT_EQ(node.Borrow().GetChildren().size(), count);
    EXPECT_EQ(node.Borrow().GetChildren()[count - 1], last.Borrow());
    EXPECT_EQ(Take(node.Child(count - 1)), last);
    const auto live = manager_.GetStorageStats().value();
    EXPECT_EQ(live.live_edges, count);
    EXPECT_GE(live.node_bytes - previous_bytes, count * sizeof(Node));
    node = {};
    EXPECT_EQ(NodeTestPeer::RefCount(first), 1);
    EXPECT_EQ(manager_.GetStorageStats()->node_bytes, previous_bytes);
  }
}

TEST_F(Table, VacantSlotIsReusedUntilGrowth) {
  NodeTestPeer::ForceCollisions(manager_, 15);
  std::vector<Node> nodes;
  for (int i = 0; i < 12; ++i) nodes.push_back(Int(i));
  EXPECT_EQ(NodeTestPeer::GetStats(manager_).insertion_searches, 0);

  // The next miss grows the table. A failed candidate still leaves the
  // resized table valid and the next miss can reuse its new vacant slot.
  NodeTestPeer::FailAllocationAfter(manager_, 1);
  const auto failed = manager_.MakeInt(IntegerView(12));
  NodeTestPeer::ClearFailures(manager_);
  Resource(failed, ResourceKind::kCppAllocation);
  EXPECT_EQ(NodeTestPeer::GetStats(manager_).table_capacity, 32);
  EXPECT_EQ(NodeTestPeer::GetStats(manager_).insertion_searches, 1);
  nodes.push_back(Int(12));
  EXPECT_EQ(NodeTestPeer::GetStats(manager_).insertion_searches, 1);
  for (int i = 0; i <= 12; ++i)
    EXPECT_EQ(Int(i), nodes[static_cast<std::size_t>(i)]);
}

TEST_F(Table, ThresholdGrowthAndWrapAroundBackwardShiftAgainstReference) {
  NodeTestPeer::ForceCollisions(manager_, 15);
  EXPECT_EQ(NodeTestPeer::GetStats(manager_).table_capacity, 16);
  std::map<int, Node> reference;
  for (int i = 0; i < 12; ++i) reference.emplace(i, Int(i));
  EXPECT_EQ(NodeTestPeer::GetStats(manager_).table_capacity, 16);
  const auto stable = View(reference.at(7));
  reference.emplace(12, Int(12));
  EXPECT_EQ(NodeTestPeer::GetStats(manager_).table_capacity, 32);
  EXPECT_EQ(stable, View(reference.at(7)));
  for (int i = 13; i < 80; ++i) reference.emplace(i, Int(i));
  for (const int key : {0, 40, 79, 1, 39, 78, 20, 60}) {
    const auto old_id = reference.at(key).GetId();
    reference.erase(key);
    for (const auto& [value, node] : reference) EXPECT_EQ(Int(value), node);
    auto reconstructed = Int(key);
    EXPECT_GT(reconstructed.GetId(), old_id);
    reference.emplace(key, std::move(reconstructed));
  }
  std::mt19937 random(0x51a7u);
  for (int step = 0; step < 200; ++step) {
    const auto key = static_cast<int>(random() % 80);
    reference.erase(key);
    for (const auto& [value, node] : reference) EXPECT_EQ(Int(value), node);
    reference.emplace(key, Int(key));
  }
  reference.clear();
  EXPECT_EQ(NodeTestPeer::GetStats(manager_).live_nodes, 0);
}

TEST_F(Table, FailureDuringGrowthAndAfterGrowthPreservesLiveNodes) {
  std::vector<Node> nodes;
  for (int i = 0; i < 12; ++i) nodes.push_back(Int(i));
  bool reached_success = false;
  for (std::size_t checkpoint = 0; checkpoint < 32; ++checkpoint) {
    const auto before = NodeTestPeer::GetStats(manager_);
    NodeTestPeer::FailAllocationAfter(manager_, checkpoint);
    const Integer number(99);
    auto result = manager_.MakeInt(number.Borrow());
    NodeTestPeer::ClearFailures(manager_);
    if (result) {
      reached_success = true;
      break;
    }
    Resource(result, ResourceKind::kCppAllocation);
    EXPECT_EQ(NodeTestPeer::GetStats(manager_).live_nodes, before.live_nodes);
    EXPECT_EQ(NodeTestPeer::GetStats(manager_).next_node_id,
              before.next_node_id);
    for (int i = 0; i < 12; ++i)
      EXPECT_EQ(nodes[static_cast<std::size_t>(i)], Int(i));
  }
  EXPECT_TRUE(reached_success);
}

}  // namespace

TEST_F(Table, MixedHomeWraparoundDeletionPreservesEverySurvivor) {
  // Acquire values by their natural home slot, then insert only the fixture.
  std::vector<std::int64_t> at_fifteen;
  std::optional<std::int64_t> at_one;
  for (std::int64_t candidate = 0; candidate < 4096; ++candidate) {
    const auto node = Int(candidate);
    const auto home = NodeTestPeer::StructuralHash(node) & 15;
    if (home == 15 && at_fifteen.size() < 3) at_fifteen.push_back(candidate);
    if (home == 1) at_one = candidate;
    if (at_fifteen.size() == 3 && at_one) break;
  }
  ASSERT_EQ(at_fifteen.size(), 3);
  ASSERT_TRUE(at_one);
  const std::array keys{at_fifteen[0], at_fifteen[1], *at_one, at_fifteen[2]};
  std::vector<Node> nodes;
  for (const auto key : keys) nodes.push_back(Int(key));
  EXPECT_EQ(NodeTestPeer::GetStats(manager_).table_capacity, 16);
  const auto erased = nodes.front().GetId();
  nodes.front() = {};
  for (std::size_t i = 1; i < keys.size(); ++i)
    EXPECT_EQ(Int(keys[i]), nodes[i]);
  nodes.front() = Int(keys.front());
  EXPECT_GT(nodes.front().GetId(), erased);
  EXPECT_GT(nodes.front().GetId(), nodes.back().GetId());
  for (std::size_t i = 0; i < keys.size(); ++i)
    EXPECT_EQ(Int(keys[i]), nodes[i]);
}

TEST_F(Table, FailedCompactionKeepsHitsAndGrowthAllowsReinterning) {
  std::vector<Node> nodes;
  for (int i = 0; i < 96; ++i) nodes.push_back(Int(i));
  const auto old_id = nodes.back().GetId();
  nodes.resize(3);
  NodeTestPeer::FailAllocationAfter(manager_, 0);
  const auto compact = manager_.CompactNodeTable();
  NodeTestPeer::ClearFailures(manager_);
  Resource(compact, ResourceKind::kCppAllocation);
  for (int i = 0; i < 3; ++i)
    EXPECT_EQ(Int(i), nodes[static_cast<std::size_t>(i)]);
  const auto capacity = NodeTestPeer::GetStats(manager_).table_capacity;
  for (int i = 100; i < 100 + static_cast<int>(capacity); ++i)
    nodes.push_back(Int(i));
  EXPECT_GT(NodeTestPeer::GetStats(manager_).table_capacity, capacity);
  const auto restored = Int(95);
  EXPECT_GT(restored.GetId(), old_id);
  EXPECT_EQ(restored, Int(95));
  for (int i = 0; i < 3; ++i)
    EXPECT_EQ(Int(i), nodes[static_cast<std::size_t>(i)]);
}

TEST_F(Table, StructuredWideIntegersDoNotCollapseNaturalProbes) {
  constexpr std::uint64_t kMultiplier = 0x9e3779b185ebca87ULL;
  // The old hash packed Int sort ID 1 with zero children, then folded the
  // kind and positive two-chunk marker. Choosing high equal to the state
  // after low made the following XOR cancel for every value.
  constexpr auto kOldPrefix =
      ((((std::uint64_t{1} << 32) ^ std::to_underlying(Kind::kIntValue)) *
        kMultiplier) ^
       4) *
      kMultiplier;
  constexpr std::uint64_t kCount = 256;
  std::vector<Integer> values;
  std::vector<Node> nodes;
  const auto before = NodeTestPeer::GetStats(manager_);
  for (std::uint64_t low = 0; low < kCount; ++low) {
    const auto high = (kOldPrefix ^ low) * kMultiplier;
    ASSERT_NE(high, 0);
    values.push_back(
        Take(Integer::ParseRadix(std::format("{:x}{:016x}", high, low), 16)));
    nodes.push_back(Take(manager_.MakeInt(values.back().Borrow())));
  }
  for (std::size_t i = 0; i < values.size(); ++i)
    EXPECT_EQ(Take(manager_.MakeInt(values[i].Borrow())), nodes[i]);
  const auto after = NodeTestPeer::GetStats(manager_);
  EXPECT_EQ(after.live_nodes, kCount);
  EXPECT_LT(after.probe_slots - before.probe_slots, 32 * kCount);
  EXPECT_LT(after.probe_max, kCount / 2);
}

}  // namespace dandelion::testing

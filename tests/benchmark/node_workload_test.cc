#include <gtest/gtest.h>

import std;
import dandelion.node;
import dandelion.node_workload;

namespace dandelion::node_bench {
namespace {

Workload Read(std::string_view input) {
  std::istringstream stream{std::string(input)};
  return Workload::Read(stream);
}

TEST(NodeWorkloadTest, FlatRecordsKeepWideMetadataAndOrderedOperands) {
  const auto workload = Read(R"(dandelion-node 1 graph
sort 4294967300 bv 8
sort 4294967301 bv 4
make 0 4294967300 kBvValue 0 165
make 1 4294967300 kBvNot 1 0
make 2 4294967301 kBvExtract 1 0 7 4
hold 3 1
drop 0
drop 1
roots 3 2
end
)");
  Replay replay(workload, true);
  EXPECT_GT(workload.StorageBytes(), 0);
  EXPECT_GT(replay.StorageBytes(), 0);
  EXPECT_EQ(replay.GetManager().GetStorageStats()->live_nodes, 0);
  replay.Run();
  auto roots = replay.TakeRoots();
  ASSERT_EQ(roots.size(), 2);
  const auto a = roots[0].Borrow().GetChildren()[0];
  const auto b = roots[1].Borrow().GetChildren()[0];
  EXPECT_EQ(a, b);
  EXPECT_EQ(a.GetValue()->GetInteger()->ToDecimal(), "165");
  EXPECT_EQ(roots[1].Borrow().GetIndices()[0].ToDecimal(), "7");
  EXPECT_EQ(roots[1].Borrow().GetIndices()[1].ToDecimal(), "4");
  const auto live = replay.GetManager().GetStorageStats().value();
  EXPECT_EQ(live.live_nodes, 3);
  EXPECT_EQ(live.live_edges, 2);
  EXPECT_GT(live.node_bytes, 0);
  roots[0] = {};
  const auto partial = replay.GetManager().GetStorageStats().value();
  EXPECT_EQ(partial.live_nodes, 2);
  EXPECT_EQ(partial.live_edges, 1);
  EXPECT_LT(partial.node_bytes, live.node_bytes);
  roots.clear();
  const auto released = replay.GetManager().GetStorageStats().value();
  EXPECT_EQ(released.live_nodes, 0);
  EXPECT_EQ(released.live_edges, 0);
  EXPECT_EQ(released.node_bytes, 0);
  EXPECT_EQ(released.metadata_bytes, live.metadata_bytes);
}

TEST(NodeWorkloadTest, OrderedWalkerEventsAndCascadingRelease) {
  const auto workload = Read(R"(dandelion-node 1 graph
sort 1 bool
decl 1 function 1
inst 1 1
make 0 1 kApply 0 1
make 1 1 kAnd 2 0 0
make 2 1 kNot 1 0
drop 0
hold 3 1
roots 1 2 3
end
)");
  Replay replay(workload, true);
  replay.Run();
  EXPECT_TRUE(replay.GetNode(0).IsNull());
  auto roots = replay.TakeRoots();
  const NodeId a = roots[0].Borrow().GetChildren()[0].GetId();
  const NodeId b = roots[0].GetId();
  const NodeId c = roots[1].GetId();
  EXPECT_EQ(roots[2].GetId(), b);
  using enum DagEventKind;
  const auto stats = Walk(roots);
  EXPECT_EQ(stats.Roots(), 3);
  EXPECT_EQ(stats.Nodes(), 3);
  EXPECT_EQ(stats.Edges(), 3);
  EXPECT_EQ(stats.Depth(), 2);
  EXPECT_EQ(stats.MaxFanout(), 2);
  {
    auto walker = DagWalker::Create(roots);
    ASSERT_TRUE(walker);
    static_assert(std::same_as<decltype(DagEvent::index), std::uint64_t>);
    using Event = std::tuple<DagEventKind, NodeId, std::uint64_t>;
    constexpr auto kNoIndex = std::numeric_limits<std::uint64_t>::max();
    const std::vector<Event> expected{
        {kRootEnter, b, 0},       {kNodeEnter, b, kNoIndex},
        {kEdgeEnter, a, 0},       {kNodeEnter, a, kNoIndex},
        {kNodeExit, a, kNoIndex}, {kEdgeExit, a, 0},
        {kEdgeEnter, a, 1},       {kEdgeExit, a, 1},
        {kNodeExit, b, kNoIndex}, {kRootExit, b, 0},
        {kRootEnter, c, 1},       {kNodeEnter, c, kNoIndex},
        {kEdgeEnter, a, 0},       {kEdgeExit, a, 0},
        {kNodeExit, c, kNoIndex}, {kRootExit, c, 1},
        {kRootEnter, b, 2},       {kRootExit, b, 2}};
    for (const auto& item : expected) {
      const auto event = walker->Next();
      ASSERT_TRUE(event && event->has_value());
      const auto& actual = **event;
      EXPECT_EQ((Event{actual.kind, actual.node.GetId(), actual.index}), item);
    }
    const auto last = walker->Next();
    ASSERT_TRUE(last);
    EXPECT_FALSE(last->has_value());
  }
  EXPECT_EQ(NodeTestPeer::GetStats(replay.GetManager()).live_nodes, 3);
  roots[0] = {};
  roots[2] = {};
  EXPECT_EQ(NodeTestPeer::GetStats(replay.GetManager()).live_nodes, 2);
  EXPECT_EQ(roots[1].Borrow().GetChildren()[0].GetId(), a);
  roots.clear();
  EXPECT_EQ(NodeTestPeer::GetStats(replay.GetManager()).live_nodes, 0);
  EXPECT_EQ(NodeTestPeer::GetStats(replay.GetManager()).node_reclamations, 3);
}

TEST(NodeWorkloadTest, RequestsAreInternedByTheManager) {
  const auto workload = Read(R"(dandelion-node 1 mixed
sort 1 int
make 0 1 kIntValue 0 -123456789012345678901234567890
hold 1 0
drop 0
make 0 1 kIntValue 0 -123456789012345678901234567890
drop 0
drop 1
make 0 1 kIntValue 0 -123456789012345678901234567890
drop 0
end
)");
  Replay replay(workload, true);
  ASSERT_TRUE(replay.Step());
  const auto first = replay.GetNode(0).GetId();
  EXPECT_EQ(replay.GetNode(0).GetValue()->GetInteger()->ToDecimal(),
            "-123456789012345678901234567890");
  ASSERT_TRUE(replay.Step());
  ASSERT_TRUE(replay.Step());
  EXPECT_TRUE(replay.GetNode(0).IsNull());
  ASSERT_TRUE(replay.Step());
  EXPECT_EQ(replay.GetNode(0).GetId(), first);
  ASSERT_TRUE(replay.Step());
  ASSERT_TRUE(replay.Step());
  EXPECT_EQ(NodeTestPeer::GetStats(replay.GetManager()).live_nodes, 0);
  ASSERT_TRUE(replay.Step());
  EXPECT_NE(replay.GetNode(0).GetId(), first);
  replay.Run();
  const auto stats = NodeTestPeer::GetStats(replay.GetManager());
  EXPECT_EQ(stats.node_allocations, 2);
  EXPECT_EQ(stats.node_reclamations, 2);
}

TEST(NodeWorkloadTest, ReplaysRecordedLookupsInOrder) {
  const auto workload = Read(R"(dandelion-node 1 lookup
sort 1 bv 8
make 0 1 kBvValue 0 3
make 1 1 kBvValue 0 5
make 2 1 kBvAdd 2 0 1
lookup 2 0 2
lookup 1 0
end
)");
  EXPECT_TRUE(workload.IsLookup());
  EXPECT_EQ(workload.RequestCount(), 3);
  ASSERT_EQ(workload.LookupCount(), 5);
  Replay replay(workload, true);
  replay.Run();
  const std::array<std::size_t, 5> slots{2, 0, 2, 1, 0};
  for (std::size_t request = 0; request < slots.size(); ++request) {
    EXPECT_EQ(replay.Lookup(request).GetId(),
              replay.GetNode(slots[request]).GetId());
  }
  replay.RunLookups();
  const auto stats = NodeTestPeer::GetStats(replay.GetManager());
  EXPECT_EQ(stats.node_allocations, 3);
  EXPECT_EQ(stats.live_nodes, 3);
  EXPECT_EQ(stats.node_reclamations, 0);
}

TEST(NodeWorkloadTest, RejectsInvalidRecordedLookupSequences) {
  const std::string head = "dandelion-node 1 lookup\nsort 1 bool\n";
  const std::string seed = head + "make 0 1 kBoolValue 0 1\n";
  for (const std::string_view invalid :
       {"end\n", "lookup\nend\n", "lookup 1\nend\n", "lookup -1\nend\n",
        "lookup 0\nmake 1 1 kNot 1 0\nend\n", "hold 1 0\nlookup 0\nend\n",
        "drop 0\nlookup 0\nend\n", "roots 0\nlookup 0\nend\n"}) {
    EXPECT_THROW(Read(seed + std::string(invalid)), std::runtime_error)
        << invalid;
  }
  EXPECT_THROW(Read(head + "make 1 1 kBoolValue 0 1\nlookup 1\nend\n"),
               std::runtime_error);
  for (const std::string_view format : {"graph", "mixed"}) {
    EXPECT_THROW(Read("dandelion-node 1 " + std::string(format) +
                      "\nsort 1 bool\nmake 0 1 kBoolValue 0 1\nlookup 0\n"
                      "end\n"),
                 std::runtime_error);
  }
}

TEST(NodeWorkloadTest, MixedTheoryValuesDeclarationsAndOrderedOperands) {
  const auto workload = Read(R"(dandelion-node 1 graph
sort 1 bool
sort 2 bv 12
sort 3 bv 5
sort 4 fp 8 24
sort 5 array 3 4
group 1 1
recipe 1 param 0 0
ctor 0 1 2 3 1
commit
sort 6 apply 1 5
symbol 2 1
sort 7 apply 2 6
decl 4 function 7 6 4
decl 5 function 7 6 4
inst 1 1 5
inst 2 3 5
inst 3 4
inst 4 5
make 0 2 kBvValue 0 291
make 1 3 kBvExtract 1 0 8 4
drop 0
make 2 4 kFpValue 0 2147483648
make 3 4 kFpValue 0 nan
make 4 5 kArrayConst 1 2
make 5 6 kApply 1 4 1
make 6 5 kApply 1 5 2
make 7 4 kArraySelect 2 6 1
make 8 7 kApply 2 5 3 3
make 9 7 kApply 2 5 3 4
drop 1
drop 2
drop 3
drop 4
drop 5
drop 6
roots 7 8 9
end
)");
  Replay replay(workload, true);
  replay.Run();
  const auto roots = replay.TakeRoots();
  const auto select = roots[0].Borrow();
  EXPECT_EQ(select.GetKind(), Kind::kArraySelect);
  const auto extract = select.GetChildren()[1];
  ASSERT_EQ(extract.GetIndices().size(), 2);
  EXPECT_EQ(extract.GetIndices()[0].ToDecimal(), "8");
  EXPECT_EQ(extract.GetIndices()[1].ToDecimal(), "4");
  EXPECT_EQ(extract.GetSort().GetWidth(), 5);
  const auto selector = select.GetChildren()[0].GetDeclaration();
  ASSERT_TRUE(selector);
  EXPECT_EQ(selector->GetDeclaration().GetKind(), DeclKind::kDatatypeSelector);
  const auto first_uf = roots[1].Borrow();
  const auto second_uf = roots[2].Borrow();
  ASSERT_NE(first_uf.GetDeclaration(), second_uf.GetDeclaration());
  EXPECT_EQ(first_uf.GetChildren()[0], second_uf.GetChildren()[0]);
  const auto constructor = first_uf.GetChildren()[0];
  EXPECT_EQ(constructor.GetDeclaration()->GetDeclaration().GetKind(),
            DeclKind::kDatatypeConstructor);
  EXPECT_EQ(constructor.GetSort().GetArguments()[0].GetKind(),
            SortKind::kArray);
  const auto constant_array = constructor.GetChildren()[0];
  EXPECT_EQ(constant_array,
            select.GetChildren()[0].GetChildren()[0].GetChildren()[0]);
  EXPECT_EQ(constant_array.GetChildren()[0].GetValue()->GetFpCategory(),
            FpCategory::kNegativeZero);
  EXPECT_EQ(first_uf.GetChildren()[1].GetValue()->GetFpCategory(),
            FpCategory::kNaN);
}

TEST(NodeWorkloadTest, RejectsInvalidInputAndLiveTemporaries) {
  const std::string valid_head = "dandelion-node 1 graph\nsort 1 bool\n";
  EXPECT_THROW(Workload::Load("/dev/null/no-workload"), std::runtime_error);
  EXPECT_THROW(Read(""), std::runtime_error);
  EXPECT_THROW(Read(valid_head + "end\n"), std::runtime_error);
  EXPECT_THROW(Read(valid_head + "make 0 1 kNot 1 0\nroots 0\nend\n"),
               std::runtime_error);
  EXPECT_THROW(Read(valid_head + "make 0 1 kBoolValue 0 1\nroots 0\n"),
               std::runtime_error);
  EXPECT_THROW(Read(valid_head + "make 0 1 kBoolValue 0 3\nroots 0\nend\n"),
               std::runtime_error);
  EXPECT_THROW(Read(valid_head + "make 0 2 kBoolValue 0 1\nroots 0\nend\n"),
               std::runtime_error);
  EXPECT_THROW(Read(valid_head + "make 0 1 kInternal 0\nroots 0\nend\n"),
               std::runtime_error);
  EXPECT_THROW(Read(valid_head + "make 0 1 kBoolValue 0 1\n"
                                 "hold 1 0\nroots 0\nend\n"),
               std::runtime_error);
  EXPECT_THROW(Read(valid_head + "make 0 1 kBoolValue 0 1\n"
                                 "roots 0\nend\nextra\n"),
               std::runtime_error);
  const auto workload = Read(valid_head +
                             "sort 2 int\n"
                             "make 0 2 kBoolValue 0 1\nroots 0\nend\n");
  Replay replay(workload, true);
  EXPECT_THROW(replay.Run(), std::runtime_error);
}

TEST(NodeWorkloadTest, ReportsRealWorkloadProbesWhenRequested) {
  const auto* path = std::getenv("DANDELION_NODE_PROBE_WORKLOAD");
  if (!path) GTEST_SKIP() << "An explicit workload is required";
  const auto workload = Workload::Load(path);
  Replay replay(workload);
  replay.Run();
  const auto built = NodeTestPeer::GetStats(replay.GetManager());
  if (workload.IsLookup()) replay.RunLookups();
  const auto final = NodeTestPeer::GetStats(replay.GetManager());
  const auto print = [](std::string_view stage, const NodeTestStats& stats) {
    std::cout << stage << " " << stats.probe_slots << " " << stats.probe_max
              << " " << stats.insertion_searches << " "
              << stats.node_allocations << " " << stats.node_reclamations;
    for (auto bucket : stats.probe_histogram) std::cout << " " << bucket;
    std::cout << '\n';
  };
  print("built", built);
  print("final", final);
  if (workload.IsLookup()) {
    EXPECT_EQ(built.node_allocations, final.node_allocations);
    EXPECT_EQ(built.node_reclamations, final.node_reclamations);
  }
}

TEST(NodeWorkloadTest, DiagnosesRealGraphLifetimeWhenRequested) {
  const auto* path = std::getenv("DANDELION_NODE_DIAGNOSTIC_WORKLOAD");
  if (!path) GTEST_SKIP() << "An explicit larger generated graph is required";
  const auto workload = Workload::Load(path);
  ASSERT_TRUE(workload.IsGraph());
  ASSERT_GT(workload.RootCount(), 1);
  Replay replay(workload, true);
  const auto initial = NodeTestPeer::GetStats(replay.GetManager());
  replay.Run();
  auto roots = replay.TakeRoots();
  const auto graph = Walk(roots);
  const auto constructed = NodeTestPeer::GetStats(replay.GetManager());
  EXPECT_EQ(constructed.live_nodes, graph.Nodes());
  EXPECT_GT(constructed.table_capacity, initial.table_capacity);
  replay.Reset();
  replay.Run();
  auto repeated = replay.TakeRoots();
  EXPECT_EQ(roots, repeated);
  const auto repeat = NodeTestPeer::GetStats(replay.GetManager());
  EXPECT_EQ(repeat.node_allocations, constructed.node_allocations);
  repeated.clear();
  const auto half = roots.size() / 2;
  const auto remaining = Walk(std::span(roots).subspan(half));
  for (std::size_t index = 0; index < half; ++index) roots[index] = {};
  const auto partial = NodeTestPeer::GetStats(replay.GetManager());
  EXPECT_EQ(partial.live_nodes, remaining.Nodes());
  roots.clear();
  const auto released = NodeTestPeer::GetStats(replay.GetManager());
  EXPECT_EQ(released.live_nodes, 0);
  EXPECT_EQ(released.node_reclamations, constructed.node_allocations);
  std::cout << std::format(
      "{{\"capacity_initial\":{},\"capacity_after_construct\":{},"
      "\"allocations_after_construct\":{},\"allocations_after_repeat\":{},"
      "\"live_after_partial\":{},\"edges_after_partial\":{},"
      "\"live_after_final\":{},\"reclamations_after_final\":{}}}\n",
      initial.table_capacity, constructed.table_capacity,
      constructed.node_allocations, repeat.node_allocations, partial.live_nodes,
      remaining.Edges(), released.live_nodes, released.node_reclamations);
}

}  // namespace

TEST(NodeWorkloadTest, ReplaysUnreducedSignedRationalPayloadExactly) {
  const auto workload = Read(R"(dandelion-node 1 graph
sort 1 rm
sort 2 fp 8 24
make 0 1 kRoundingModeValue 0 0
make 1 2 kFpFromRational 1 0 8 24 12 -18
drop 0
roots 1
end
)");
  Replay replay(workload, true);
  replay.Run();
  const auto roots = replay.TakeRoots();
  ASSERT_EQ(roots.size(), 1);
  const auto node = roots.front().Borrow();
  ASSERT_TRUE(node.GetRationalParameter());
  EXPECT_EQ(node.GetRationalParameter()->Numerator().ToDecimal(), "-2");
  EXPECT_EQ(node.GetRationalParameter()->Denominator().ToDecimal(), "3");
  ASSERT_EQ(node.GetChildren().size(), 1);
  EXPECT_EQ(node.GetChildren()[0].GetValue()->GetRoundingMode(),
            RoundingMode::kRne);
  ASSERT_EQ(node.GetIndices().size(), 2);
  EXPECT_EQ(node.GetIndices()[0].ToDecimal(), "8");
  EXPECT_EQ(node.GetIndices()[1].ToDecimal(), "24");
}

TEST(NodeWorkloadTest, ReportsDiagnosticCategoryAndPhysicalRecordLine) {
  const auto check = [](const std::string& input, std::string_view message) {
    try {
      const auto workload = Read(input);
      Replay replay(workload, true);
      replay.Run();
      ADD_FAILURE() << "Expected workload diagnostic";
    } catch (const std::runtime_error& error) {
      EXPECT_TRUE(std::string_view(error.what()).starts_with("Line 5: "))
          << error.what();
      EXPECT_NE(std::string_view(error.what()).find(message),
                std::string_view::npos)
          << error.what();
    }
  };
  const std::string header =
      "dandelion-node 1 graph\nsort 1 bool\nsort 2 int\n\n";
  const std::string tail = "\nroots 0\nend\n";
  check(header + "make 0 1 kBoolValue 0 3" + tail, "invalid Bool payload");
  check(
      "dandelion-node 1 graph\nsort 1 rm\nsort 2 fp 8 24\n"
      "make 0 1 kRoundingModeValue 0 0\n"
      "make 1 2 kFpFromRational 1 0 8 24 3" +
          tail,
      "expected FP rational payload");
  check(header + "make 0 1 kApply 0 9" + tail,
        "undefined declaration instance ID 9");
  check(header + "make 0 2 kBoolValue 0 1" + tail,
        "result sort differs from capture");
}

}  // namespace dandelion::node_bench

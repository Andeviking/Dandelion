#include <benchmark/benchmark.h>

import std;
import dandelion.node;
import dandelion.node_workload;
import dandelion.util.numeric;

namespace {

using dandelion::Kind;
using dandelion::Node;
using dandelion::NodeManager;
using dandelion::NodeStorageStats;
using dandelion::NodeView;
using dandelion::numeric::BitVectorView;
using dandelion::numeric::FloatingPointView;
using dandelion::numeric::Integer;
using dandelion::numeric::IntegerView;
using dandelion::numeric::Rational;
using dandelion::numeric::RoundingMode;

void NodeIntegerHit(benchmark::State& state) {
  auto manager_result = dandelion::NodeManager::Create();
  if (!manager_result) {
    state.SkipWithError("Cannot create NodeManager");
    return;
  }
  auto manager = std::move(*manager_result);
  auto retained = manager.MakeInt(IntegerView(42));
  {
    auto repeated = manager.MakeInt(IntegerView(42));
    if (!retained || !repeated || retained->GetId() != repeated->GetId()) {
      state.SkipWithError(
          "Integer node interning did not produce the same identity");
      return;
    }
  }
  for (auto iteration : state) {
    (void)iteration;
    auto result = manager.MakeInt(IntegerView(42));
    if (!result) {
      state.SkipWithError("Cannot construct integer node");
      break;
    }
    benchmark::DoNotOptimize(result);
  }
}

void NodeIntegerCreateRelease(benchmark::State& state) {
  auto manager_result = dandelion::NodeManager::Create();
  if (!manager_result) {
    state.SkipWithError("Cannot create NodeManager");
    return;
  }
  auto manager = std::move(*manager_result);
  dandelion::NodeId first_id = 0;
  {
    auto first = manager.MakeInt(IntegerView(42));
    if (!first) {
      state.SkipWithError("Cannot construct integer node");
      return;
    }
    first_id = first->GetId();
  }
  {
    auto second = manager.MakeInt(IntegerView(42));
    if (!second || second->GetId() == first_id) {
      state.SkipWithError("Released integer node was not reclaimed");
      return;
    }
  }
  for (auto iteration : state) {
    (void)iteration;
    auto result = manager.MakeInt(IntegerView(42));
    if (!result) {
      state.SkipWithError("Cannot construct integer node");
      break;
    }
    benchmark::DoNotOptimize(result);
  }
}

BENCHMARK(NodeIntegerHit)->Name("node/integer/hit");
BENCHMARK(NodeIntegerCreateRelease)->Name("node/integer/create_release");

void Check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void ReportStorage(benchmark::State& state, const NodeStorageStats& stats,
                   std::string_view prefix = "") {
  const std::string name(prefix);
  state.counters[name + "node_bytes"] = static_cast<double>(stats.node_bytes);
  state.counters[name + "numeric_backing_bytes"] =
      static_cast<double>(stats.numeric_backing_bytes);
  state.counters[name + "live_nodes"] = static_cast<double>(stats.live_nodes);
  state.counters[name + "live_edges"] = static_cast<double>(stats.live_edges);
}

void CheckStorage(const NodeStorageStats& actual,
                  const NodeStorageStats& expected) {
  Check(actual.live_nodes == expected.live_nodes &&
            actual.live_edges == expected.live_edges &&
            actual.node_bytes == expected.node_bytes &&
            actual.numeric_backing_bytes == expected.numeric_backing_bytes,
        "Unexpected live storage after dropping benchmark results");
}

// Prepare the payload and sort in setup. A hit retains the seed while a
// create_release iteration times both the miss and final owner destruction.
template <class Setup>
void RunPayloadBenchmark(benchmark::State& state, bool hit, Setup setup) {
  try {
    auto manager = NodeManager::Create().value();
    setup(manager, [&](auto make, auto matches, const char* label) {
      const auto baseline = manager.GetStorageStats().value();
      auto seed = make().value();
      Check(matches(seed.Borrow()), "Unexpected numeric fixture payload");
      const auto id = seed.GetId();
      const auto live = manager.GetStorageStats().value();
      Check(live.live_nodes == baseline.live_nodes + 1,
            "Expected one live numeric fixture node");
      if (!hit) seed = {};
      {
        const auto repeated = make().value();
        Check((repeated.GetId() == id) == hit,
              "Unexpected numeric fixture identity");
      }
      CheckStorage(manager.GetStorageStats().value(), hit ? live : baseline);

      for (auto iteration : state) {
        (void)iteration;
        auto result = make();
        if (!result) {
          state.SkipWithError("Cannot construct numeric node");
          break;
        }
        benchmark::DoNotOptimize(result);
      }

      seed = {};
      const auto released = manager.GetStorageStats().value();
      CheckStorage(released, baseline);
      state.SetLabel(label);
      ReportStorage(state, live);
      ReportStorage(state, released, "released_");
    });
  } catch (const std::exception& error) {
    state.SkipWithError(error.what());
  }
}

void NodeWideInteger(benchmark::State& state, bool hit) {
  RunPayloadBenchmark(state, hit, [](NodeManager& manager, auto measure) {
    // An owning Integer exposes magnitude limbs above INT64_MAX.
    const auto value = Integer::FromUnsigned(std::uint64_t{1} << 63).value();
    measure([&] { return manager.MakeInt(value.Borrow()); },
            [&](NodeView node) {
              return node.GetValue().value().GetInteger() == value.Borrow();
            },
            "Int 2^63");
  });
}

void NodeBv(benchmark::State& state, bool hit, BitVectorView bits,
            const char* label) {
  RunPayloadBenchmark(state, hit, [&](NodeManager& manager, auto measure) {
    const auto sort = manager.MakeBvSort(bits.Width()).value();
    measure([&] { return manager.MakeBvValue(sort.Borrow(), bits); },
            [&](NodeView node) {
              return node.GetSort() == sort.Borrow() &&
                     node.GetValue().value().GetBitVector() == bits;
            },
            label);
  });
}

void NodeFp(benchmark::State& state, std::uint32_t exponent,
            std::uint32_t significand, bool special, const char* label) {
  RunPayloadBenchmark(state, false, [&](NodeManager& manager, auto measure) {
    const auto sort =
        manager.MakeFpSort(IntegerView(exponent), IntegerView(significand))
            .value();
    const auto format = *sort.Borrow().GetFpFormat();
    const std::array<std::uint64_t, 2> limbs{1, 0x3fff000000000000};
    const auto payload =
        special ? FloatingPointView::FromCanonicalSpecial(
                      format, dandelion::numeric::FpSpecial::kNegativeZero)
                : FloatingPointView::FromCanonicalBits(
                      format, format.Width() == 64
                                  ? BitVectorView::FromCanonicalWord(
                                        format.Width(), 0x3ff0000000000001)
                                  : BitVectorView::FromCanonicalLimbs(
                                        format.Width(), limbs));
    measure([&] { return manager.MakeFpValue(sort.Borrow(), payload); },
            [&](NodeView node) {
              return node.GetSort() == sort.Borrow() &&
                     node.GetValue().value().GetFloatingPoint() == payload;
            },
            label);
  });
}

void NodeRationalConversion(benchmark::State& state, bool wide) {
  RunPayloadBenchmark(state, false, [&](NodeManager& manager, auto measure) {
    const std::array<std::uint64_t, 3> numerator{1, 0, 1};
    const std::array<std::uint64_t, 2> denominator{0, 2};
    const auto value =
        (wide ? Rational::Make(
                    IntegerView::FromCanonicalMagnitude(numerator, false),
                    IntegerView::FromCanonicalMagnitude(denominator, false))
              : Rational::Make(IntegerView(9), IntegerView(8)))
            .value();
    const auto rne = manager.MakeRoundingMode(RoundingMode::kRne).value();
    const std::array indices{Integer(8), Integer(24)};
    const std::array children{rne.Borrow()};
    const auto sort =
        manager.MakeFpSort(indices[0].Borrow(), indices[1].Borrow()).value();
    measure(
        [&] {
          return manager.MakeBuiltin(
              {Kind::kFpFromRational, indices, {}, value.Borrow()}, children);
        },
        [&](NodeView node) {
          return node.GetKind() == Kind::kFpFromRational &&
                 node.GetSort() == sort.Borrow() &&
                 node.GetRationalParameter() == value.Borrow() &&
                 node.GetChildren()[0] == rne.Borrow();
        },
        wide ? "FP(8,24) RNE (2^128+1)/2^65" : "FP(8,24) RNE 9/8");
  });
}

BENCHMARK_CAPTURE(NodeWideInteger, hit, true)->Name("node/integer/wide_hit");
BENCHMARK_CAPTURE(NodeWideInteger, create_release, false)
    ->Name("node/integer/wide_create_release");
constexpr std::array<std::uint64_t, 2> kBv65Limbs{
    std::numeric_limits<std::uint64_t>::max(), 1};
BENCHMARK_CAPTURE(NodeBv, word_hit, true,
                  BitVectorView::FromCanonicalWord(64, 42), "BV64 42")
    ->Name("node/bv/word_hit");
BENCHMARK_CAPTURE(NodeBv, word_highbit, false,
                  BitVectorView::FromCanonicalWord(64, std::uint64_t{1} << 63),
                  "BV64 2^63")
    ->Name("node/bv/word_highbit_create_release");
BENCHMARK_CAPTURE(NodeBv, wide_hit, true,
                  BitVectorView::FromCanonicalLimbs(65, kBv65Limbs),
                  "BV65 2^65-1")
    ->Name("node/bv/wide_hit");
BENCHMARK_CAPTURE(NodeBv, wide_create_release, false,
                  BitVectorView::FromCanonicalLimbs(65, kBv65Limbs),
                  "BV65 2^65-1")
    ->Name("node/bv/wide_create_release");
BENCHMARK_CAPTURE(NodeFp, special, 11, 53, true, "FP(11,53) -0")
    ->Name("node/fp/special_create_release");
BENCHMARK_CAPTURE(NodeFp, word, 11, 53, false, "FP(11,53) 1 + 2^-52")
    ->Name("node/fp/word_create_release");
BENCHMARK_CAPTURE(NodeFp, wide, 15, 113, false, "FP(15,113) 1 + 2^-112")
    ->Name("node/fp/wide_create_release");
BENCHMARK_CAPTURE(NodeRationalConversion, small, false)
    ->Name("node/rational_conv/small_create_release");
BENCHMARK_CAPTURE(NodeRationalConversion, wide, true)
    ->Name("node/rational_conv/wide_create_release");

using dandelion::node_bench::BuildFoldingDag;
using dandelion::node_bench::FoldDag;
using dandelion::node_bench::Replay;
using dandelion::node_bench::Walk;
using dandelion::node_bench::Workload;

void NodeDag(benchmark::State& state, bool fold) {
  try {
    auto manager = NodeManager::Create().value();
    const auto roots = BuildFoldingDag(manager);
    const auto shape = Walk(roots);
    const auto input = manager.GetStorageStats().value();
    Check(
        shape.Nodes() == input.live_nodes && shape.Edges() == input.live_edges,
        "DAG fixture has live nodes outside its roots");
    if (fold) {
      {
        const auto results = FoldDag(manager, roots);
        Check(
            results.size() == roots.size() && results.front() != roots.front(),
            "DAG fixture did not produce folded roots");
        ReportStorage(state, manager.GetStorageStats().value(), "folded_");
      }
      CheckStorage(manager.GetStorageStats().value(), input);
    }

    if (fold) {
      for (auto iteration : state) {
        (void)iteration;
        const auto results = FoldDag(manager, roots);
        benchmark::DoNotOptimize(results.data());
      }
    } else {
      const std::function<dandelion::Result<void>(NodeView)> visit =
          [](NodeView node) -> dandelion::Result<void> {
        benchmark::DoNotOptimize(node);
        return {};
      };
      for (auto iteration : state) {
        (void)iteration;
        dandelion::VisitDagPostorder(roots, visit).value();
      }
    }
    CheckStorage(manager.GetStorageStats().value(), input);
    state.counters["roots"] = static_cast<double>(shape.Roots());
    state.counters["depth"] = static_cast<double>(shape.Depth());
    ReportStorage(state, input);
  } catch (const std::exception& error) {
    state.SkipWithError(error.what());
  }
}

BENCHMARK_CAPTURE(NodeDag, postorder, false)->Name("node/dag/postorder");
BENCHMARK_CAPTURE(NodeDag, constant_fold, true)->Name("node/dag/constant_fold");

enum class Scenario {
  kConstruct,
  kRepeat,
  kLookup,
  kWalk,
  kReleasePartial,
  kReleaseFinal,
  kMixed
};

void RunWorkload(benchmark::State& state, const Workload& workload,
                 Scenario scenario) {
  using enum Scenario;
  try {
    if (scenario == kLookup) {
      Replay replay(workload);
      replay.Run();
      for (auto iteration : state) {
        (void)iteration;
        replay.RunLookups();
      }
      state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(
                                                       workload.LookupCount()));
      return;
    }
    if (scenario == kRepeat) {
      Replay replay(workload);
      replay.Run();
      auto original = replay.TakeRoots();
      replay.Reset();
      for (auto iteration : state) {
        (void)iteration;
        replay.Run();
        replay.Reset();
      }
      benchmark::DoNotOptimize(original.data());
      return;
    }

    for (auto iteration : state) {
      (void)iteration;
      state.PauseTiming();
      {
        Replay replay(workload);
        std::vector<dandelion::Node> roots;
        if (scenario != kConstruct && scenario != kMixed) {
          replay.Run();
          roots = replay.TakeRoots();
        }
        state.ResumeTiming();
        switch (scenario) {
          case kConstruct:
          case kMixed:
            replay.Run();
            break;
          case kWalk: {
            const auto stats = Walk(roots, false);
            benchmark::DoNotOptimize(stats.Nodes());
            benchmark::DoNotOptimize(stats.Edges());
            break;
          }
          case kReleasePartial:
          case kReleaseFinal: {
            const auto count =
                scenario == kReleasePartial ? roots.size() / 2 : roots.size();
            if (!replay.GetManager().ReleaseRoots(
                    std::span{roots}.first(count)))
              throw std::runtime_error("Cannot release workload roots");
            break;
          }
          case kRepeat:
          case kLookup:
            std::unreachable();
        }
        state.PauseTiming();
      }
      state.ResumeTiming();
    }
  } catch (const std::exception& error) {
    state.SkipWithError(error.what());
  }
}

void RegisterWorkload(const Workload& workload) {
  benchmark::ClearRegisteredBenchmarks();
  const auto add = [&workload](const char* name, Scenario scenario) {
    benchmark::RegisterBenchmark(std::string("node/workload/") + name,
                                 RunWorkload, std::cref(workload), scenario)
        ->UseRealTime();
  };
  if (workload.IsGraph()) {
    add("construct", Scenario::kConstruct);
    add("repeat", Scenario::kRepeat);
    add("walk", Scenario::kWalk);
    if (workload.RootCount() > 1)
      add("release-partial", Scenario::kReleasePartial);
    add("release-final", Scenario::kReleaseFinal);
  } else if (workload.IsLookup()) {
    add("lookup", Scenario::kLookup);
  } else {
    add("mixed", Scenario::kMixed);
  }
}

void Inspect(const Workload& workload) {
  Replay replay(workload, true);
  replay.Run();
  if (workload.IsLookup()) {
    replay.RunLookups();
    std::cout << std::format(
        "{{\"requests\":{},\"lookups\":{},\"peak_slots\":{}}}\n",
        workload.RequestCount(), workload.LookupCount(), workload.PeakSlots());
    return;
  }
  const auto roots = replay.TakeRoots();
  const auto stats = Walk(roots);
  if (workload.IsGraph() && stats.Nodes() == 0) {
    throw std::runtime_error("The workload has no live nodes at its roots");
  }
  std::cout << std::format(
      "{{\"requests\":{},\"operations\":{},\"peak_slots\":{},"
      "\"roots\":{},\"nodes\":{},\"edges\":{},\"depth\":{},"
      "\"max_fanout\":{}}}\n",
      workload.RequestCount(), workload.OperationCount(), workload.PeakSlots(),
      stats.Roots(), stats.Nodes(), stats.Edges(), stats.Depth(),
      stats.MaxFanout());
}

std::optional<std::size_t> ResidentKiB() {
  std::ifstream memory("/proc/self/smaps_rollup");
  std::string line;
  while (std::getline(memory, line)) {
    if (!line.starts_with("Rss:")) continue;
    std::istringstream fields(line.substr(4));
    std::size_t kib = 0;
    if (fields >> kib) return kib;
    break;
  }
  return std::nullopt;
}

void InspectMemory(const Workload& workload) {
  std::chrono::nanoseconds compaction_time{0};
  std::chrono::nanoseconds metadata_time{0};
  std::cout << std::format("{{\"workload_bytes\":{},\"stages\":[",
                           workload.StorageBytes());
  const auto snapshot = [&](std::string_view stage, Replay* replay,
                            std::size_t root_bytes = 0) {
    const auto rss = ResidentKiB();
    std::cout << std::format("{{\"stage\":\"{}\",\"rss_kib\":{}", stage,
                             rss ? std::to_string(*rss) : "null");
    if (replay) {
      const auto stats = replay->GetManager().GetStorageStats().value();
      std::cout << std::format(
          ",\"replay_bytes\":{},\"root_bytes\":{},\"live_nodes\":{},"
          "\"live_edges\":{},\"node_bytes\":{},\"numeric_backing_bytes\":{},"
          "\"metadata_bytes\":{},"
          "\"table_bytes\":{},\"peak_table_bytes\":{},\"declarations\":{},"
          "\"instances\":{},\"sort_index_entries\":{},"
          "\"instance_index_entries\":{},\"metadata_index_buckets\":{}",
          replay->StorageBytes(), root_bytes, stats.live_nodes,
          stats.live_edges, stats.node_bytes, stats.numeric_backing_bytes,
          stats.metadata_bytes, stats.table_bytes, stats.peak_table_bytes,
          stats.declarations, stats.instances, stats.sort_index_entries,
          stats.instance_index_entries, stats.metadata_index_buckets);
    }
    std::cout << '}';
  };
  snapshot("loaded", nullptr);
  {
    const auto start = std::chrono::steady_clock::now();
    Replay replay(workload, true);
    metadata_time = std::chrono::steady_clock::now() - start;
    std::cout << ',';
    snapshot("metadata", &replay);
    replay.Run();
    auto roots = replay.TakeRoots();
    const auto root_bytes = roots.capacity() * sizeof(dandelion::Node);
    std::cout << ',';
    snapshot("replayed", &replay, root_bytes);
    if (workload.IsGraph()) {
      const auto half = roots.size() / 2;
      if (half != 0) {
        if (!replay.GetManager().ReleaseRoots(std::span{roots}.first(half)))
          throw std::runtime_error("Cannot release partial workload roots");
        std::cout << ',';
        snapshot("partial-release", &replay, root_bytes);
        const auto compact_start = std::chrono::steady_clock::now();
        const auto compact = replay.GetManager().CompactNodeTable();
        compaction_time = std::chrono::steady_clock::now() - compact_start;
        if (!compact) throw std::runtime_error("Node table compaction failed");
        std::cout << ',';
        snapshot("compacted", &replay, root_bytes);
      }
      if (!replay.GetManager().ReleaseRoots(std::span{roots}.subspan(half)))
        throw std::runtime_error("Cannot release remaining workload roots");
      std::cout << ',';
      snapshot("final-release", &replay, root_bytes);
    } else if (workload.IsLookup()) {
      replay.RunLookups();
      std::cout << ',';
      snapshot("lookups", &replay);
    }
  }
  std::cout << ',';
  snapshot("manager-destroyed", nullptr);
  std::cout << std::format("],\"compaction_ns\":{},\"metadata_ns\":{}}}\n",
                           compaction_time.count(), metadata_time.count());
}

}  // namespace

int main(int argc, char** argv) {
  try {
    std::optional<Workload> workload;
    bool inspect = false;
    bool inspect_memory = false;
    int remaining = 1;
    for (int index = 1; index < argc; ++index) {
      const std::string_view argument = argv[index];
      if (argument == "--workload") {
        if (workload || ++index == argc) {
          throw std::runtime_error("Specify one workload path");
        }
        workload = Workload::Load(argv[index]);
      } else if (argument == "--inspect") {
        inspect = true;
      } else if (argument == "--inspect-memory") {
        inspect_memory = true;
      } else {
        argv[remaining++] = argv[index];
      }
    }
    argv[remaining] = nullptr;
    if (inspect || inspect_memory) {
      if (!workload || remaining != 1 || (inspect && inspect_memory)) {
        throw std::runtime_error(
            "Specify one inspection mode and --workload PATH");
      }
      if (inspect)
        Inspect(*workload);
      else
        InspectMemory(*workload);
      return 0;
    }
    if (workload) RegisterWorkload(*workload);
    benchmark::Initialize(&remaining, argv);
    if (benchmark::ReportUnrecognizedArguments(remaining, argv)) return 1;
    const auto matched = benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return matched > 0 ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

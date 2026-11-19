#include <benchmark/benchmark.h>

import std;
import dandelion.node;
import dandelion.node_workload;

namespace {

void NodeIntegerHit(benchmark::State& state) {
  auto manager_result = dandelion::NodeManager::Create();
  if (!manager_result) {
    state.SkipWithError("Cannot create NodeManager");
    return;
  }
  auto manager = std::move(*manager_result);
  auto retained = manager.MakeInt(dandelion::IntegerView(42));
  {
    auto repeated = manager.MakeInt(dandelion::IntegerView(42));
    if (!retained || !repeated || retained->GetId() != repeated->GetId()) {
      state.SkipWithError(
          "Integer node interning did not produce the same identity");
      return;
    }
  }
  for (auto iteration : state) {
    (void)iteration;
    auto result = manager.MakeInt(dandelion::IntegerView(42));
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
    auto first = manager.MakeInt(dandelion::IntegerView(42));
    if (!first) {
      state.SkipWithError("Cannot construct integer node");
      return;
    }
    first_id = first->GetId();
  }
  {
    auto second = manager.MakeInt(dandelion::IntegerView(42));
    if (!second || second->GetId() == first_id) {
      state.SkipWithError("Released integer node was not reclaimed");
      return;
    }
  }
  for (auto iteration : state) {
    (void)iteration;
    auto result = manager.MakeInt(dandelion::IntegerView(42));
    if (!result) {
      state.SkipWithError("Cannot construct integer node");
      break;
    }
    benchmark::DoNotOptimize(result);
  }
}

BENCHMARK(NodeIntegerHit)->Name("node/integer/hit");
BENCHMARK(NodeIntegerCreateRelease)->Name("node/integer/create_release");

using dandelion::node_bench::Replay;
using dandelion::node_bench::Walk;
using dandelion::node_bench::Workload;

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

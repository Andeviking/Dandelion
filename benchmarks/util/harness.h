#ifndef DANDELION_BENCHMARKS_UTIL_HARNESS_H
#define DANDELION_BENCHMARKS_UTIL_HARNESS_H

#include <benchmark/benchmark.h>

#include "fixtures.h"

namespace dandelion::util_bench {

void RegisterCreation();
void RegisterComputation();

template <class Workload>
void Register(std::string name, Workload workload) {
  benchmark::RegisterBenchmark(name, [workload](benchmark::State& state) {
    try {
      workload(state);
    } catch (const std::exception& error) {
      state.SkipWithError(error.what());
    }
  })->Unit(benchmark::kNanosecond);
}

inline void Require(bool condition, const char* diagnostic) {
  if (!condition) throw std::runtime_error(diagnostic);
}

inline void ReportBatch(benchmark::State& state, const char* unit = "ns/op") {
  state.SetItemsProcessed(state.iterations() *
                          static_cast<std::int64_t>(kBatchSize));
  state.counters[unit] = benchmark::Counter(
      kBatchSize / 1e9, benchmark::Counter::kIsIterationInvariantRate |
                            benchmark::Counter::kInvert);
}

template <class Batch>
void RunBatch(benchmark::State& state, Batch batch,
              const char* unit = "ns/op") {
  batch();
  for (auto iteration : state) {
    (void)iteration;
    batch();
    benchmark::ClobberMemory();
  }
  ReportBatch(state, unit);
}

template <class Inputs, class Operation>
void RunIndependent(benchmark::State& state, const Inputs& inputs,
                    Operation operation) {
  RunBatch(state, [&] {
    for (const auto& input : inputs) {
      benchmark::DoNotOptimize(&input);
      auto result = operation(input);
      (void)result.value();
      benchmark::DoNotOptimize(result);
    }
  });
}

}  // namespace dandelion::util_bench

#endif  // DANDELION_BENCHMARKS_UTIL_HARNESS_H

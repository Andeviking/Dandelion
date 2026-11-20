#include "harness.h"

int main(int argc, char** argv) {
  benchmark::Initialize(&argc, argv);
  if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
  dandelion::util_bench::RegisterCreation();
  dandelion::util_bench::RegisterComputation();
  const auto matched = benchmark::RunSpecifiedBenchmarks();
  benchmark::Shutdown();
  return matched == 0 ? 1 : 0;
}

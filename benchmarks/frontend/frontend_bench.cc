#include <benchmark/benchmark.h>

import dandelion.frontend;
import dandelion.frontend_workload;
import std;

namespace {

using dandelion::frontend::DiagnosticCode;
using dandelion::frontend_bench::Inspect;
using dandelion::frontend_bench::InspectReport;
using dandelion::frontend_bench::InspectToJson;
using dandelion::frontend_bench::LoadFile;
using dandelion::frontend_bench::ParseFromReader;
using dandelion::frontend_bench::ParseFromText;
using dandelion::frontend_bench::ParseStats;
using dandelion::frontend_bench::ReaderMode;

struct Scenario {
  // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
  std::string_view name;
  std::optional<ReaderMode> reader_mode = std::nullopt;
  bool measure_phases = false;
  bool teardown_only = false;
  // NOLINTEND(readability-identifier-naming)
};

constexpr Scenario kScenarios[] = {
    {"text"},
    {"reader_64k", ReaderMode::kMemory64K},
    {"reader_4k", ReaderMode::kMemory4K},
    {"reader_irregular", ReaderMode::kMemoryIrregular},
    {.name = "phases", .measure_phases = true},
    {.name = "teardown", .teardown_only = true},
};

constexpr std::string_view kBuiltinInput =
    "(set-logic QF_SAT)\n(assert true)\n(check-sat)\n";

struct Options {
  // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
  std::optional<std::filesystem::path> input;
  std::vector<Scenario> scenarios;
  bool inspect = false;
  // NOLINTEND(readability-identifier-naming)
};

std::vector<Scenario> ParseScenarios(std::string_view csv) {
  std::vector<Scenario> scenarios;
  for (;;) {
    const auto comma = csv.find(',');
    const auto name = csv.substr(0, comma);
    if (name.empty()) throw std::runtime_error("Empty frontend scenario name");
    const auto* scenario = std::ranges::find(kScenarios, name, &Scenario::name);
    if (scenario == std::end(kScenarios))
      throw std::runtime_error(
          std::format("Unknown frontend scenario '{}'", name));
    if (std::ranges::find(scenarios, name, &Scenario::name) != scenarios.end())
      throw std::runtime_error(
          std::format("Duplicate frontend scenario '{}'", name));
    scenarios.push_back(*scenario);
    if (comma == std::string_view::npos) return scenarios;
    csv.remove_prefix(comma + 1);
  }
}

Options ParseOptions(int& argc, char** argv) {
  Options options;
  int remaining = 1;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    const auto read_value = [&]() -> std::string_view {
      if (++index == argc)
        throw std::runtime_error(std::format("Missing value for {}", argument));
      const std::string_view value(argv[index]);
      if (value.empty() || value.starts_with("--"))
        throw std::runtime_error(std::format("Missing value for {}", argument));
      return value;
    };
    if (argument == "--input") {
      if (options.input) throw std::runtime_error("Specify --input once");
      options.input.emplace(read_value());
    } else if (argument == "--scenario") {
      if (!options.scenarios.empty())
        throw std::runtime_error("Specify --scenario once");
      options.scenarios = ParseScenarios(read_value());
    } else if (argument == "--inspect") {
      if (options.inspect) throw std::runtime_error("Specify --inspect once");
      options.inspect = true;
    } else {
      argv[remaining++] = argv[index];
    }
  }
  argc = remaining;
  argv[argc] = nullptr;

  if (!options.input && (options.inspect || !options.scenarios.empty()))
    throw std::runtime_error("--inspect and --scenario require --input");
  if (options.inspect && !options.scenarios.empty()) {
    const auto& scenario = options.scenarios.front();
    if (options.scenarios.size() > 1 || scenario.measure_phases ||
        scenario.teardown_only)
      throw std::runtime_error("--inspect accepts one text or reader scenario");
  }
  return options;
}

void RunWorkload(benchmark::State& state, std::string_view input,
                 const Scenario& scenario) {
  ParseStats totals;
  for (auto iteration : state) {
    (void)iteration;
    if (scenario.teardown_only) state.PauseTiming();
    auto parsed = scenario.reader_mode
                      ? ParseFromReader(input, *scenario.reader_mode)
                      : ParseFromText(input, scenario.measure_phases);
    if (parsed.status != "ok") {
      state.SkipWithError(parsed.diagnostic_message);
      return;
    }
    if (scenario.teardown_only) state.ResumeTiming();
    parsed.Teardown(scenario.measure_phases);
    if (scenario.measure_phases) {
      totals.next_syntax_ns += parsed.stats.next_syntax_ns;
      totals.apply_ns += parsed.stats.apply_ns;
      totals.teardown_ns += parsed.stats.teardown_ns;
    }
  }

  if (!scenario.teardown_only)
    state.SetBytesProcessed(state.iterations() *
                            static_cast<std::int64_t>(input.size()));
  if (scenario.measure_phases) {
    const auto report = [&state](const char* name, std::uint64_t total) {
      state.counters[name] = benchmark::Counter(
          static_cast<double>(total), benchmark::Counter::kAvgIterations);
    };
    report("next_syntax_ns", totals.next_syntax_ns);
    report("apply_ns", totals.apply_ns);
    report("teardown_ns", totals.teardown_ns);
  }
}

void RegisterWorkload(const std::string& name, const Scenario& scenario,
                      std::string_view input,
                      std::string_view load_error = {}) {
  benchmark::RegisterBenchmark(name,
                               [scenario, input, load_error](auto& state) {
                                 if (!load_error.empty()) {
                                   state.SkipWithError(std::string(load_error));
                                   return;
                                 }
                                 try {
                                   RunWorkload(state, input, scenario);
                                 } catch (const std::exception& error) {
                                   state.SkipWithError(error.what());
                                 }
                               });
}

InspectReport LoadFailure(const std::error_code& error,
                          const std::string& message) {
  InspectReport report;
  if (error == std::errc::not_enough_memory ||
      error == std::errc::file_too_large) {
    report.status = "resource";
    report.diagnostic_code = DiagnosticCode::kResource;
  } else {
    report.status = "input_io";
    report.diagnostic_code = DiagnosticCode::kInputIo;
  }
  report.diagnostic_message = message;
  return report;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    auto options = ParseOptions(argc, argv);
    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;

    if (!options.input) {
      RegisterWorkload("frontend/smoke/builtin", kScenarios[0], kBuiltinInput);
      const auto matched = benchmark::RunSpecifiedBenchmarks();
      benchmark::Shutdown();
      return matched > 0 ? 0 : 1;
    }

    const auto input = LoadFile(*options.input);
    const auto load_error =
        input ? std::string{}
              : std::format("Cannot load '{}': {}", options.input->string(),
                            input.error().message());
    if (options.inspect) {
      const auto mode = options.scenarios.empty()
                            ? std::nullopt
                            : options.scenarios.front().reader_mode;
      const auto report = input ? Inspect(*input, mode)
                                : LoadFailure(input.error(), load_error);
      std::cout << InspectToJson(report) << '\n';
      benchmark::Shutdown();
      return report.status == "ok" ? 0 : 1;
    }

    const auto scenarios = options.scenarios.empty()
                               ? std::span<const Scenario>(kScenarios)
                               : std::span<const Scenario>(options.scenarios);
    for (const auto& scenario : scenarios) {
      RegisterWorkload(
          std::format("frontend/parse/{}", scenario.name), scenario,
          input ? std::string_view(*input) : std::string_view{}, load_error);
    }
    const auto matched = benchmark::RunSpecifiedBenchmarks();
    benchmark::Shutdown();
    return matched > 0 ? 0 : 1;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}

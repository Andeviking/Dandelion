#include <gtest/gtest.h>

#include <cerrno>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include "benchmark_test_config.h"

extern char** environ;

namespace {

namespace fs = std::filesystem;

struct ProcessResult {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  int exit_code;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::string output;
};

std::string ReadFile(const fs::path& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("Cannot read " + path.string());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void WriteFile(const fs::path& path, const std::string& contents) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path);
  output << contents;
  if (!output) throw std::runtime_error("Cannot write " + path.string());
}

class BenchmarkIntegrationTest : public ::testing::Test {
 protected:
  void SetUp() override {
    auto pattern =
        (fs::temp_directory_path() / "dandelion benchmark-XXXXXX").string();
    std::vector<char> bytes(pattern.begin(), pattern.end());
    bytes.push_back('\0');
    char* created = mkdtemp(bytes.data());
    ASSERT_NE(created, nullptr);
    directory_ = created;
  }

  void TearDown() override {
    if (directory_.empty()) return;
    std::error_code error;
    fs::remove_all(directory_, error);
    EXPECT_FALSE(error) << error.message();
  }

  ProcessResult Run(std::vector<std::string> arguments) {
    const auto output_path =
        directory_ / ("output-" + std::to_string(process_count_++));
    std::vector<char*> argv;
    for (auto& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    int error = posix_spawn_file_actions_init(&actions);
    if (error != 0) throw std::system_error(error, std::generic_category());
    error = posix_spawn_file_actions_addopen(
        &actions, STDOUT_FILENO, output_path.c_str(),
        O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (error == 0)
      error = posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO,
                                               STDERR_FILENO);
    pid_t child = 0;
    if (error == 0)
      error = posix_spawnp(&child, argv.front(), &actions, nullptr, argv.data(),
                           environ);
    posix_spawn_file_actions_destroy(&actions);
    if (error != 0) throw std::system_error(error, std::generic_category());

    int status = 0;
    while (waitpid(child, &status, 0) == -1) {
      if (errno != EINTR)
        throw std::system_error(errno, std::generic_category());
    }
    return {WIFEXITED(status) ? WEXITSTATUS(status) : 128,
            ReadFile(output_path)};
  }

  void WriteParent(const std::string& setup, bool expect_benchmark) {
    std::string cmake = R"cmake(cmake_minimum_required(VERSION 4.5)
project(BenchmarkFixture LANGUAGES CXX)
add_library(gmp_stub INTERFACE)
add_library(GMP::GMP ALIAS gmp_stub)
set(GMP_VERSION 6.3.0)
)cmake";
    cmake += setup;
    cmake += "\nadd_subdirectory([==[" + std::string(kDandelionSourceDir) +
             "]==] dandelion)\n";
    cmake += expect_benchmark
                 ? "if(NOT TARGET dandelion_node_benchmarks)\n"
                   "  message(FATAL_ERROR \"Expected benchmark target\")\n"
                   "endif()\n"
                 : "if(TARGET dandelion_node_benchmarks OR TARGET "
                   "benchmark::benchmark)\n"
                   "  message(FATAL_ERROR \"Unexpected benchmark target\")\n"
                   "endif()\n";
    WriteFile(directory_ / "CMakeLists.txt", cmake);
  }

  ProcessResult Configure(std::vector<std::string> options) {
    std::vector<std::string> args = {
        kCmakeCommand,
        "-S",
        directory_.string(),
        "-B",
        (directory_ / "build").string(),
        "-G",
        "Ninja",
        "-DCMAKE_CXX_COMPILER=" + std::string(kCxxCompiler),
        "-DDANDELION_BUILD_TESTS=OFF",
        "-DDANDELION_ENABLE_SANITIZERS=OFF",
        "-DDANDELION_DEPENDENCY_MODE=EXISTING",
        "-DDANDELION_SUBPROJECTS_DIR=" + (directory_ / "managed").string(),
    };
    args.insert(args.end(), options.begin(), options.end());
    return Run(std::move(args));
  }

  void WriteSource(const fs::path& root) {
    WriteFile(root / "CMakeLists.txt",
              R"cmake(cmake_minimum_required(VERSION 3.13)
project(benchmark VERSION 1.10.0 LANGUAGES CXX)
if(BENCHMARK_ENABLE_TESTING OR BENCHMARK_ENABLE_GTEST_TESTS OR
   BENCHMARK_ENABLE_INSTALL OR BENCHMARK_DOWNLOAD_DEPENDENCIES)
  message(FATAL_ERROR "Unexpected upstream test, install, or download option")
endif()
add_library(benchmark STATIC stub.cc)
set_target_properties(benchmark PROPERTIES VERSION 1.10.0)
add_library(benchmark::benchmark ALIAS benchmark)
message(STATUS "fixture-source-selected")
)cmake");
    WriteFile(root / "stub.cc", "void BenchmarkFixture() {}\n");
    WriteFile(root / "include/benchmark/benchmark.h", "#pragma once\n");
  }

  void WritePackage() {
    const auto package = directory_ / "package";
    WriteFile(package / "benchmarkConfig.cmake", R"cmake(
add_library(benchmark::benchmark INTERFACE IMPORTED GLOBAL)
message(STATUS "fixture-package-selected")
)cmake");
    WriteFile(package / "benchmarkConfigVersion.cmake", R"cmake(
set(PACKAGE_VERSION "1.10.0")
if(PACKAGE_FIND_VERSION VERSION_GREATER PACKAGE_VERSION)
  set(PACKAGE_VERSION_COMPATIBLE FALSE)
else()
  set(PACKAGE_VERSION_COMPATIBLE TRUE)
endif()
)cmake");
  }

  fs::path directory_;
  int process_count_ = 0;
};

TEST_F(BenchmarkIntegrationTest, DisabledDoesNotResolveOrPopulateBenchmark) {
  WriteParent("", false);
  const auto result = Configure({"-DDANDELION_BUILD_BENCHMARKS=OFF",
                                 "-DCMAKE_DISABLE_FIND_PACKAGE_benchmark=TRUE",
                                 "-DDANDELION_BENCHMARK_SOURCE_DIR=" +
                                     (directory_ / "missing").string()});
  EXPECT_EQ(result.exit_code, 0) << result.output;
  EXPECT_FALSE(fs::exists(directory_ / "managed"));
}

TEST_F(BenchmarkIntegrationTest, ParentTargetTakesPrecedence) {
  WriteParent("add_library(benchmark::benchmark INTERFACE IMPORTED GLOBAL)\n",
              true);
  const auto result = Configure({"-DDANDELION_BUILD_BENCHMARKS=ON",
                                 "-DCMAKE_DISABLE_FIND_PACKAGE_benchmark=TRUE",
                                 "-DDANDELION_BENCHMARK_SOURCE_DIR=" +
                                     (directory_ / "missing").string()});
  EXPECT_EQ(result.exit_code, 0) << result.output;
  EXPECT_FALSE(fs::exists(directory_ / "managed"));
}

TEST_F(BenchmarkIntegrationTest, ExplicitSourcePrecedesInstalledPackage) {
  WriteParent("", true);
  WriteSource(directory_ / "source");
  WritePackage();
  const auto result = Configure(
      {"-DDANDELION_BUILD_BENCHMARKS=ON",
       "-Dbenchmark_DIR=" + (directory_ / "package").string(),
       "-DDANDELION_BENCHMARK_SOURCE_DIR=" + (directory_ / "source").string()});
  EXPECT_EQ(result.exit_code, 0) << result.output;
  EXPECT_NE(result.output.find("fixture-source-selected"), std::string::npos);
  EXPECT_EQ(result.output.find("fixture-package-selected"), std::string::npos);
}

TEST_F(BenchmarkIntegrationTest, InvalidExplicitSourceDoesNotFallBack) {
  WriteParent("", true);
  WritePackage();
  const auto result =
      Configure({"-DDANDELION_BUILD_BENCHMARKS=ON",
                 "-Dbenchmark_DIR=" + (directory_ / "package").string(),
                 "-DDANDELION_BENCHMARK_SOURCE_DIR=" +
                     (directory_ / "missing").string()});
  EXPECT_NE(result.exit_code, 0) << result.output;
  EXPECT_NE(result.output.find("DANDELION_BENCHMARK_SOURCE_DIR"),
            std::string::npos);
  EXPECT_EQ(result.output.find("fixture-package-selected"), std::string::npos);
}

TEST_F(BenchmarkIntegrationTest, InstalledPackageIsUsedWhenAvailable) {
  WriteParent("", true);
  WritePackage();
  const auto result =
      Configure({"-DDANDELION_BUILD_BENCHMARKS=ON",
                 "-Dbenchmark_DIR=" + (directory_ / "package").string()});
  EXPECT_EQ(result.exit_code, 0) << result.output;
  EXPECT_NE(result.output.find("fixture-package-selected"), std::string::npos);
  EXPECT_FALSE(fs::exists(directory_ / "managed"));
}

TEST_F(BenchmarkIntegrationTest,
       MissingDependencyFailsOfflineWithInstructions) {
  WriteParent("", true);
  const auto result =
      Configure({"-DDANDELION_BUILD_BENCHMARKS=ON",
                 "-DCMAKE_DISABLE_FIND_PACKAGE_benchmark=TRUE"});
  EXPECT_NE(result.exit_code, 0) << result.output;
  EXPECT_NE(result.output.find("Google Benchmark"), std::string::npos);
  EXPECT_NE(result.output.find("DANDELION_DEPENDENCY_MODE=FETCH"),
            std::string::npos);
  EXPECT_FALSE(fs::exists(directory_ / "managed"));
}

TEST_F(BenchmarkIntegrationTest, ManagedSourceIsReusedOffline) {
  WriteParent("", true);
  const auto source = directory_ / "managed" / "benchmark-1.10.0";
  WriteSource(source);
  WriteFile(source / ".dandelion-benchmark-commit",
            "a6b4a9f00b36c19af9997d68a1cf6e0b5b3d032c\n");
  const auto result =
      Configure({"-DDANDELION_BUILD_BENCHMARKS=ON",
                 "-DCMAKE_DISABLE_FIND_PACKAGE_benchmark=TRUE"});
  EXPECT_EQ(result.exit_code, 0) << result.output;
  EXPECT_NE(result.output.find("fixture-source-selected"), std::string::npos);
}

TEST_F(BenchmarkIntegrationTest, InvalidManagedSourceIsNotOverwritten) {
  WriteParent("", true);
  const auto source = directory_ / "managed" / "benchmark-1.10.0";
  WriteSource(source);
  WriteFile(source / ".dandelion-benchmark-commit", "unexpected\n");
  const auto result =
      Configure({"-DDANDELION_BUILD_BENCHMARKS=ON",
                 "-DCMAKE_DISABLE_FIND_PACKAGE_benchmark=TRUE"});
  EXPECT_NE(result.exit_code, 0) << result.output;
  EXPECT_NE(result.output.find("provenance"), std::string::npos);
  EXPECT_EQ(ReadFile(source / ".dandelion-benchmark-commit"), "unexpected\n");
}

#ifdef __linux__
TEST_F(BenchmarkIntegrationTest, ConvertsTraceWithoutForcingPoolOutcomes) {
  const auto result = Run(
      {"python3",
       std::string(kDandelionSourceDir) + "/tests/benchmark/pipeline_test.py",
       "conversion"});
  EXPECT_EQ(result.exit_code, 0) << result.output;
}

TEST_F(BenchmarkIntegrationTest, ConvertsCapturedLookupHitsInOrder) {
  const auto result = Run(
      {"python3",
       std::string(kDandelionSourceDir) + "/tests/benchmark/pipeline_test.py",
       "lookup-conversion"});
  EXPECT_EQ(result.exit_code, 0) << result.output;
}

TEST_F(BenchmarkIntegrationTest, DetectsInvalidBenchmarkResults) {
  const auto result = Run(
      {"python3",
       std::string(kDandelionSourceDir) + "/tests/benchmark/pipeline_test.py",
       "reporting"});
  EXPECT_EQ(result.exit_code, 0) << result.output;
}

TEST_F(BenchmarkIntegrationTest, ClassifiesTimeoutAndSignal) {
  const auto result = Run(
      {"python3",
       std::string(kDandelionSourceDir) + "/tests/benchmark/pipeline_test.py",
       "processes"});
  EXPECT_EQ(result.exit_code, 0) << result.output;
}

TEST_F(BenchmarkIntegrationTest, CapturesActualReferenceWhenRequested) {
  if (std::getenv("DANDELION_CVC5_TRACE_BINARY") == nullptr)
    GTEST_SKIP()
        << "Set DANDELION_CVC5_TRACE_BINARY for the isolated reference run";
  const auto result = Run(
      {"python3",
       std::string(kDandelionSourceDir) + "/tests/benchmark/pipeline_test.py",
       "reference"});
  EXPECT_EQ(result.exit_code, 0) << result.output;
}

TEST_F(BenchmarkIntegrationTest, CapturesLargeSnapshotAdapterWhenRequested) {
  if (std::getenv("DANDELION_CVC5_SNAPSHOT_BINARY") == nullptr) {
    GTEST_SKIP()
        << "Set DANDELION_CVC5_SNAPSHOT_BINARY for the snapshot fixture";
  }
  std::vector<std::string> args = {
      "python3",
      std::string(kDandelionSourceDir) + "/tests/benchmark/pipeline_test.py",
      "snapshot"};
#ifdef DANDELION_BENCHMARK_EXECUTABLE
  args.emplace_back(DANDELION_BENCHMARK_EXECUTABLE);
#endif
  const auto result = Run(std::move(args));
  EXPECT_EQ(result.exit_code, 0) << result.output;
}

#ifdef DANDELION_BENCHMARK_EXECUTABLE
TEST_F(BenchmarkIntegrationTest, MeasuresRecordedLookupsAndChecksTheManifest) {
  const auto result = Run(
      {"python3",
       std::string(kDandelionSourceDir) + "/tests/benchmark/pipeline_test.py",
       "lookup", DANDELION_BENCHMARK_EXECUTABLE});
  EXPECT_EQ(result.exit_code, 0) << result.output;
}

TEST_F(BenchmarkIntegrationTest, ReportsSeparateMemoryLifecycleCheckpoints) {
  const auto result = Run(
      {"python3",
       std::string(kDandelionSourceDir) + "/tests/benchmark/pipeline_test.py",
       "memory-inspection", DANDELION_BENCHMARK_EXECUTABLE});
  EXPECT_EQ(result.exit_code, 0) << result.output;
}
#endif
#endif

#ifdef DANDELION_BENCHMARK_EXECUTABLE
TEST_F(BenchmarkIntegrationTest, LoadedGraphPassesBoundedBenchmark) {
  const auto workload = directory_ / "node workload.txt";
  const auto report = directory_ / "node benchmark.json";
  WriteFile(workload,
            "dandelion-node 1 graph\nsort 1 bool\n"
            "make 0 1 kBoolValue 0 1\n"
            "make 1 1 kNot 1 0\n"
            "roots 0 1\nend\n");
  const auto result =
      Run({DANDELION_BENCHMARK_EXECUTABLE, "--workload", workload.string(),
           "--benchmark_min_time=2x", "--benchmark_out=" + report.string(),
           "--benchmark_out_format=json"});
  ASSERT_EQ(result.exit_code, 0) << result.output;
  const auto json = ReadFile(report);
  EXPECT_NE(json.find("node/workload/construct"), std::string::npos);
  EXPECT_NE(json.find("node/workload/repeat"), std::string::npos);
  EXPECT_NE(json.find("node/workload/walk"), std::string::npos);
  EXPECT_NE(json.find("node/workload/release-partial"), std::string::npos);
  EXPECT_NE(json.find("node/workload/release-final"), std::string::npos);
  EXPECT_EQ(json.find("\"error_occurred\": true"), std::string::npos);
  EXPECT_EQ(json.find("\"skipped\": true"), std::string::npos);
}

TEST_F(BenchmarkIntegrationTest, RealNodeWorkloadsPassDryRun) {
  const auto report = directory_ / "benchmark results.json";
  const auto result = Run(
      {DANDELION_BENCHMARK_EXECUTABLE, "--benchmark_dry_run",
       "--benchmark_filter=^node/integer/",
       "--benchmark_out=" + report.string(), "--benchmark_out_format=json"});
  ASSERT_EQ(result.exit_code, 0) << result.output;
  ASSERT_TRUE(fs::exists(report)) << result.output;
  const auto json = ReadFile(report);
  EXPECT_NE(json.find("node/integer/hit"), std::string::npos);
  EXPECT_NE(json.find("node/integer/create_release"), std::string::npos);
  EXPECT_EQ(json.find("\"error_occurred\": true"), std::string::npos);
  EXPECT_EQ(json.find("\"skipped\": true"), std::string::npos);
}
#endif

}  // namespace

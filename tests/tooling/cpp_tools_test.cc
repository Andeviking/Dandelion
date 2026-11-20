#include <gtest/gtest.h>

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include "tooling_config.h"

extern char** environ;

namespace {

namespace fs = std::filesystem;

struct ProcessResult {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  int exit_code;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::string output;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::string error;
};

std::string ReadFile(const fs::path& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("Cannot read " + path.string());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

void WriteFile(const fs::path& path, const std::string& content) {
  fs::create_directories(path.parent_path());
  std::ofstream output(path);
  output << content;
  if (!output) throw std::runtime_error("Cannot write " + path.string());
}

fs::path MakeTemporaryDirectory() {
  std::random_device random;
  for (int attempt = 0; attempt < 100; ++attempt) {
    const fs::path path = fs::temp_directory_path() /
                          ("dandelion-tooling-" + std::to_string(random()) +
                           "-" + std::to_string(random()));
    std::error_code error;
    if (fs::create_directory(path, error)) return path;
    if (error && error != std::errc::file_exists) {
      throw std::system_error(error, "Cannot create temporary test directory");
    }
  }
  throw std::runtime_error("Cannot create a unique temporary test directory");
}

class CppToolsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    directory_ = MakeTemporaryDirectory();
    fs::copy_file(fs::path(kDandelionSourceDir) / ".clang-format",
                  directory_ / ".clang-format");
    const auto format = Run({"clang-format", "--version"});
    ASSERT_EQ(format.exit_code, 0) << format.output << format.error;
    ASSERT_NE(format.output.find("version 23.1.5"), std::string::npos);
    const auto tidy = Run({"clang-tidy", "--version"});
    ASSERT_EQ(tidy.exit_code, 0) << tidy.output << tidy.error;
    ASSERT_NE(tidy.output.find("version 23.1.5"), std::string::npos);
  }

  void TearDown() override {
    std::error_code error;
    fs::remove_all(directory_, error);
    EXPECT_FALSE(error) << error.message();
  }

  ProcessResult Run(std::vector<std::string> arguments) {
    const auto suffix = std::to_string(process_count_++);
    const fs::path output_path = directory_ / ("stdout-" + suffix);
    const fs::path error_path = directory_ / ("stderr-" + suffix);
    std::vector<char*> argv;
    for (auto& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    int result = posix_spawn_file_actions_init(&actions);
    if (result != 0) throw std::system_error(result, std::generic_category());
    result = posix_spawn_file_actions_addopen(
        &actions, STDOUT_FILENO, output_path.c_str(),
        O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (result == 0) {
      result = posix_spawn_file_actions_addopen(
          &actions, STDERR_FILENO, error_path.c_str(),
          O_WRONLY | O_CREAT | O_TRUNC, 0600);
    }
    pid_t child = 0;
    if (result == 0) {
      result = posix_spawnp(&child, argv.front(), &actions, nullptr,
                            argv.data(), environ);
    }
    posix_spawn_file_actions_destroy(&actions);
    if (result != 0) {
      throw std::system_error(result, std::generic_category(),
                              "Cannot start " + arguments.front());
    }

    int status = 0;
    while (waitpid(child, &status, 0) == -1) {
      if (errno != EINTR)
        throw std::system_error(errno, std::generic_category());
    }
    const int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : 128;
    return {exit_code, ReadFile(output_path), ReadFile(error_path)};
  }

  ProcessResult Tidy(const std::string& source) {
    const fs::path path = directory_ / "naming.cc";
    WriteFile(path, source);
    return Run({"clang-tidy",
                "--config-file=" +
                    (fs::path(kDandelionSourceDir) / ".clang-tidy").string(),
                path.string(), "--", "-std=c++23"});
  }

  fs::path directory_;
  int process_count_ = 0;
};

TEST_F(CppToolsTest, TidyConfigurationIsValidAndNarrow) {
  const std::string config =
      "--config-file=" +
      (fs::path(kDandelionSourceDir) / ".clang-tidy").string();
  const auto verified = Run({"clang-tidy", config, "--verify-config"});
  ASSERT_EQ(verified.exit_code, 0) << verified.output << verified.error;
  const auto listed = Run({"clang-tidy", config, "--list-checks"});
  ASSERT_EQ(listed.exit_code, 0) << listed.output << listed.error;
  std::istringstream lines(listed.output);
  std::string line;
  std::getline(lines, line);
  std::vector<std::string> checks;
  while (std::getline(lines, line)) {
    const auto first = line.find_first_not_of(" \t\r");
    if (first != std::string::npos) {
      const auto last = line.find_last_not_of(" \t\r");
      checks.push_back(line.substr(first, last - first + 1));
    }
  }
  EXPECT_EQ(checks,
            (std::vector<std::string>{"readability-identifier-naming"}));
}

TEST_F(CppToolsTest, LegalNamesAndLocalExceptionsPass) {
  const auto result = Tidy(R"fixture(
#define DANDELION_TOOLING_MAX_SIZE 8
    namespace dandelion {
    constexpr int kMaxSize = 8;
    const int kStaticSize = 8;
    enum class TermKind { kBitVector };
    using TermIndex = int;
    template <typename ValueType>
    struct TermStorage {};
    class TermParser {
     public:
      int ParseTerm(int token_count);
      static constexpr int kMaxCount = 4;
      static int token_cache_;
      // NOLINTNEXTLINE(readability-identifier-naming): Google-style accessor.
      int token_count() const { return token_count_; }
      // NOLINTNEXTLINE(readability-identifier-naming): Google-style mutator.
      void set_token_count(int token_count) { token_count_ = token_count; }

     private:
      int token_count_ = 0;
      const int limit_ = 8;
    };
    struct TermInfo {
      // NOLINTNEXTLINE(readability-identifier-naming): Struct field.
      int token_count;
      // NOLINTNEXTLINE(readability-identifier-naming): Static struct field.
      static int token_pool;
      static constexpr int kMaxCount = 4;
    };
    class TermRange {
     public:
      // NOLINTNEXTLINE(readability-identifier-naming): Container interface.
      using value_type = int;
      // NOLINTNEXTLINE(readability-identifier-naming): Range interface.
      int* begin();
      // NOLINTNEXTLINE(readability-identifier-naming): Range interface.
      int* end();
    };
    void ParseTerm(int token_count) {
      const int local_count = token_count;
      const int kFixedCount = 8;
      constexpr int local_limit = 8;
      constexpr int kLocalLimit = 8;
      static const int kStaticLimit = 8;
    }
    }  // namespace dandelion
    int main() { return 0; }
  )fixture");
  EXPECT_EQ(result.exit_code, 0) << result.output << result.error;
}

using NamingCase = std::pair<std::string, std::string>;
class BadNamingTest : public CppToolsTest,
                      public ::testing::WithParamInterface<NamingCase> {};

TEST_P(BadNamingTest, ReportsTheExpectedIdentifier) {
  const auto& [source, identifier] = GetParam();
  const auto result = Tidy(source);
  const std::string output = result.output + result.error;
  EXPECT_NE(result.exit_code, 0) << output;
  EXPECT_NE(output.find("error: invalid case style"), std::string::npos)
      << output;
  EXPECT_NE(output.find("'" + identifier + "'"), std::string::npos) << output;
  EXPECT_NE(output.find("readability-identifier-naming,-warnings-as-errors"),
            std::string::npos)
      << output;
  EXPECT_EQ(output.find("clang-diagnostic-error"), std::string::npos) << output;
}

INSTANTIATE_TEST_SUITE_P(
    InvalidNames, BadNamingTest,
    ::testing::Values(
        NamingCase{"class term_parser {};", "term_parser"},
        NamingCase{"struct term_info {};", "term_info"},
        NamingCase{"enum class term_kind {};", "term_kind"},
        NamingCase{"enum class TermKind { BitVector };", "BitVector"},
        NamingCase{"void parse_term();", "parse_term"},
        NamingCase{"void test_main();", "test_main"},
        NamingCase{"class TermParser { void parse_term(); };", "parse_term"},
        NamingCase{"class TermParser { int tokenCount_; };", "tokenCount_"},
        NamingCase{"class TermParser { int token_count; };", "token_count"},
        NamingCase{"class TermParser { public: int token_count; };",
                   "token_count"},
        NamingCase{"struct TermInfo { int tokenCount; };", "tokenCount"},
        NamingCase{"void ParseTerm(int tokenCount);", "tokenCount"},
        NamingCase{"void ParseTerm() { int tokenCount = 0; }", "tokenCount"},
        NamingCase{"int tokenCount;", "tokenCount"},
        NamingCase{"constexpr int max_size = 8;", "max_size"},
        NamingCase{"const int max_size = 8;", "max_size"},
        NamingCase{"void ParseTerm() { static const int max_size = 8; }",
                   "max_size"},
        NamingCase{"class TermParser { static constexpr int max_size = 8; };",
                   "max_size"},
        NamingCase{"#define MAX_SIZE 8\n", "MAX_SIZE"},
        NamingCase{"#define DANDELION_bad_macro 8\n", "DANDELION_bad_macro"},
        NamingCase{"using term_index = int;", "term_index"},
        NamingCase{"template <typename value_type> struct TermStorage {};",
                   "value_type"},
        NamingCase{"namespace TermNamespace {}", "TermNamespace"}));

class UnannotatedNamingTest
    : public CppToolsTest,
      public ::testing::WithParamInterface<std::string> {};

TEST_P(UnannotatedNamingTest, RequiresReview) {
  const auto result = Tidy(GetParam());
  EXPECT_NE(result.exit_code, 0) << result.output << result.error;
  EXPECT_NE(result.output.find("readability-identifier-naming"),
            std::string::npos)
      << result.output << result.error;
}

INSTANTIATE_TEST_SUITE_P(
    MissingExceptions, UnannotatedNamingTest,
    ::testing::Values("class TermParser { int token_count() const; };",
                      "class TermRange { public: using value_type = int; };",
                      "class TermRange { public: int* begin(); };",
                      "struct TermInfo { int token_count; };"));

TEST_F(CppToolsTest, OneExceptionDoesNotHideAdjacentViolations) {
  const auto result = Tidy(R"fixture(
    class TermParser {
     public:
      // NOLINTNEXTLINE(readability-identifier-naming): Google-style accessor.
      int token_count() const;
      int parse_term();

     private:
      int tokenCount_;
    };
  )fixture");
  const std::string output = result.output + result.error;
  EXPECT_NE(result.exit_code, 0) << output;
  EXPECT_NE(output.find("method 'parse_term'"), std::string::npos) << output;
  EXPECT_NE(output.find("member 'tokenCount_'"), std::string::npos) << output;
  EXPECT_EQ(output.find("method 'token_count'"), std::string::npos) << output;
}

TEST_F(CppToolsTest, ModuleFormattingFixIsIdempotent) {
  const fs::path project = directory_ / "format-entry";
  const fs::path script = project / "scripts/check_cpp.py";
  fs::create_directories(script.parent_path());
  fs::copy_file(fs::path(kDandelionSourceDir) / "scripts/check_cpp.py", script);
  fs::copy_file(fs::path(kDandelionSourceDir) / ".clang-format",
                project / ".clang-format");
  const fs::path source = project / "tests/format_probe.cppm";
  WriteFile(source,
            "export module dandelion.format_probe;\n"
            "export int ComputeValue( int value ){return value+1;}\n");
  auto result = Run({kDandelionPython, script.string(), "format-check"});
  EXPECT_NE(result.exit_code, 0);
  EXPECT_NE(result.error.find("clang-format-violations"), std::string::npos)
      << result.error;
  result = Run({kDandelionPython, script.string(), "format"});
  ASSERT_EQ(result.exit_code, 0) << result.error;
  const auto formatted = ReadFile(source);
  for (int attempt = 0; attempt < 2; ++attempt) {
    result = Run({kDandelionPython, script.string(), "format-check"});
    EXPECT_EQ(result.exit_code, 0) << result.error;
  }
  result = Run({kDandelionPython, script.string(), "format"});
  ASSERT_EQ(result.exit_code, 0) << result.error;
  EXPECT_EQ(ReadFile(source), formatted);
}

TEST_F(CppToolsTest, FormattingPreservesIncludeOrderBracesAndQualifiers) {
  const fs::path source = directory_ / "preserve.cc";
  WriteFile(source,
            "#include \"z.h\"\n#include \"a.h\"\n"
            "int const* PassThrough(int const* value) {\n"
            "  if (value) return value;\n  return nullptr;\n}\n");
  const auto result = Run({"clang-format", "--style=file", source.string()});
  ASSERT_EQ(result.exit_code, 0) << result.error;
  EXPECT_EQ(result.output.find("#include \"z.h\"\n#include \"a.h\"\n"), 0U);
  EXPECT_NE(result.output.find("int const*"), std::string::npos);
  EXPECT_NE(result.output.find("if (value) return value;"), std::string::npos);
}

TEST_F(CppToolsTest, SourceSelectionExcludesDependenciesAndGeneratedFiles) {
  const fs::path root = directory_ / "source-scope";
  const std::set<std::string> wanted = {
      "src/component.cc", "include/component.hpp", "tests/module.cppm"};
  const std::set<std::string> unwanted = {
      "src/third_party/upstream.cc",     "src/generated/output.cpp",
      "src/vendor/upstream.h",           "build/generated.cc",
      "tests/build-scratch/output.cppm", "external/upstream.cc"};
  for (const auto& name : wanted) WriteFile(root / name, "// Test fixture.\n");
  for (const auto& name : unwanted)
    WriteFile(root / name, "// Test fixture.\n");
  fs::create_symlink(root / "external/upstream.cc", root / "src/symlink.cc");
  const auto result =
      Run({kDandelionPython,
           (fs::path(kDandelionSourceDir) / "tests/tooling/source_scope.py")
               .string(),
           root.string()});
  ASSERT_EQ(result.exit_code, 0) << result.output << result.error;
  std::set<std::string> actual;
  std::istringstream lines(result.output);
  std::string line;
  while (std::getline(lines, line)) actual.insert(line);
  EXPECT_EQ(actual, wanted);
}

TEST_F(CppToolsTest, EmptySourceScopeIsAnError) {
  const fs::path root = directory_ / "empty-scope";
  fs::create_directories(root);
  auto result =
      Run({kDandelionPython,
           (fs::path(kDandelionSourceDir) / "tests/tooling/source_scope.py")
               .string(),
           root.string()});
  EXPECT_EQ(result.exit_code, 2) << result.output << result.error;
  EXPECT_NE(result.error.find("No project C++ files"), std::string::npos);
  const fs::path script = root / "scripts/check_cpp.py";
  fs::create_directories(script.parent_path());
  fs::copy_file(fs::path(kDandelionSourceDir) / "scripts/check_cpp.py", script);
  result = Run({kDandelionPython, script.string(), "format-check"});
  EXPECT_EQ(result.exit_code, 2) << result.output << result.error;
  EXPECT_NE(result.error.find("no checks were performed"), std::string::npos);
}

TEST_F(CppToolsTest, NamingAcceptsSyntheticBmisButRejectsDuplicateObjects) {
  const fs::path project = directory_ / "compilation-database";
  const fs::path build = project / "build";
  const fs::path script = project / "scripts/check_cpp.py";
  fs::create_directories(script.parent_path());
  fs::copy_file(fs::path(kDandelionSourceDir) / "scripts/check_cpp.py", script);
  fs::copy_file(fs::path(kDandelionSourceDir) / ".clang-tidy",
                project / ".clang-tidy");
  WriteFile(project / "CMakeLists.txt",
            "cmake_minimum_required(VERSION 4.5)\n"
            "project(TidyFixture LANGUAGES CXX)\n"
            "add_library(probe OBJECT src/probe.cc)\n");
  WriteFile(project / "src/probe.cc", "int ComputeValue() { return 1; }\n");
  auto result = Run({"cmake", "-S", project.string(), "-B", build.string(),
                     "-G", "Ninja", "-DCMAKE_CXX_COMPILER=clang++",
                     "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"});
  ASSERT_EQ(result.exit_code, 0) << result.output << result.error;
  const fs::path helper =
      fs::path(kDandelionSourceDir) / "tests/tooling/compile_db_fixture.py";
  const fs::path database = build / "compile_commands.json";
  result =
      Run({kDandelionPython, helper.string(), database.string(), "synthetic"});
  ASSERT_EQ(result.exit_code, 0) << result.output << result.error;
  result = Run({kDandelionPython, script.string(), "tidy", "--build-dir",
                build.string()});
  EXPECT_EQ(result.exit_code, 0) << result.output << result.error;
  EXPECT_NE(result.output.find("Checked 1 project translation units"),
            std::string::npos)
      << result.output;

  result =
      Run({kDandelionPython, helper.string(), database.string(), "duplicate"});
  ASSERT_EQ(result.exit_code, 0) << result.output << result.error;
  result = Run({kDandelionPython, script.string(), "tidy", "--build-dir",
                build.string()});
  EXPECT_EQ(result.exit_code, 2) << result.output << result.error;
  EXPECT_NE(result.error.find("Ambiguous compilation commands"),
            std::string::npos)
      << result.error;
}

TEST_F(CppToolsTest, MissingGmpFailsInOfflineMode) {
  const fs::path root = directory_ / "empty-find-root";
  const fs::path subprojects = directory_ / "offline-subprojects";
  fs::create_directories(root);
  const auto result =
      Run({"cmake", "-E", "env", "--unset=GMP_ROOT", "cmake", "-S",
           kDandelionSourceDir, "-B", (directory_ / "offline-build").string(),
           "-G", "Ninja", "-DDANDELION_BUILD_TESTS=OFF",
           "-DDANDELION_DEPENDENCY_MODE=EXISTING",
           "-DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON",
           "-DCMAKE_FIND_ROOT_PATH=" + root.string(),
           "-DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY",
           "-DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY",
           "-DDANDELION_SUBPROJECTS_DIR=" + subprojects.string()});
  const auto output = result.output + result.error;
  EXPECT_NE(result.exit_code, 0) << output;
  EXPECT_NE(output.find("GMP 6.3.0 or newer was not found"), std::string::npos)
      << output;
  EXPECT_NE(output.find("DANDELION_DEPENDENCY_MODE=FETCH"), std::string::npos)
      << output;
  EXPECT_FALSE(fs::exists(subprojects));
}

TEST_F(CppToolsTest, ExplicitOutdatedGmpIsRejected) {
  const fs::path root = directory_ / "old-gmp";
  WriteFile(root / "include/gmp.h",
            "#define __GNU_MP_VERSION 6\n"
            "#define __GNU_MP_VERSION_MINOR 2\n"
            "#define __GNU_MP_VERSION_PATCHLEVEL 0\n");
  WriteFile(root / "lib/libgmp.a", "");
  const auto result = Run({"cmake", "-S", kDandelionSourceDir, "-B",
                           (directory_ / "old-gmp-build").string(), "-G",
                           "Ninja", "-DDANDELION_BUILD_TESTS=OFF",
                           "-DDANDELION_DEPENDENCY_MODE=EXISTING",
                           "-DGMP_ROOT=" + root.string()});
  const auto output = result.output + result.error;
  EXPECT_NE(result.exit_code, 0) << output;
  EXPECT_NE(output.find("header version: '6.2.0'"), std::string::npos)
      << output;
}

TEST_F(CppToolsTest, BenchmarkSourcesAreIncludedInFormattingScope) {
  const fs::path root = directory_ / "benchmark-scope";
  WriteFile(root / "src/component.cc", "// Fixture.\n");
  WriteFile(root / "benchmarks/node/probe.cc", "// Fixture.\n");
  WriteFile(root / "benchmarks/vendor/upstream.cc", "// Fixture.\n");
  const auto result =
      Run({kDandelionPython,
           (fs::path(kDandelionSourceDir) / "tests/tooling/source_scope.py")
               .string(),
           root.string()});
  ASSERT_EQ(result.exit_code, 0) << result.output << result.error;
  EXPECT_NE(result.output.find("benchmarks/node/probe.cc"), std::string::npos);
  EXPECT_EQ(result.output.find("benchmarks/vendor/upstream.cc"),
            std::string::npos);
}

TEST_F(CppToolsTest, NamingChecksBenchmarkSourcesWhenEnabled) {
  const fs::path project = directory_ / "benchmark-naming";
  const fs::path build = project / "build";
  const fs::path script = project / "scripts/check_cpp.py";
  fs::create_directories(script.parent_path());
  fs::copy_file(fs::path(kDandelionSourceDir) / "scripts/check_cpp.py", script);
  fs::copy_file(fs::path(kDandelionSourceDir) / ".clang-tidy",
                project / ".clang-tidy");
  WriteFile(project / "CMakeLists.txt",
            "cmake_minimum_required(VERSION 4.5)\n"
            "project(TidyBenchmarkFixture LANGUAGES CXX)\n"
            "option(DANDELION_BUILD_BENCHMARKS \"Build benchmarks\" OFF)\n"
            "add_library(probe OBJECT src/probe.cc)\n"
            "if(DANDELION_BUILD_BENCHMARKS)\n"
            "  add_library(bench OBJECT benchmarks/probe.cc)\n"
            "endif()\n");
  WriteFile(project / "src/probe.cc", "int ComputeValue() { return 1; }\n");
  WriteFile(project / "benchmarks/probe.cc",
            "int BenchmarkValue() { return 1; }\n");
  auto result =
      Run({"cmake", "-S", project.string(), "-B", build.string(), "-G", "Ninja",
           "-DCMAKE_CXX_COMPILER=clang++", "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON",
           "-DDANDELION_BUILD_BENCHMARKS=OFF"});
  ASSERT_EQ(result.exit_code, 0) << result.output << result.error;
  result = Run({kDandelionPython, script.string(), "tidy", "--build-dir",
                build.string()});
  ASSERT_EQ(result.exit_code, 0) << result.output << result.error;
  EXPECT_NE(result.output.find("Checked 1 project translation units"),
            std::string::npos);

  result = Run({"cmake", "-S", project.string(), "-B", build.string(),
                "-DDANDELION_BUILD_BENCHMARKS=ON"});
  ASSERT_EQ(result.exit_code, 0) << result.output << result.error;
  result = Run({kDandelionPython, script.string(), "tidy", "--build-dir",
                build.string()});
  ASSERT_EQ(result.exit_code, 0) << result.output << result.error;
  EXPECT_NE(result.output.find("Checked 2 project translation units"),
            std::string::npos);

  WriteFile(project / "benchmarks/unregistered.cc", "int Unregistered();\n");
  result = Run({kDandelionPython, script.string(), "tidy", "--build-dir",
                build.string()});
  EXPECT_EQ(result.exit_code, 2) << result.output << result.error;
  EXPECT_NE(result.error.find("No compilation command"), std::string::npos)
      << result.error;
}

TEST_F(CppToolsTest, NamingChecksFuzzerSourcesWhenEnabled) {
  const fs::path project = directory_ / "fuzzer-naming";
  const fs::path build = project / "build";
  const fs::path script = project / "scripts/check_cpp.py";
  fs::create_directories(script.parent_path());
  fs::copy_file(fs::path(kDandelionSourceDir) / "scripts/check_cpp.py", script);
  fs::copy_file(fs::path(kDandelionSourceDir) / ".clang-tidy",
                project / ".clang-tidy");
  WriteFile(project / "CMakeLists.txt",
            "cmake_minimum_required(VERSION 4.5)\n"
            "project(TidyFuzzerFixture LANGUAGES CXX)\n"
            "option(DANDELION_BUILD_FUZZERS \"Build fuzzers\" OFF)\n"
            "add_library(probe OBJECT src/probe.cc)\n"
            "if(DANDELION_BUILD_FUZZERS)\n"
            "  add_library(fuzz OBJECT tests/fuzz/probe.cc)\n"
            "endif()\n");
  WriteFile(project / "src/probe.cc", "int ComputeValue() { return 1; }\n");
  WriteFile(project / "tests/fuzz/probe.cc",
            "int FuzzOneInput() { return 0; }\n");
  auto result = Run({"cmake", "-S", project.string(), "-B", build.string(),
                     "-G", "Ninja", "-DCMAKE_CXX_COMPILER=clang++",
                     "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"});
  ASSERT_EQ(result.exit_code, 0) << result.output << result.error;
  result = Run({kDandelionPython, script.string(), "tidy", "--build-dir",
                build.string()});
  EXPECT_EQ(result.exit_code, 0) << result.output << result.error;
  EXPECT_NE(result.output.find("Checked 1 project translation units"),
            std::string::npos);

  result = Run({"cmake", "-S", project.string(), "-B", build.string(),
                "-DDANDELION_BUILD_FUZZERS=ON"});
  ASSERT_EQ(result.exit_code, 0) << result.output << result.error;
  result = Run({kDandelionPython, script.string(), "tidy", "--build-dir",
                build.string()});
  EXPECT_EQ(result.exit_code, 0) << result.output << result.error;
  EXPECT_NE(result.output.find("Checked 2 project translation units"),
            std::string::npos);

  WriteFile(project / "tests/fuzz/unregistered.cc", "int Unregistered();\n");
  result = Run({kDandelionPython, script.string(), "tidy", "--build-dir",
                build.string()});
  EXPECT_EQ(result.exit_code, 2) << result.output << result.error;
  EXPECT_NE(result.error.find("No compilation command"), std::string::npos)
      << result.error;
}

}  // namespace

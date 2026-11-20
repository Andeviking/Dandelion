#include <gtest/gtest.h>

#include <cstdio>
#include <sys/wait.h>

#include "harness.h"

namespace dandelion::util_bench {
namespace {

std::pair<int, std::string> RunBenchmark(std::string_view arguments) {
  std::string command = "'";
  for (const char ch : std::string_view(DANDELION_UTIL_BENCHMARK_PATH)) {
    command += ch == '\'' ? "'\\''" : std::string(1, ch);
  }
  command += "' ";
  command += arguments;
  command += " 2>&1";
  using Process = std::unique_ptr<std::FILE, decltype(&pclose)>;
  Process process(popen(command.c_str(), "r"), pclose);
  if (!process) throw std::runtime_error("Cannot launch util benchmark");
  std::string output;
  char buffer[4096];
  while (std::fgets(buffer, sizeof(buffer), process.get())) output += buffer;
  const int status = pclose(process.release());
  return {status != -1 && WIFEXITED(status) ? WEXITSTATUS(status) : -1,
          std::move(output)};
}

std::size_t Count(std::string_view text, std::string_view match) {
  std::size_t count = 0;
  for (auto index = text.find(match); index != text.npos;
       index = text.find(match, index + match.size())) {
    ++count;
  }
  return count;
}

TEST(UtilBenchmark, DryRunCoversRegisteredCases) {
  const auto [list_code, listing] = RunBenchmark("--benchmark_list_tests");
  ASSERT_EQ(list_code, 0) << listing;
  const auto [dry_code, json] =
      RunBenchmark("--benchmark_dry_run --benchmark_format=json");
  ASSERT_EQ(dry_code, 0) << json;
  EXPECT_EQ(json.find("error_occurred"), json.npos);
  EXPECT_EQ(json.find("skipped"), json.npos);

  std::istringstream lines(listing);
  std::string name;
  std::size_t case_count = 0;
  while (std::getline(lines, name)) {
    if (!name.starts_with("util/")) continue;
    SCOPED_TRACE(name);
    EXPECT_EQ(Count(json, "\"name\": \"" + name + "\""), 1);
    ++case_count;
  }
  EXPECT_EQ(Count(json, "\"iterations\": 1,"), case_count);
  EXPECT_EQ(Count(json, "\"ns/op\":") + Count(json, "\"ns/row\":"), case_count);
  for (const std::string_view required : {
           "util/create/integer/parse_radix/16/4096",
           "util/create/rational/make/strong_reduction",
           "util/create/bv/from_uint64/4294967295_sparse",
           "util/create/bv/from_binary/65",
           "util/create/bv/from_hex/65536",
           "util/create/fp/from_bits/binary16",
           "util/compute/integer/mul/overflow",
           "util/compute/integer/add/mix90_10",
           "util/compute/integer/add_chain/4096",
           "util/compute/rational/cross_cancel_chain/wide",
           "util/compute/rational/simplex_row/word/32",
           "util/compute/bv/smod/64_high",
           "util/compute/bv/umul_overflow/4096",
           "util/compute/fp/fma/binary128",
           "util/compute/fp/add_gap/20_24",
           "util/compute/fp/to_signed_bv/3_4",
       }) {
    EXPECT_NE(listing.find(std::string(required) + "\n"), listing.npos)
        << required;
  }
}

TEST(UtilBenchmark, InvalidSelectionFails) {
  const auto [no_match, output] =
      RunBenchmark("--benchmark_filter=^no_such_benchmark$");
  EXPECT_NE(no_match, 0);
  EXPECT_NE(output.find("Failed to match"), output.npos) << output;
  const auto [unrecognized, usage] = RunBenchmark("--unknown-util-flag");
  EXPECT_NE(unrecognized, 0);
  EXPECT_NE(usage.find("--unknown-util-flag"), usage.npos) << usage;
}

TEST(UtilBenchmark, PreflightFailureReportsAnError) {
  constexpr char kDiagnostic[] = "Deliberately rejected test fixture";
  char name[] = "util-benchmark-preflight-test";
  char option[] = "--benchmark_dry_run";
  char* argv[] = {name, option, nullptr};
  int argc = 2;
  benchmark::Initialize(&argc, argv);
  Register("util/test/failed_preflight", [&](benchmark::State& state) {
    RunBatch(state, [&] { Require(false, kDiagnostic); });
  });
  std::ostringstream output;
  benchmark::JSONReporter reporter;
  reporter.SetOutputStream(&output);
  const auto matched = benchmark::RunSpecifiedBenchmarks(&reporter);
  benchmark::ClearRegisteredBenchmarks();
  benchmark::Shutdown();

  EXPECT_EQ(matched, 1);
  EXPECT_NE(output.view().find("\"error_occurred\": true"), output.view().npos)
      << output.view();
  EXPECT_NE(output.view().find(kDiagnostic), output.view().npos)
      << output.view();
}

TEST(UtilBenchmarkFixtures, DenseAndSparseBitVectors) {
  std::mt19937_64 random(17);
  for (const std::uint32_t width : {32u, 64u, 65u, 128u, 256u, 4096u, 65536u}) {
    const auto bits = DenseBv(random, width, width);
    EXPECT_EQ(bits.Width(), width);
    EXPECT_EQ(bits.EffectiveBitLength(), width);
    EXPECT_TRUE(bits.TestBit(width - 1));
  }
  const auto sparse_width = std::numeric_limits<std::uint32_t>::max();
  const auto sparse = SparseBv(random, sparse_width);
  EXPECT_EQ(sparse.Width(), sparse_width);
  EXPECT_EQ(sparse.EffectiveBitLength(), 64);
  EXPECT_EQ(sparse.BackingBytes(), 0);
  EXPECT_GE(sparse.TryUint64().value(), std::uint64_t{1} << 63);
}

TEST(UtilBenchmarkFixtures, IntegerMixAndOverflow) {
  std::mt19937_64 random(17);
  int wide_pairs = 0;
  for (std::size_t index = 0; index < kBatchSize; ++index) {
    const auto [lhs, rhs] = MixedIntegers(random, index);
    EXPECT_EQ(lhs.Borrow().IsSmall(), rhs.Borrow().IsSmall());
    if (!lhs.Borrow().IsSmall()) ++wide_pairs;
  }
  EXPECT_EQ(wide_pairs, 4);
  const auto [lhs, rhs] = AddOverflowIntegers(random);
  ASSERT_TRUE(lhs.Borrow().IsSmall());
  ASSERT_TRUE(rhs.Borrow().IsSmall());
  const auto sum = lhs.Borrow() + rhs.Borrow();
  ASSERT_TRUE(sum);
  EXPECT_FALSE(sum->Borrow().IsSmall());
  EXPECT_EQ(sum->Borrow().BitLength(), 64);
  EXPECT_EQ(sum->Borrow().Sign(), 1);
}

TEST(UtilBenchmarkFixtures, RationalCancellationTelescopes) {
  for (const auto bits : {31u, 192u}) {
    const auto chain = MakeRationalChain(bits);
    auto current = chain.values.front();
    for (const auto& factor : chain.factors) {
      auto next = current.Borrow() * factor.Borrow();
      ASSERT_TRUE(next);
      current = std::move(*next);
    }
    EXPECT_EQ(current.Borrow(), chain.values.back().Borrow());
  }
}

TEST(UtilBenchmarkFixtures, FloatingPointExponentGap) {
  std::mt19937_64 random(17);
  const auto format = numeric::FpFormat::FromCanonicalWidths(20, 24);
  const auto [large, small] = FpWithExponentGap(random, format);
  EXPECT_TRUE(large.IsNormal());
  EXPECT_TRUE(small.IsNormal());
  const auto large_exponent = large.ExponentBits().value().TryUint64().value();
  const auto small_exponent = small.ExponentBits().value().TryUint64().value();
  EXPECT_GE(large_exponent - small_exponent, 1'048'560);
}

}  // namespace
}  // namespace dandelion::util_bench

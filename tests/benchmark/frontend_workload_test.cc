#include <gtest/gtest.h>

#ifdef __linux__
#include <cerrno>

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include "benchmark_test_config.h"
#endif

import std;
import dandelion.frontend;
import dandelion.frontend_workload;
import dandelion.util.numeric;

#ifdef __linux__
extern char** environ;
#endif

namespace dandelion::frontend_bench {
namespace {

TEST(FrontendWorkloadTest, CanonicalizationIsOptInForAllAdapters) {
  constexpr std::string_view input = R"(
(set-logic ALL)
(declare-const a Int)
(declare-const b Int)
(declare-const p Bool)
(declare-const q Bool)
(assert (= (+ a b) (+ b a)))
(assert (xor p q))
(assert (xor q p))
(check-sat)
)";
  const auto check = [](const ParsedQuery& parsed, bool canonicalize) {
    ASSERT_TRUE(parsed.query) << parsed.diagnostic_message;
    const auto assertions = parsed.query->GetAssertions();
    ASSERT_EQ(assertions.size(), 3);
    const auto equality = assertions[0].Borrow();
    ASSERT_EQ(equality.GetKind(), Kind::kEqual);
    ASSERT_EQ(equality.GetChildren().size(), 2);
    EXPECT_EQ(equality.GetChildren()[0] == equality.GetChildren()[1],
              canonicalize);
    EXPECT_EQ(assertions[1] == assertions[2], canonicalize);
  };
  check(ParseFromText(input), false);
  check(ParseFromText(input, false, true), true);
  for (const auto mode : {ReaderMode::kMemory64K, ReaderMode::kMemory4K,
                          ReaderMode::kMemoryIrregular}) {
    SCOPED_TRACE(static_cast<int>(mode));
    check(ParseFromReader(input, mode), false);
    check(ParseFromReader(input, mode, false, true), true);
  }
  for (const auto mode :
       {std::optional<ReaderMode>{}, std::optional{ReaderMode::kMemory64K},
        std::optional{ReaderMode::kMemory4K},
        std::optional{ReaderMode::kMemoryIrregular}}) {
    const auto original = Inspect(input, mode);
    const auto canonical = Inspect(input, mode, true);
    ASSERT_EQ(original.status, "ok") << original.diagnostic_message;
    ASSERT_EQ(canonical.status, "ok") << canonical.diagnostic_message;
    EXPECT_EQ(original.nodes, 9);
    EXPECT_EQ(original.edges, 10);
    EXPECT_EQ(canonical.nodes, 7);
    EXPECT_EQ(canonical.edges, 6);
    EXPECT_EQ(original.live_nodes, 9);
    EXPECT_EQ(original.live_edges, 10);
    EXPECT_EQ(canonical.live_nodes, 7);
    EXPECT_EQ(canonical.live_edges, 6);
  }
}

TEST(FrontendWorkloadTest, InspectReportsExactDiamondDagCounts) {
  constexpr std::string_view input =
      "(set-logic QF_SAT)\n"
      "(declare-const shared Bool)\n"
      "(assert (and (not shared) (or shared shared false)))\n"
      "(assert (and (not shared) (or shared shared false)))\n"
      "(check-sat)\n";
  const auto report = Inspect(input);
  ASSERT_EQ(report.status, "ok") << report.diagnostic_message;
  EXPECT_EQ(report.logic, "QF_SAT");
  EXPECT_EQ(report.bytes, input.size());
  EXPECT_EQ(report.commands, 5);
  EXPECT_EQ(report.max_command_bytes, 52);
  EXPECT_EQ(report.assertions, 2);
  EXPECT_EQ(report.definitions, 0);
  EXPECT_EQ(report.user_functions, 1);
  // The root, not, or, shared and false are the five distinct nodes.
  // The root contributes two operand slots, not one, and or three.
  EXPECT_EQ(report.nodes, 5);
  EXPECT_EQ(report.edges, 6);
  EXPECT_EQ(report.depth, 3);
  EXPECT_EQ(report.max_fanout, 3);
  EXPECT_EQ(report.live_nodes, 5);
  EXPECT_EQ(report.live_edges, 6);
}

TEST(FrontendWorkloadTest, PreservesDefineFunCallDagWithoutExpansion) {
  constexpr std::string_view input =
      "(set-logic QF_SAT)\n"
      "(declare-const p Bool)\n"
      "(define-fun base ((x Bool)) Bool (not x))\n"
      "(define-fun both ((x Bool)) Bool (and (base x) (base x)))\n"
      "(assert (both p))\n"
      "(check-sat)\n";
  const auto parsed = ParseFromText(input);
  ASSERT_TRUE(parsed.query);
  EXPECT_EQ(parsed.stats.definitions, 2);
  const auto& query = *parsed.query;
  ASSERT_EQ(query.GetDefinitionCount(), 2);
  EXPECT_EQ(query.GetDefinition(0).body.Borrow().GetKind(), Kind::kNot);
  const auto body = query.GetDefinition(1).body.Borrow();
  ASSERT_EQ(body.GetKind(), Kind::kAnd);
  const auto calls = body.GetChildren();
  ASSERT_EQ(calls.size(), 2);
  EXPECT_EQ(calls[0], calls[1]);
  ASSERT_EQ(calls[0].GetKind(), Kind::kApply);
  ASSERT_TRUE(calls[0].GetDeclaration());
  EXPECT_EQ(query.FindDefinition(calls[0].GetDeclaration()->GetDeclaration()),
            &query.GetDefinition(0));
  ASSERT_EQ(calls[0].GetChildren().size(), 1);
  const auto argument = calls[0].GetChildren()[0].GetDeclaration();
  ASSERT_TRUE(argument);
  ASSERT_EQ(query.GetDefinition(1).formals.size(), 1);
  EXPECT_EQ(argument->GetDeclaration(),
            query.GetDefinition(1).formals[0].Borrow());

  ASSERT_EQ(query.GetAssertions().size(), 1);
  const auto assertion = query.GetAssertions().front().Borrow();
  ASSERT_EQ(assertion.GetKind(), Kind::kApply);
  ASSERT_TRUE(assertion.GetDeclaration());
  EXPECT_EQ(query.FindDefinition(assertion.GetDeclaration()->GetDeclaration()),
            &query.GetDefinition(1));

  const auto report = Inspect(input);
  ASSERT_EQ(report.status, "ok") << report.diagnostic_message;
  EXPECT_EQ(report.definitions, 2);
  // The base body has two nodes and one edge. The both body adds three
  // nodes and three edges. The assertion adds two nodes and one edge.
  EXPECT_EQ(report.nodes, 7);
  EXPECT_EQ(report.edges, 5);
  EXPECT_EQ(report.depth, 3);
}

TEST(FrontendWorkloadTest, HandlesParametricDatatypeMatchAndNamedTerms) {
  constexpr std::string_view input = R"(
(set-logic ALL)
(declare-datatypes ((Maybe 1)) ((par (T) ((none) (some (value T))))))
(declare-const m (Maybe Int))
(assert (! (= (match m (((some v) v) (none 0))) 7) :named picked))
(assert picked)
(check-sat)
)";
  const auto parsed = ParseFromText(input, false, false, true);
  ASSERT_TRUE(parsed.query);
  const auto& query = *parsed.query;
  ASSERT_EQ(query.GetDatatypeGroups().size(), 1);
  const auto& records = *parsed.session->GetSourceRecords();
  ASSERT_EQ(records.named_terms.size(), 1);
  ASSERT_EQ(records.annotations.size(), 1);
  ASSERT_EQ(query.GetAssertions().size(), 2);
  const auto& named = records.named_terms.front();
  EXPECT_EQ(named.name, "picked");
  EXPECT_EQ(named.term, records.annotations.front().term);
  EXPECT_EQ(named.term, query.GetAssertions()[0]);
  EXPECT_EQ(named.term, query.GetAssertions()[1]);

  const auto equality = named.term.Borrow();
  ASSERT_EQ(equality.GetKind(), Kind::kEqual);
  ASSERT_EQ(equality.GetChildren().size(), 2);
  const auto match = equality.GetChildren()[0];
  ASSERT_EQ(match.GetKind(), Kind::kIte);
  ASSERT_EQ(match.GetChildren().size(), 3);
  const auto tester = match.GetChildren()[0];
  const auto selector = match.GetChildren()[1];
  ASSERT_TRUE(tester.GetDeclaration());
  ASSERT_TRUE(selector.GetDeclaration());
  const auto test_decl = tester.GetDeclaration()->GetDeclaration();
  const auto select_decl = selector.GetDeclaration()->GetDeclaration();
  EXPECT_EQ(test_decl.GetKind(), DeclKind::kDatatypeTester);
  EXPECT_EQ(select_decl.GetKind(), DeclKind::kDatatypeSelector);
  EXPECT_EQ(test_decl.GetConstructor(), select_decl.GetConstructor());
  EXPECT_EQ(select_decl.GetNameHint(), "value");
  ASSERT_EQ(tester.GetChildren().size(), 1);
  ASSERT_EQ(selector.GetChildren().size(), 1);
  EXPECT_EQ(tester.GetChildren()[0], selector.GetChildren()[0]);
  const auto parameters = selector.GetChildren()[0].GetSort().GetArguments();
  ASSERT_EQ(parameters.size(), 1);
  EXPECT_EQ(parameters[0].GetKind(), SortKind::kInt);
  EXPECT_EQ(selector.GetSort().GetKind(), SortKind::kInt);

  const auto report = Inspect(input);
  ASSERT_EQ(report.status, "ok") << report.diagnostic_message;
  EXPECT_EQ(report.datatype_groups, 1);
  EXPECT_EQ(report.named_terms, 1);
  EXPECT_EQ(report.annotations, 1);
  EXPECT_EQ(report.assertions, 2);
}

TEST(FrontendWorkloadTest, NormalizesFpRationalConversionsAndArrayStores) {
  constexpr std::string_view input = R"(
(set-logic ALL)
(declare-const a (Array Int Float32))
(assert (fp.eq (select (store a 3 ((_ to_fp 8 24) RNE (/ (- 6) 9))) 3)
               ((_ to_fp 8 24) RNE (/ (- 2) 3))))
(check-sat)
)";
  const auto parsed = ParseFromText(input);
  ASSERT_TRUE(parsed.query);
  const auto assertions = parsed.query->GetAssertions();
  ASSERT_EQ(assertions.size(), 1);
  const auto equality = assertions.front().Borrow();
  ASSERT_EQ(equality.GetKind(), Kind::kFpEq);
  ASSERT_EQ(equality.GetChildren().size(), 2);
  const auto select = equality.GetChildren()[0];
  const auto conversion = equality.GetChildren()[1];
  ASSERT_EQ(select.GetKind(), Kind::kArraySelect);
  ASSERT_EQ(select.GetChildren().size(), 2);
  const auto store = select.GetChildren()[0];
  ASSERT_EQ(store.GetKind(), Kind::kArrayStore);
  ASSERT_EQ(store.GetChildren().size(), 3);
  EXPECT_EQ(store.GetChildren()[1], select.GetChildren()[1]);
  EXPECT_EQ(store.GetChildren()[2], conversion);
  ASSERT_EQ(conversion.GetKind(), Kind::kFpFromRational);
  const auto rational = conversion.GetRationalParameter();
  ASSERT_TRUE(rational);
  EXPECT_EQ(rational->Numerator().ToDecimal(), "-2");
  EXPECT_EQ(rational->Denominator().ToDecimal(), "3");
  ASSERT_EQ(conversion.GetIndices().size(), 2);
  EXPECT_EQ(conversion.GetIndices()[0].ToDecimal(), "8");
  EXPECT_EQ(conversion.GetIndices()[1].ToDecimal(), "24");
  ASSERT_EQ(conversion.GetChildren().size(), 1);
  const auto rounding = conversion.GetChildren()[0].GetValue();
  ASSERT_TRUE(rounding);
  EXPECT_EQ(rounding->GetRoundingMode(), numeric::RoundingMode::kRne);
}

TEST(FrontendWorkloadTest, ReaderChunkModesMatchTextParsingAcrossSplits) {
  std::string input =
      "(set-logic QF_SAT)\r\n; ignored (assert false)\r\n(declare-const ";
  const auto name_start = input.size();
  input += '|';
  // Each fixed chunk boundary falls inside a multibyte scalar.
  input.resize(4 * 1024 - 1, 'a');
  input += "🌱";
  input.resize(64 * 1024 - 1, 'b');
  input += "🌱 ;()|";
  const auto name = input.substr(name_start);
  input += " Bool)\r\n; ignored )\r\n(assert " + name + ")\r\n(check-sat)\r\n";

  const auto text = ParseFromText(input, false, false, true);
  ASSERT_TRUE(text.query);
  ASSERT_EQ(text.query->GetFunctions().size(), 1);
  ASSERT_EQ(text.query->GetAssertions().size(), 1);
  const auto& records = *text.session->GetSourceRecords();
  EXPECT_EQ(records.functions.front().source_name, name);
  EXPECT_EQ(text.stats.bytes, input.size());
  EXPECT_EQ(text.stats.commands, 4);
  EXPECT_EQ(text.stats.assertions, 1);
  EXPECT_EQ(text.stats.definitions, 0);
  EXPECT_EQ(text.stats.max_command_bytes, name.size() + 21);
  EXPECT_EQ(text.stats.reader_calls, 0);

  for (const auto mode : {ReaderMode::kMemory64K, ReaderMode::kMemory4K,
                          ReaderMode::kMemoryIrregular}) {
    SCOPED_TRACE(static_cast<int>(mode));
    const auto reader = ParseFromReader(input, mode, true, false, true);
    ASSERT_TRUE(reader.query);
    EXPECT_EQ(reader.stats.bytes, text.stats.bytes);
    EXPECT_EQ(reader.stats.commands, text.stats.commands);
    EXPECT_EQ(reader.stats.assertions, text.stats.assertions);
    EXPECT_EQ(reader.stats.definitions, text.stats.definitions);
    EXPECT_EQ(reader.stats.max_command_bytes, text.stats.max_command_bytes);
    EXPECT_GT(reader.stats.reader_calls, 1);
    EXPECT_GT(reader.stats.read_command_ns, 0);
    EXPECT_EQ(reader.query->GetLogic(), text.query->GetLogic());
    ASSERT_EQ(reader.query->GetFunctions().size(), 1);
    ASSERT_EQ(reader.query->GetAssertions().size(), 1);
    const auto& source = *reader.session->GetSourceRecords();
    const auto& function = source.functions.front();
    EXPECT_TRUE(function.source_name == name);
    EXPECT_EQ(function.where, records.functions.front().where);
    const auto& assertion = source.assertions.front();
    EXPECT_EQ(assertion.where, records.assertions.front().where);
    const auto term = assertion.term.Borrow();
    ASSERT_EQ(term.GetKind(), Kind::kApply);
    ASSERT_TRUE(term.GetDeclaration());
    EXPECT_EQ(term.GetDeclaration()->GetDeclaration(), function.decl.Borrow());
    EXPECT_TRUE(term.GetChildren().empty());
  }
}

TEST(FrontendWorkloadTest, CountsDefinitionCommandsAndCapturedRoots) {
  SCOPED_TRACE("FE-P1-BENCH and FE-P2-BENCH");
  constexpr std::string_view input =
      "(set-info :status unknown)(set-info :status sat)(set-logic QF_LIA)"
      "(define-fun a () Int 1)(define-const b Int a)(define-fun id ((x Int)) "
      "Int x)"
      "(assert (= (id (! b :named n)) n))(check-sat)";
  const auto timed = ParseFromText(input);
  ASSERT_TRUE(timed.query);
  ASSERT_TRUE(timed.session);
  EXPECT_EQ(timed.session->GetSourceRecords(), nullptr);
  EXPECT_EQ(timed.stats.commands, 8);
  EXPECT_EQ(timed.stats.definitions, 3);
  EXPECT_EQ(timed.query->GetDefinitionCount(), 1);
  for (const auto mode : {ReaderMode::kMemory64K, ReaderMode::kMemory4K,
                          ReaderMode::kMemoryIrregular}) {
    auto reader = ParseFromReader(input, mode, true);
    ASSERT_TRUE(reader.query);
    EXPECT_EQ(reader.stats.commands, 8);
    EXPECT_EQ(reader.stats.committed_end_offset, input.size());
    EXPECT_EQ(reader.stats.assertions, 1);
    EXPECT_EQ(reader.stats.definitions, 3);
    EXPECT_EQ(reader.query->GetDefinitionCount(), 1);
    const auto read_ns = reader.stats.read_command_ns;
    reader.Teardown(true);
    EXPECT_FALSE(reader.query);
    EXPECT_FALSE(reader.session);
    EXPECT_EQ(reader.stats.read_command_ns, read_ns);
  }
  for (const bool fail : {false, true}) {
    const auto text =
        fail ? std::string(input.substr(0, input.find("(check-sat)"))) +
                   "(assert (= (! 2 :named discarded) unbound))"
             : std::string(input);
    const auto inspected = Inspect(text, ReaderMode::kMemoryIrregular);
    EXPECT_EQ(inspected.status, fail ? "invalid_input" : "ok");
    EXPECT_EQ(inspected.commands, fail ? 7 : 8);
    EXPECT_EQ(inspected.committed_end_offset,
              fail ? input.find("(check-sat)") : input.size());
    EXPECT_EQ(inspected.assertions, 1);
    EXPECT_EQ(inspected.checks, fail ? 0 : 1);
    EXPECT_EQ(inspected.definitions, 3);
    EXPECT_EQ(inspected.metadata, 2);
    EXPECT_EQ(inspected.named_terms, 1);
    EXPECT_EQ(inspected.annotations, 1);
    EXPECT_EQ(inspected.user_functions, 0);
    EXPECT_EQ(inspected.user_sorts, 0);
    EXPECT_EQ(inspected.roots, 6);
    EXPECT_EQ(inspected.nodes, 4);
    EXPECT_EQ(inspected.edges, 3);
    const auto json = InspectToJson(inspected);
    EXPECT_TRUE(json.contains("\"schema_version\":2,"));
    EXPECT_TRUE(json.contains("\"read_command_ns\":" +
                              std::to_string(inspected.read_command_ns) + ","));
    EXPECT_TRUE(json.contains(
        "\"teardown_ns\":" + std::to_string(inspected.teardown_ns) + ","));
  }
}

TEST(FrontendWorkloadTest, ReportsStructuredDiagnosticsOnErrors) {
  for (const auto& [input, status, diagnostic] :
       {std::tuple{"(set-logic QF_SAT)(assert true", "syntax",
                   "Incomplete command"},
        {"(set-logic ALL)(assert (and (! true :named staged) 9))(check-sat)",
         "invalid_input", "Incompatible operand or result sort"},
        {"(set-logic QF_S)(check-sat)", "unsupported", "Unregistered logic"}}) {
    SCOPED_TRACE(input);
    const auto report = Inspect(input);
    EXPECT_EQ(report.status, status);
    EXPECT_TRUE(report.diagnostic_message.contains(diagnostic))
        << report.diagnostic_message;
    EXPECT_EQ(report.named_terms, 0);
    EXPECT_EQ(report.annotations, 0);
    EXPECT_EQ(report.assertions, 0);
  }
}

TEST(FrontendWorkloadTest, MemorySnapshotCapturesPositiveProcessRss) {
#ifdef __linux__
  const auto snapshot = ReadProcessMemory();
  EXPECT_GT(snapshot.retained_rss_kib, 0);
  EXPECT_GE(snapshot.peak_rss_kib, snapshot.retained_rss_kib);
#endif
  const auto report =
      Inspect("(set-logic QF_SAT)(assert (not true))(check-sat)");
  ASSERT_EQ(report.status, "ok") << report.diagnostic_message;
  EXPECT_GT(report.node_bytes, 0);
  EXPECT_GT(report.table_bytes, 0);
  EXPECT_GE(report.peak_table_bytes, report.table_bytes);
}

#ifdef __linux__
void RunPipelineCheck(std::string_view check) {
  std::vector<std::string> arguments{
      "python3", "-B",
      (std::filesystem::path(kDandelionSourceDir) / "tests/benchmark" /
       "frontend_pipeline_test.py")
          .string(),
      std::string(check)};
#ifdef DANDELION_FRONTEND_BENCHMARK_EXECUTABLE
  arguments.emplace_back(DANDELION_FRONTEND_BENCHMARK_EXECUTABLE);
#endif
  std::vector<char*> argv;
  for (auto& argument : arguments) argv.push_back(argument.data());
  argv.push_back(nullptr);

  pid_t child = 0;
  const auto error = posix_spawnp(&child, argv.front(), nullptr, nullptr,
                                  argv.data(), environ);
  ASSERT_EQ(error, 0) << std::generic_category().message(error);
  int status = 0;
  pid_t waited;
  do {
    waited = waitpid(child, &status, 0);
  } while (waited == -1 && errno == EINTR);
  ASSERT_EQ(waited, child);
  ASSERT_TRUE(WIFEXITED(status)) << "Python wait status: " << status;
  EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(FrontendWorkloadTest, GeneratorSmoke) {
  RunPipelineCheck("generator-smoke");
}

TEST(FrontendWorkloadTest, CorpusSafety) { RunPipelineCheck("corpus-safety"); }

TEST(FrontendWorkloadTest, RunnerConcurrent) {
  RunPipelineCheck("runner-concurrent");
}
#endif

}  // namespace
}  // namespace dandelion::frontend_bench

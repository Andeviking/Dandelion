#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>

#include "test_support.h"

namespace dandelion::frontend::testing {
namespace {

struct ProbeResult {
  // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
  int status;
  std::string output;
  std::string diagnostics;
  // NOLINTEND(readability-identifier-naming)
};

std::string ReadFile(std::FILE* file) {
  std::rewind(file);
  std::string result;
  std::array<char, 512> buffer;
  while (const auto count = std::fread(buffer.data(), 1, buffer.size(), file))
    result.append(buffer.data(), count);
  EXPECT_EQ(std::ferror(file), 0);
  return result;
}

ProbeResult RunProbe(std::string_view input,
                     std::initializer_list<std::string_view> options = {}) {
  using File = std::unique_ptr<std::FILE, decltype(&std::fclose)>;
  const File source(std::tmpfile(), &std::fclose);
  const File output(std::tmpfile(), &std::fclose);
  const File diagnostics(std::tmpfile(), &std::fclose);
  if (!source || !output || !diagnostics)
    throw std::runtime_error("Could not create probe capture files");
  if ((!input.empty() && std::fwrite(input.data(), 1, input.size(),
                                     source.get()) != input.size()) ||
      std::fflush(source.get()) != 0 ||
      std::fseek(source.get(), 0, SEEK_SET) != 0)
    throw std::runtime_error("Could not feed probe input");

  std::vector<std::string> arguments{DANDELION_FRONTEND_PROBE_PATH};
  arguments.insert(arguments.end(), options.begin(), options.end());
  std::vector<char*> argv;
  for (auto& arg : arguments) argv.push_back(arg.data());
  argv.push_back(nullptr);
  const auto child = ::fork();
  if (child == -1) throw std::runtime_error("Could not fork frontend probe");
  if (child == 0) {
    if (::dup2(::fileno(source.get()), STDIN_FILENO) == -1 ||
        ::dup2(::fileno(output.get()), STDOUT_FILENO) == -1 ||
        ::dup2(::fileno(diagnostics.get()), STDERR_FILENO) == -1)
      ::_exit(126);
    ::execv(argv.front(), argv.data());
    ::_exit(127);
  }
  int status = 0;
  pid_t waited;
  do {
    waited = ::waitpid(child, &status, 0);
  } while (waited == -1 && errno == EINTR);
  if (waited != child || !WIFEXITED(status))
    throw std::runtime_error("Frontend probe did not exit normally: " +
                             ReadFile(diagnostics.get()));
  return {WEXITSTATUS(status), ReadFile(output.get()),
          ReadFile(diagnostics.get())};
}

TEST(FrontendProbe, ModesSummariesAndExitCodes) {
  constexpr std::string_view complete =
      "(set-logic QF_SAT)(declare-const p Bool)(assert p)(check-sat)(exit)";
  const auto input = std::string(complete) + "\xff(";
  const auto summary =
      "QF_SAT commands=5 assertions=1 functions=1 definitions=0 "
      "datatype_groups=0\n";
  SCOPED_TRACE(::testing::Message() << "FE-PROBE-CLI-02 " << complete);
  const auto typed = RunProbe(input);
  EXPECT_EQ(typed.status, 0);
  EXPECT_EQ(typed.output, "");
  EXPECT_EQ(typed.diagnostics, summary);
  const auto compared = RunProbe(input, {"--compare"});
  EXPECT_EQ(compared.status, 0);
  EXPECT_EQ(compared.diagnostics, summary);
  EXPECT_EQ(compared.output,
            "(set-logic QF_SAT)\n(declare-const p Bool)\n"
            "(assert (or false\n(xor p\n|p|)\n))\n(check-sat)\n");

  constexpr std::string_view with_definition =
      "(set-logic QF_SAT)(define-const n Bool true)(assert n)(check-sat)(exit)";
  const auto clean_definition = RunProbe(with_definition, {"--compare"});
  ASSERT_EQ(clean_definition.status, 0);
  const auto ignored_suffix =
      RunProbe(std::string(with_definition) + "\xff dandelion_recovered_0 (",
               {"--compare"});
  SCOPED_TRACE(with_definition);
  EXPECT_EQ(ignored_suffix.status, 0);
  EXPECT_EQ(ignored_suffix.diagnostics, clean_definition.diagnostics);
  EXPECT_EQ(ignored_suffix.output, clean_definition.output);

  for (auto options : {std::initializer_list<std::string_view>{"--invalid"},
                       {"--compare", "--compare"}}) {
    const auto result = RunProbe(input, options);
    EXPECT_EQ(result.status, 1);
    EXPECT_EQ(result.output, "");
    EXPECT_EQ(result.diagnostics,
              "Usage: dandelion_frontend_probe [--compare] < input.smt2\n");
  }

  struct Rejection {
    // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
    std::string_view script;
    int status;
    std::string_view diagnostic;
    // NOLINTEND(readability-identifier-naming)
  };
  constexpr std::array failures{
      Rejection{"(set-logic ALL)", 1, "needs check-sat"},
      Rejection{"(set-logic ALL)(check-sat 1)", 1, "1:16"},
      Rejection{"(set-logic ALL)(assert 0)(check-sat)", 1, "1:24"},
      Rejection{"(set-logic ALL)(declare-const big (_ BitVec 4294967296))"
                "(check-sat)",
                1, "representation exhausted"},
      Rejection{"(set-logic ALL)(reset-assertions)(check-sat)", 2,
                "outside the input profile"},
      Rejection{"(set-option :produce-models true)(set-logic ALL)"
                "(check-sat)(get-value (@value0))",
                3, "requires its model"}};
  for (const auto& [script, status, diagnostic] : failures) {
    SCOPED_TRACE(script);
    for (auto options :
         {std::initializer_list<std::string_view>{}, {"--compare"}}) {
      const auto result = RunProbe(script, options);
      EXPECT_EQ(result.status, status);
      EXPECT_EQ(result.output, "");
      EXPECT_NE(result.diagnostics.find(diagnostic), std::string::npos)
          << result.diagnostics;
    }
  }

  constexpr std::string_view definition =
      "(set-logic QF_SAT)(define-fun f ((x Bool)) Bool (not x))"
      "(assert (f false))(check-sat)";
  const auto normal = RunProbe(definition);
  EXPECT_EQ(normal.status, 0);
  EXPECT_EQ(normal.output, "");
  EXPECT_EQ(normal.diagnostics,
            "QF_SAT commands=4 assertions=1 functions=0 definitions=1 "
            "datatype_groups=0\n");
  const auto inconclusive = RunProbe(definition, {"--compare"});
  EXPECT_EQ(inconclusive.status, 3);
  EXPECT_EQ(inconclusive.output, "");
  EXPECT_EQ(inconclusive.diagnostics,
            normal.diagnostics +
                "Comparison with non-nullary definitions is unavailable\n");
}

// Independent truth table for the Boolean fragment in the comparison cases.
bool EvalBool(const QueryContext& query, NodeView term, bool p) {
  const auto args = term.GetChildren();
  switch (term.GetKind()) {
    case Kind::kBoolValue:
      return term.GetValue()->GetBool().value();
    case Kind::kApply: {
      const auto decl = Applied(term);
      if (const auto* def = query.FindDefinition(decl))
        return EvalBool(query, def->body.Borrow(), p);
      if (query.HasFunction(decl) && decl.GetNameHint() == "p") return p;
      break;
    }
    case Kind::kNot:
      return !EvalBool(query, args[0], p);
    case Kind::kOr:
      for (const auto arg : args)
        if (EvalBool(query, arg, p)) return true;
      return false;
    case Kind::kXor:
      return EvalBool(query, args[0], p) != EvalBool(query, args[1], p);
    default:
      break;
  }
  throw std::runtime_error("Unexpected term in Boolean comparison fixture");
}

TEST(FrontendProbe, ComparisonPairsEveryWrittenRootInSourceOrder) {
  constexpr std::string_view signature =
      "(set-logic QF_SAT)(declare-const p Bool)";
  constexpr std::string_view pair_p = "(xor p\n|p|)\n";
  constexpr std::string_view pair_not_p = "(xor (not p)\n(not |p|))\n";
  for (bool duplicate : {false, true}) {
    const auto script = std::string(signature) + "(assert p)(assert (not p))" +
                        (duplicate ? "(assert p)" : "") + "(check-sat)";
    SCOPED_TRACE(::testing::Message() << "FE-PROBE-XOR-02 " << script);
    const auto result = RunProbe(script, {"--compare"});
    ASSERT_EQ(result.status, 0) << result.diagnostics;
    const auto comparison = [&](std::string_view first, std::string_view second,
                                std::string_view final) {
      return "(set-logic QF_SAT)\n(declare-const p Bool)\n(assert (or false\n" +
             std::string(first) + std::string(second) + std::string(final) +
             "))\n(check-sat)\n";
    };
    EXPECT_EQ(result.output,
              comparison(pair_p, pair_not_p, duplicate ? pair_p : ""));
    const auto check = [](std::string_view text, bool expected) {
      const auto query = RequireQuery(Parse(text));
      ASSERT_EQ(query->GetAssertions().size(), 1);
      const auto root = query->GetAssertions()[0].term.Borrow();
      EXPECT_EQ(EvalBool(*query, root, false), expected);
      EXPECT_EQ(EvalBool(*query, root, true), expected);
    };
    check(result.output, false);
    check(comparison("(xor p\n(not |p|))\n", "(xor (not p)\n|p|)\n",
                     duplicate ? pair_p : ""),
          true);
    check(duplicate ? comparison(pair_p, pair_not_p, "(xor p\n(not |p|))\n")
                    : comparison(pair_p, "(xor (not p)\n|p|)\n", ""),
          true);
  }
}

TEST(FrontendProbe, EmptyComparisonAndFreshRecoveredDefinitionNames) {
  constexpr std::string_view empty = "(set-logic QF_SAT)(check-sat)";
  SCOPED_TRACE(::testing::Message() << "FE-PROBE-XOR-02 " << empty);
  const auto no_roots = RunProbe(empty, {"--compare"});
  EXPECT_EQ(no_roots.status, 0);
  EXPECT_EQ(no_roots.output,
            "(set-logic QF_SAT)\n(assert false)\n(check-sat)\n");

  constexpr std::string_view script =
      "(set-logic QF_SAT)(declare-const p Bool)"
      "(declare-const dandelion_recovered_0 Bool)"
      "(define-const first Bool p)(define-const second Bool (not first))"
      "(assert first)(assert second)(check-sat)";
  SCOPED_TRACE(script);
  const auto recovered = RunProbe(script, {"--compare"});
  ASSERT_EQ(recovered.status, 0) << recovered.diagnostics;
  EXPECT_EQ(
      recovered.output,
      "(set-logic QF_SAT)\n(declare-const p Bool)\n"
      "(declare-const dandelion_recovered_0 Bool)\n"
      "(define-const first Bool p)\n(define-const second Bool (not first))\n"
      "(define-fun dandelion_recovered__0 () Bool |p|)\n"
      "(define-fun dandelion_recovered__1 () Bool (not "
      "dandelion_recovered__0))\n"
      "(assert (or false\n(xor first\ndandelion_recovered__0)\n"
      "(xor second\ndandelion_recovered__1)\n))\n(check-sat)\n");
  const auto query = RequireQuery(Parse(recovered.output));
  const auto defs = query->GetDefinitions();
  ASSERT_EQ(defs.size(), 4);
  ASSERT_EQ(query->GetAssertions().size(), 1);
  const auto root = query->GetAssertions()[0].term.Borrow();
  ASSERT_EQ(root.GetKind(), Kind::kOr);
  const auto disjunction = root.GetChildren();
  ASSERT_EQ(disjunction.size(), 3);
  for (std::size_t i = 1; i < 3; ++i) {
    const auto pair = disjunction[i];
    ASSERT_EQ(pair.GetKind(), Kind::kXor);
    ASSERT_EQ(pair.GetChildren().size(), 2);
    EXPECT_EQ(Applied(pair.GetChildren()[0]), defs[i - 1].decl.Borrow());
    EXPECT_EQ(Applied(pair.GetChildren()[1]), defs[i + 1].decl.Borrow());
  }
  EXPECT_EQ(Applied(Argument(defs[3].body, 0)), defs[2].decl.Borrow());
  EXPECT_FALSE(EvalBool(*query, root, false));
  EXPECT_FALSE(EvalBool(*query, root, true));
}

}  // namespace
}  // namespace dandelion::frontend::testing

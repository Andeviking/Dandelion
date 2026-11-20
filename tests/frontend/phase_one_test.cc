#include "model_support.h"

#include "../node/node_test_peer.h"

namespace dandelion::frontend::testing {
namespace {

template <class T>
concept HasHistory = requires(const T& query) { query.GetNamedTerms(); } ||
                     requires(const T& query) { query.GetAnnotations(); } ||
                     requires(const T& query) { query.GetMetadata(); } ||
                     requires(const T& query) { query.GetDefinitions(); };
template <class T>
concept HasIdLookup =
    requires(const T& query, DeclId id) { query.FindDefinition(id); };
template <class T>
concept HasAnnotations = requires(const T& command) { command.annotations; };

static_assert(!HasHistory<QueryContext>);
static_assert(!HasIdLookup<QueryContext>);
static_assert(!HasAnnotations<TypedCommand>);
static_assert(
    std::same_as<decltype(&QueryContext::GetAssertions),
                 std::span<const Node> (QueryContext::*)() const noexcept>);
static_assert(std::same_as<decltype(&QueryContext::GetUserSorts),
                           std::span<const SortConstructor> (QueryContext::*)()
                               const noexcept>);
static_assert(
    std::same_as<decltype(&QueryContext::GetFunctions),
                 std::span<const Decl> (QueryContext::*)() const noexcept>);
static_assert(std::same_as<decltype(&QueryContext::GetDefinitionCount),
                           std::size_t (QueryContext::*)() const noexcept>);
static_assert(std::same_as<decltype(&QueryContext::GetDefinition),
                           const DefinedFunction& (QueryContext::*)(std::size_t)
                               const noexcept>);

Reader Fragmented(std::string_view text) {
  return [text,
          offset = std::size_t{0}](std::span<char> out) mutable -> ReadResult {
    const auto count =
        std::min({out.size(), text.size() - offset, offset % 3 + 1});
    std::copy_n(text.data() + offset, count, out.data());
    offset += count;
    return count;
  };
}

Session Open(std::string_view text, bool capture) {
  const ParseOptions options{.capture_source_records = capture};
  return Require(capture ? Session::FromReader(Fragmented(text), options, 19)
                         : Session::FromText(text, options, 19));
}

TEST(FrontendPhaseOne, QueryOwnsOnlyOrderedSemanticRootsAndDeclarations) {
  SCOPED_TRACE("FE-P1-QUERY");
  std::shared_ptr<const QueryContext> query;
  {
    const std::string text =
        "(set-option :global-declarations true)(set-logic ALL)(declare-sort U "
        "0)"
        "(declare-const a Bool)(define-fun f ((x Bool)) Bool (and a x))"
        "(define-fun bad ((x Int)) Int true)(declare-datatype D ((c)))"
        "(define-fun g ((x Bool)) Bool (f x))(declare-const unused U)"
        "(assert (g true))(assert (g true))(check-sat)";
    auto session = Open(text, false);
    for (int i = 0; i < 5; ++i) Require(Next(session));
    ExpectError(Next(session), DiagnosticCode::kInvalidInput);
    for (int i = 0; i < 5; ++i) Require(Next(session));
    query = RequireCheck(session);
  }
  ASSERT_EQ(query->GetFunctions().size(), 2);
  ASSERT_EQ(query->GetUserSorts().size(), 1);
  ASSERT_EQ(query->GetDatatypeGroups().size(), 1);
  ASSERT_EQ(query->GetAssertions().size(), 2);
  ASSERT_EQ(query->GetDefinitionCount(), 2);
  EXPECT_EQ(query->GetAssertions()[0], query->GetAssertions()[1]);
  EXPECT_EQ(query->GetLogic(), "ALL");
  EXPECT_TRUE(query->GetOptions().global_declarations);
  EXPECT_FALSE(query->GetManager().IsNull());
  const auto& f = query->GetDefinition(0);
  const auto& g = query->GetDefinition(1);
  EXPECT_EQ(query->FindDefinition(f.decl.Borrow()), &f);
  EXPECT_EQ(query->FindDefinition(g.decl.Borrow()), &g);
  EXPECT_EQ(Applied(query->GetAssertions()[0].Borrow()), g.decl.Borrow());
  EXPECT_EQ(Applied(g.body.Borrow()), f.decl.Borrow());
  EXPECT_EQ(Applied(Argument(f.body, 0)), query->GetFunctions()[0].Borrow());
  EXPECT_EQ(query->GetFunctions()[1].Borrow().GetRange()->GetSortConstructor(),
            query->GetUserSorts()[0].Borrow());
  const auto foreign =
      RequireQuery(Parse("(set-logic ALL)(declare-const a Bool)"
                         "(define-fun f ((x Bool)) Bool x)(check-sat)"));
  for (const auto decl :
       {DeclView{}, foreign->GetFunctions()[0].Borrow(),
        foreign->GetDefinition(0).decl.Borrow(), f.formals[0].Borrow(),
        Require(query->GetDatatypeGroups()[0].Borrow().GetConstructor(0, 0))}) {
    EXPECT_FALSE(query->HasFunction(decl));
    EXPECT_EQ(query->FindDefinition(decl), nullptr);
  }
  EXPECT_FALSE(query->HasFunction(f.decl.Borrow()));
  for (const auto& decl : query->GetFunctions()) {
    EXPECT_TRUE(query->HasFunction(decl.Borrow()));
    EXPECT_EQ(query->FindDefinition(decl.Borrow()), nullptr);
  }
}

TEST(FrontendPhaseOne, AliasesReuseExistingRootsWithoutAllocatingWrappers) {
  SCOPED_TRACE("FE-P1-ALIAS");
  for (const bool capture : {false, true}) {
    auto session = Open(
        "(set-logic QF_LIA)(declare-const g Int)(assert (= (+ g 1) 0))"
        "(define-fun a () Int (+ g 1))(define-const b Int a)(define-fun c () "
        "Int b)"
        "(assert (= a b (as b Int) c (let ((g 7)) c)))(assert (= (let ((b 9)) "
        "b) 9))"
        "(define-fun id ((x Int)) Int x)(assert (= (id 2) 2))"
        "(define-const d Int (id 2))(assert (= d (id 2)))(check-sat)",
        capture);
    RequireNext(session, CommandKind::kSetLogic);
    RequireNext(session, CommandKind::kDeclareConst);
    RequireNext(session, CommandKind::kAssert);
    const auto original = Argument(session.GetPrefix().GetAssertions()[0], 0);
    auto manager =
        Require(NodeManager::Attach(session.GetPrefix().GetManager()));
    const auto before = NodeTestPeer::GetStats(manager);
    RequireNext(session, CommandKind::kDefineFun);
    RequireNext(session, CommandKind::kDefineConst);
    RequireNext(session, CommandKind::kDefineFun);
    const auto after = NodeTestPeer::GetStats(manager);
    EXPECT_EQ(after.declarations, before.declarations);
    EXPECT_EQ(after.decl_instances, before.decl_instances);
    EXPECT_EQ(after.node_allocations, before.node_allocations);
    EXPECT_EQ(session.GetPrefix().GetDefinitionCount(), 0);
    EXPECT_EQ(session.GetPrefix().GetFunctions().size(), 1);
    while (const auto command = Require(Next(session))) {
      if (!command->query) continue;
      const auto& q = *command->query;
      EXPECT_EQ(q.GetDefinitionCount(), 1);
      ASSERT_EQ(q.GetAssertions().size(), 5);
      for (const auto child : q.GetAssertions()[1].Borrow().GetChildren())
        EXPECT_EQ(child, original);
      EXPECT_EQ(IntegerValue(Argument(q.GetAssertions()[2], 0)), "9");
      EXPECT_EQ(Argument(q.GetAssertions()[3], 0),
                Argument(q.GetAssertions()[4], 0));
      EXPECT_EQ(Argument(q.GetAssertions()[4], 0),
                Argument(q.GetAssertions()[4], 1));
    }
  }
}

TEST(FrontendPhaseOne, InvalidAliasBodiesAndRanksDoNotReserveNames) {
  SCOPED_TRACE("FE-P1-ALIAS-ERROR");
  for (const bool capture : {false, true}) {
    for (const std::string_view form : {"define-const ", "define-fun "}) {
      const auto define = [&](std::string_view name, std::string_view body) {
        return "(" + std::string(form) + std::string(name) +
               (form == "define-fun " ? " () Int " : " Int ") +
               std::string(body) + ")";
      };
      for (const auto& [body, blame] :
           {std::pair<std::string_view, std::string_view>{"(+ true 1)", "true"},
            {"false", "false"},
            {"alias", "alias"},
            {"later", "later"},
            {"(let ((unused (+ false 1))) 0)", "false"}}) {
        const auto start = "(set-logic ALL)" + define("alias", "");
        const auto text = "(set-logic ALL)" + define("alias", body) +
                          define("alias", "(let ((x 1)) (+ x 2))") +
                          "(assert (= (alias 0) 3))(assert (as alias "
                          "Bool))(assert (= alias alias))(check-sat)";
        auto session = Open(text, capture);
        RequireNext(session, CommandKind::kSetLogic);
        const auto error = Next(session);
        ExpectError(error, DiagnosticCode::kInvalidInput);
        ASSERT_FALSE(error);
        const auto begin = start.size() - 1 + body.find(blame);
        const auto end = begin + blame.size();
        EXPECT_EQ(error.error().where,
                  (SourceSpan{19, {begin, 1, begin + 1}, {end, 1, end + 1}}));
        Require(Next(session));
        ExpectError(Next(session), DiagnosticCode::kInvalidInput);
        ExpectError(Next(session), DiagnosticCode::kInvalidInput);
        RequireNext(session, CommandKind::kAssert);
        const auto query = RequireCheck(session);
        EXPECT_EQ(Argument(query->GetAssertions()[0], 0).GetKind(),
                  Kind::kIntAdd);
      }
      const auto text =
          "(set-logic ALL)(declare-const free Int)"
          "(assert (= (! 1 :named named) 1))" +
          define("use", "1") + define("|free|", "1") + define("|named|", "2") +
          define("|use|", "2") + define("+", "2") +
          "(assert (= free named use))(check-sat)";
      auto session = Open(text, capture);
      for (int i = 0; i < 4; ++i) Require(Next(session));
      for (int i = 0; i < 4; ++i)
        ExpectError(Next(session), DiagnosticCode::kInvalidInput);
      RequireNext(session, CommandKind::kAssert);
      const auto query = RequireCheck(session);
      const auto operands = query->GetAssertions()[1].Borrow().GetChildren();
      EXPECT_EQ(Applied(operands[0]), query->GetFunctions()[0].Borrow());
      EXPECT_EQ(operands[1], operands[2]);
    }
  }
}

std::int64_t EvaluateInt(
    const QueryContext& q, NodeView term,
    std::span<const std::pair<DeclView, std::int64_t>> locals = {}) {
  const auto children = term.GetChildren();
  switch (term.GetKind()) {
    case Kind::kIntValue:
      return std::stoll(IntegerValue(term));
    case Kind::kIntSub:
      return EvaluateInt(q, children[0], locals) -
             EvaluateInt(q, children[1], locals);
    case Kind::kIntAdd:
      return EvaluateInt(q, children[0], locals) +
             EvaluateInt(q, children[1], locals);
    case Kind::kApply: {
      const auto decl = Applied(term);
      if (const auto* definition = q.FindDefinition(decl)) {
        std::vector<std::pair<DeclView, std::int64_t>> values;
        for (std::size_t i = 0; i < children.size(); ++i)
          values.emplace_back(definition->formals[i].Borrow(),
                              EvaluateInt(q, children[i], locals));
        return EvaluateInt(q, definition->body.Borrow(), values);
      }
      for (const auto& [formal, value] : locals)
        if (decl == formal) return value;
      if (decl == q.GetFunctions()[0].Borrow()) return 7;
      break;
    }
    default:
      break;
  }
  throw std::runtime_error("Unexpected integer fixture");
}

TEST(FrontendPhaseOne, TemplatesPreserveOrderedSubstitutionAndCapturedGlobal) {
  SCOPED_TRACE("FE-P1-TEMPLATE");
  const auto query = RequireQuery(Parse(
      "(set-logic QF_LIA)(declare-const captured Int)"
      "(define-fun f ((a Int) (b Int)) Int (- a b))(define-const zero Int 0)"
      "(define-fun g ((x Int)) Int (f x (f x 1)))"
      "(define-fun h ((x Int)) Int (+ captured x))"
      "(assert (= (f 5 2) 3))(assert (= (f 2 5) (- 3)))"
      "(assert (= (g 5) 1))(assert (let ((captured 100)) (= (h zero) "
      "7)))(check-sat)"));
  ASSERT_EQ(query->GetDefinitionCount(), 3);
  const auto& f = query->GetDefinition(0);
  const auto& g = query->GetDefinition(1);
  ASSERT_EQ(f.formals.size(), 2);
  EXPECT_NE(f.formals[0], f.formals[1]);
  EXPECT_EQ(Applied(Argument(f.body, 0)), f.formals[0].Borrow());
  EXPECT_EQ(Applied(Argument(f.body, 1)), f.formals[1].Borrow());
  EXPECT_EQ(Applied(g.body.Borrow()), f.decl.Borrow());
  EXPECT_EQ(Applied(Argument(g.body, 1)), f.decl.Borrow());
  constexpr std::array expected{3, -3, 1, 7};
  for (std::size_t i = 0; i < expected.size(); ++i)
    EXPECT_EQ(EvaluateInt(*query, Argument(query->GetAssertions()[i], 0)),
              expected[i]);
  for (const auto invalid :
       {"(define-fun f ((x Int)) Int (f x))",
        "(define-fun f ((x Int) (x Bool)) Int x)",
        "(define-fun f ((x Int)) Bool x)",
        "(define-fun f ((x Int)) Int x)(assert (= (f 1 2) 1))",
        "(define-fun f ((x Int)) Int 0)(assert (= (f true) 0))"})
    ExpectError(Parse("(set-logic QF_LIA)" + std::string(invalid)),
                DiagnosticCode::kInvalidInput);
}

TEST(FrontendPhaseOne, SourceHistoryOwnsOccurrencesAcrossAdaptersAndSealing) {
  SCOPED_TRACE("FE-P1-SOURCE FE-P1-NAME");
  SourceRecords copy;
  std::shared_ptr<const QueryContext> query;
  for (const bool capture : {false, true}) {
    const std::string text =
        "(set-info :status unknown)(set-logic ALL)(declare-sort |U| 0)"
        "(declare-fun |u f| ((Array  Int |U|)) |U|)"
        "(define-fun |a| () Int (! (! 1 :named |inner|) :named outer :tag (|x| "
        " 2)))"
        "(define-const b Int a)(define-fun id ((x Int)) Int (+ x inner))"
        "(assert (= (id b) (+ outer inner)))(assert (= (id b) (+ outer inner)))"
        "(check-sat)(set-info :status sat)";
    auto session = Open(text, capture);
    if (capture) {
      EXPECT_TRUE(session.GetSourceRecords()->functions.empty());
    }
    while (auto command = Require(Next(session))) {
      EXPECT_EQ(session.GetSourceRecords() != nullptr, capture);
      if (command->query) query = command->query;
    }
    ASSERT_TRUE(query);
    ASSERT_EQ(query->GetAssertions().size(), 2);
    EXPECT_EQ(query->GetAssertions()[0], query->GetAssertions()[1]);
    EXPECT_EQ(query->GetDefinitionCount(), 1);
    EXPECT_EQ(query->GetFunctions()[0].Borrow().GetNameHint(), "u f");
    if (capture) {
      copy = *session.GetSourceRecords();
      ASSERT_EQ(copy.assertions.size(), 2);
      EXPECT_EQ(copy.assertions[0].term, query->GetAssertions()[0]);
      for (const auto& occurrence : copy.assertions) {
        const auto [source, begin, end] = occurrence.where;
        EXPECT_EQ(source, 19);
        EXPECT_EQ(text.substr(begin.offset, end.offset - begin.offset),
                  "(= (id b) (+ outer inner))");
      }
      ASSERT_EQ(copy.annotations.size(), 2);
      EXPECT_LT(copy.annotations[1].where.begin.offset,
                copy.annotations[0].where.begin.offset);
    }
  }
  ASSERT_EQ(copy.user_sorts.size(), 1);
  EXPECT_EQ(copy.user_sorts[0].source_name, "|U|");
  ASSERT_EQ(copy.functions.size(), 1);
  EXPECT_EQ(copy.functions[0].source_name, "|u f|");
  EXPECT_EQ(copy.functions[0].source_rank,
            (std::vector<std::string>{"(Array Int |U|)", "|U|"}));
  ASSERT_EQ(copy.definitions.size(), 3);
  EXPECT_EQ(copy.definitions[0].kind, CommandKind::kDefineFun);
  EXPECT_EQ(copy.definitions[0].source_name, "|a|");
  EXPECT_EQ(copy.definitions[1].kind, CommandKind::kDefineConst);
  EXPECT_EQ(copy.definitions[0].body, copy.definitions[1].body);
  EXPECT_FALSE(copy.definitions[0].definition_ordinal);
  EXPECT_FALSE(copy.definitions[1].definition_ordinal);
  EXPECT_EQ(copy.definitions[2].definition_ordinal, 0);
  EXPECT_EQ(copy.definitions[2].body, query->GetDefinition(0).body);
  ASSERT_EQ(copy.named_terms.size(), 2);
  EXPECT_EQ(copy.named_terms[0].name, "inner");
  EXPECT_EQ(copy.named_terms[1].name, "outer");
  EXPECT_EQ(copy.named_terms[0].term, copy.named_terms[1].term);
  EXPECT_EQ(copy.named_terms[0].term, copy.definitions[0].body);
  EXPECT_EQ(copy.annotations[0].attributes[0].value, "|inner|");
  EXPECT_EQ(copy.annotations[1].attributes[1].value, "(|x| 2)");
  ASSERT_EQ(copy.metadata.size(), 2);
  EXPECT_EQ(copy.metadata[0].value, "unknown");
  EXPECT_EQ(copy.metadata[1].value, "sat");
}

TEST(FrontendPhaseOne, EscapeMarksAreLimitedToLexicalScopes) {
  SCOPED_TRACE("FE-P1-ESCAPE");
  for (const bool capture : {false, true}) {
    constexpr std::size_t kDepth = 128;
    std::string operand;
    for (std::size_t i = 0; i < kDepth; ++i) operand += "(! ";
    operand += "(+ 1 2)";
    for (std::size_t i = 0; i < kDepth; ++i)
      operand += " :named n" + std::to_string(i) + ")";
    for (const bool binder : {false, true}) {
      const auto text = std::string("(set-logic ALL)(assert ") +
                        (binder ? "(let ((x 3)) " : "") + "(= " + operand +
                        (binder ? " x)))" : " 3))");
      auto session = Open(text, capture);
      RequireNext(session, CommandKind::kSetLogic);
      RequireNext(session, CommandKind::kAssert);
      const auto stats = session.GetStats();
      EXPECT_EQ(stats.closure_visits, kDepth);
      EXPECT_EQ(stats.escape_operands, binder ? 4u : 0u);
      const auto root = session.GetPrefix().GetAssertions()[0].Borrow();
      EXPECT_EQ(root.GetChildren()[0].GetKind(), Kind::kIntAdd);
      EXPECT_EQ(IntegerValue(root.GetChildren()[1]), "3");
    }
  }
}

TEST(FrontendPhaseOne, CompatibleAndStrictMetadataAreIndependentOfCapture) {
  SCOPED_TRACE("FE-P1-METADATA");
  for (const bool capture : {false, true}) {
    for (const bool strict : {false, true}) {
      constexpr std::string_view text =
          "(set-info :category \"crafted\")(set-info :category \"random\")"
          "(set-info :category \"industrial\")(set-info :category \"check\")"
          "(set-info :category \"a\"\"b\")(set-info :category crafted)"
          "(set-info :category)(set-info :smt-lib-version 2.6)"
          "(set-info :smt-lib-version 2.7)(set-info :smt-lib-version 2.8)"
          "(set-info :smt-lib-version 2)(set-info :smt-lib-version \"2.7\")"
          "(set-info :status sat)(set-info :status unknown)(set-info :status "
          "unsat)"
          "(set-info :status \"sat\")(set-info :source |a|)(set-info :source "
          "\"b\")"
          "(set-info :license \"MIT\")(set-info :opaque)(set-info :opaque "
          "(:keyword))"
          "(set-logic QF_LIA)(declare-const x Int)(assert (= (* x x) "
          "4))(check-sat)";
      auto session = Require(
          Session::FromReader(Fragmented(text),
                              ParseOptions{.capture_source_records = capture,
                                           .strict_metadata = strict},
                              19));
      for (int i = 0; i < 3; ++i) RequireNext(session, CommandKind::kSetInfo);
      for (const auto value : {"\"check\"", "\"a\"\"b\""}) {
        if (strict) {
          const auto failed = Next(session);
          ExpectError(failed, DiagnosticCode::kInvalidInput);
          ASSERT_FALSE(failed);
          EXPECT_EQ(failed.error().where.begin.offset, text.find(value));
        } else {
          RequireNext(session, CommandKind::kSetInfo);
        }
      }
      for (int i = 0; i < 2; ++i)
        ExpectError(Next(session), DiagnosticCode::kInvalidInput);
      for (int i = 0; i < 2; ++i) RequireNext(session, CommandKind::kSetInfo);
      ExpectError(Next(session), DiagnosticCode::kUnsupported);
      for (int i = 0; i < 2; ++i)
        ExpectError(Next(session), DiagnosticCode::kInvalidInput);
      for (int i = 0; i < 3; ++i) RequireNext(session, CommandKind::kSetInfo);
      ExpectError(Next(session), DiagnosticCode::kInvalidInput);
      for (int i = 0; i < 5; ++i) RequireNext(session, CommandKind::kSetInfo);
      if (capture) {
        const auto& records = session.GetSourceRecords()->metadata;
        ASSERT_EQ(records.size(), strict ? 13u : 15u);
        EXPECT_EQ(records[records.size() - 6].value, "unsat");
        EXPECT_FALSE(records[records.size() - 2].value);
        EXPECT_EQ(records.back().value, "(:keyword)");
      } else {
        EXPECT_EQ(session.GetSourceRecords(), nullptr);
      }
      CapturedSink sink;
      int calls = 0;
      const auto run = dandelion::frontend::Run(
          session,
          [&](const QueryContext& q, SessionOptions) -> Outcome<CheckResult> {
            ++calls;
            EXPECT_EQ(Argument(q.GetAssertions()[0], 0).GetKind(),
                      Kind::kIntMul);
            return CheckResult{CheckAnswer::kSat, UnknownReason::kUnknown,
                               nullptr};
          },
          sink.Get());
      EXPECT_EQ(run.status, 0);
      EXPECT_EQ(calls, 1);
      EXPECT_EQ(sink.output, "sat\n");
    }
  }
  ExpectError(Parse("(set-info :opaque 1 2)"), DiagnosticCode::kSyntax);
  ExpectError(Parse("(set-info :opaque (x)"), DiagnosticCode::kSyntax);
}

TEST(FrontendPhaseOne, RollbackPreservesAllCommittedHistoriesAndAliases) {
  SCOPED_TRACE("FE-P1-ROLLBACK");
  for (const bool capture : {false, true}) {
    constexpr std::string_view text =
        "(set-info :tag prefix)(set-logic ALL)(declare-sort U 0)(declare-const "
        "p Bool)"
        "(define-const a Bool (! (not p) :named keep))(assert a)"
        "(assert (and (! keep :named fresh) absent))"
        "(define-const wrong Int (! keep :named fresh))"
        "(assert (and keep (! a :named fresh)))(check-sat)(check-sat)";
    auto session = Open(text, capture);
    for (int i = 0; i < 6; ++i) Require(Next(session));
    const auto prior = session.GetPrefix().GetAssertions()[0];
    for (int i = 0; i < 2; ++i) {
      ExpectError(Next(session), DiagnosticCode::kInvalidInput);
      EXPECT_EQ(session.GetPrefix().GetAssertions().size(), 1);
      EXPECT_EQ(session.GetPrefix().GetAssertions()[0], prior);
      EXPECT_EQ(session.GetPrefix().GetDefinitionCount(), 0);
      if (capture) {
        const auto& r = *session.GetSourceRecords();
        EXPECT_EQ((std::array{r.assertions.size(), r.user_sorts.size(),
                              r.functions.size(), r.definitions.size(),
                              r.named_terms.size(), r.annotations.size(),
                              r.metadata.size()}),
                  (std::array<std::size_t, 7>{1, 1, 1, 1, 1, 1, 1}));
        EXPECT_EQ(r.named_terms[0].term, prior);
      }
    }
    RequireNext(session, CommandKind::kAssert);
    session.FailAllocationAfter(0);
    ExpectError(session.ReadCommand(), DiagnosticCode::kResource);
    EXPECT_EQ(session.GetPhase(), SessionPhase::kBuilding);
    EXPECT_EQ(session.GetStats().alias_roots_released, 0);
    const auto query = RequireCheck(session);
    ExpectChildren(query->GetAssertions()[1].Borrow(),
                   {prior.Borrow(), prior.Borrow()});
    if (capture) {
      EXPECT_EQ(session.GetSourceRecords()->named_terms.size(), 2);
    }
    auto invalid = Open(
        "(set-logic ALL)(assert (and (! true :named n) absent))(check-sat)",
        capture);
    CapturedSink sink;
    EXPECT_EQ(
        dandelion::frontend::Run(
            invalid,
            [](const QueryContext&, SessionOptions) -> Outcome<CheckResult> {
              ADD_FAILURE() << "Rejected constraint reached the engine";
              return CheckResult{CheckAnswer::kSat, UnknownReason::kUnknown,
                                 nullptr};
            },
            sink.Get())
            .status,
        1);
    auto malformed =
        Open("(set-logic ALL)(assert (! true :named temporary)", capture);
    RequireNext(malformed, CommandKind::kSetLogic);
    ExpectError(malformed.ReadCommand(), DiagnosticCode::kSyntax);
    if (capture) {
      EXPECT_TRUE(malformed.GetSourceRecords()->named_terms.empty());
    }
  }
}

TEST(FrontendPhaseOne, DefinitionAllocationCheckpointsRollBackEveryOwner) {
  SCOPED_TRACE("FE-P1-ROLLBACK");
  for (const bool capture : {false, true}) {
    constexpr std::string_view text =
        "(set-logic ALL)(define-const prior Int 4)"
        "(define-fun |f| ((x Int)) Int (+ x (! prior :named n)))"
        "(define-fun |f| ((x Int)) Int (+ x (! prior :named n)))"
        "(assert (= (f 1) n))(check-sat)";
    std::size_t failures = 0;
    for (std::size_t checkpoint = 0; checkpoint < 64; ++checkpoint) {
      auto session = Open(text, capture);
      RequireNext(session, CommandKind::kSetLogic);
      RequireNext(session, CommandKind::kDefineConst);
      session.FailAllocationAfter(checkpoint);
      const auto result = Next(session);
      if (result) {
        EXPECT_EQ(session.GetPrefix().GetDefinitionCount(), 1);
        break;
      }
      ++failures;
      ExpectError(result, DiagnosticCode::kResource);
      EXPECT_EQ(session.GetPrefix().GetDefinitionCount(), 0);
      EXPECT_EQ(session.GetPhase(), SessionPhase::kBuilding);
      if (capture) {
        const auto& records = *session.GetSourceRecords();
        EXPECT_TRUE(records.named_terms.empty());
        EXPECT_TRUE(records.annotations.empty());
        ASSERT_EQ(records.definitions.size(), 1);
        EXPECT_EQ(IntegerValue(records.definitions[0].body.Borrow()), "4");
      }
      RequireNext(session, CommandKind::kDefineFun);
      RequireNext(session, CommandKind::kAssert);
      const auto query = RequireCheck(session);
      EXPECT_EQ(query->GetDefinitionCount(), 1);
      if (capture) {
        const auto& records = *session.GetSourceRecords();
        EXPECT_EQ(records.definitions.size(), 2);
        EXPECT_EQ(records.named_terms.size(), 1);
        EXPECT_EQ(records.annotations.size(), 1);
      }
    }
    EXPECT_GE(failures, capture ? 5u : 3u);
    EXPECT_LT(failures, 64);
  }
}

TEST(FrontendPhaseOne, SealBatchReleasesEveryAliasOwnerAndPreservesQueryRoots) {
  SCOPED_TRACE("FE-P1-SEAL");
  for (const bool capture : {false, true}) {
    const auto script =
        "(set-logic ALL)(declare-const p Bool)"
        "(define-const unused Int (+ 80 90))(define-const copy Int unused)"
        "(define-const retained Bool (not p))(define-fun f ((x Bool)) Bool "
        "(and x retained))"
        "(assert retained)(assert retained)(check-sat)";
    std::shared_ptr<const QueryContext> query;
    auto session = Open(script, capture);
    for (int i = 0; i < 8; ++i) Require(Next(session));
    auto manager =
        Require(NodeManager::Attach(session.GetPrefix().GetManager()));
    const auto before = NodeTestPeer::GetStats(manager);
    query = RequireCheck(session);
    EXPECT_EQ(session.GetStats().alias_roots_released, 3);
    EXPECT_EQ(before.live_nodes - NodeTestPeer::GetStats(manager).live_nodes,
              capture ? 0u : 3u);
    EXPECT_EQ(query->GetAssertions()[0], query->GetAssertions()[1]);
    EXPECT_EQ(Argument(query->GetDefinition(0).body, 1),
              query->GetAssertions()[0].Borrow());
    EXPECT_TRUE(query->HasFunction(query->GetFunctions()[0].Borrow()));
    auto retained = query->GetAssertions()[0];
    session = Open("", false);
    query.reset();
    EXPECT_EQ(NodeTestPeer::GetStats(manager).live_nodes, 2);
    retained = {};
    EXPECT_EQ(NodeTestPeer::GetStats(manager).live_nodes, 0);
  }

  // Node retains its minimum-size index. Grow the table to exercise
  // detachment when its entire graph is owned only by the alias.
  std::string text = "(set-logic ALL)(define-const unused Bool ";
  constexpr std::size_t kDepth = 128;
  for (std::size_t i = 0; i < kDepth; ++i) text += "(not ";
  text += "false" + std::string(kDepth + 1, ')') + "(check-sat)";
  auto session = Open(text, false);
  RequireNext(session, CommandKind::kSetLogic);
  RequireNext(session, CommandKind::kDefineConst);
  auto manager = Require(NodeManager::Attach(session.GetPrefix().GetManager()));
  EXPECT_EQ(NodeTestPeer::GetStats(manager).live_nodes, kDepth + 1);
  RequireCheck(session);
  EXPECT_EQ(session.GetStats().alias_roots_released, 1);
  EXPECT_EQ(NodeTestPeer::GetStats(manager).live_nodes, 0);
  EXPECT_EQ(NodeTestPeer::GetStats(manager).table_capacity, 0);
}

TEST(FrontendPhaseOne, ModelAdaptersKeepAliasRankAndForwardAllOptions) {
  SCOPED_TRACE("FE-P1-MODEL FE-P1-INSPECT");
  std::string prior_model;
  for (const bool capture : {false, true}) {
    std::shared_ptr<ModelOwner> model;
    {
      std::string script =
          "(set-option :produce-models true)(set-info :tag original)"
          "(set-logic ALL)(declare-const |plain| Int)(declare-const |needs "
          "space| Int)"
          "(define-sort S0 () Int)(define-sort Pair (T) (Array T T))";
      for (int i = 1; i <= 14; ++i) {
        const auto prev = "S" + std::to_string(i - 1);
        script += "(define-sort S" + std::to_string(i) + " () (Array " + prev +
                  " " + prev + "))";
      }
      script +=
          "(define-sort Z14 () S14)(declare-fun unused ((Array S13 S13) "
          "(Array Int Bool) (Pair Bool)) Int)"
          "(define-const alias Int (! (+ 8 7) :named named))"
          "(define-fun id ((x Int)) Int x)(check-sat)";
      auto session = Open(script, capture);
      std::shared_ptr<const QueryContext> query;
      while (const auto command = Require(Next(session)))
        if (command->query) query = command->query;
      model = Require(ModelOwner::Create(
          query,
          std::make_unique<ControlledModel>(
              [](ModelOwner& owner, NodeView) {
                return IntegerScalar(owner, 15);
              },
              ControlledModel::Description{},
              [](ModelOwner& owner) -> Outcome<ModelExport> {
                auto manager =
                    Require(NodeManager::Attach(owner.GetQuery().GetManager()));
                const auto declarations = owner.GetQuery().GetFunctions();
                const auto domain = declarations[2].Borrow().GetDomain();
                return ModelExport{
                    {{declarations[0],
                      {},
                      Require(manager.MakeInt(numeric::IntegerView(1)))},
                     {declarations[2],
                      {Require(manager.MakeFormal("a", domain[0])),
                       Require(manager.MakeFormal("b", domain[1])),
                       Require(manager.MakeFormal("c", domain[2]))},
                      ApplyDecl(manager, declarations[0].Borrow())},
                     {declarations[1],
                      {},
                      Require(manager.MakeInt(numeric::IntegerView(2)))}},
                    {}};
              }),
          CheckAnswer::kSat));
    }
    auto legacy = Require(Session::ForModelText(model, "(get-model)"));
    CapturedSink sink;
    EXPECT_EQ(dandelion::frontend::Run(legacy, {}, sink.Get()).status, 0);
    EXPECT_TRUE(sink.output.contains("(define-fun plain () S0 1)"));
    EXPECT_TRUE(sink.output.contains("(define-fun |needs space| () S0 2)"));
    EXPECT_TRUE(sink.output.contains(" S14)"));
    EXPECT_TRUE(sink.output.contains("(Array S0 Bool)"));
    EXPECT_TRUE(sink.output.contains("(Pair Bool)"));
    const auto rank_begin = sink.output.find(" S14)");
    ASSERT_NE(rank_begin, std::string::npos);
    const auto asymmetric = sink.output.find("(Array S0 Bool)");
    ASSERT_NE(asymmetric, std::string::npos);
    const auto pair = sink.output.find("(Pair Bool)");
    ASSERT_NE(pair, std::string::npos);
    const auto ranks =
        "(get-value (((as const (Array " +
        sink.output.substr(rank_begin + 1, 3) +
        " Bool)) true)"
        " ((as const (Array " +
        sink.output.substr(asymmetric,
                           std::string_view("(Array S0 Bool)").size()) +
        " Bool)) true) ((as const (Array " +
        sink.output.substr(pair, std::string_view("(Pair Bool)").size()) +
        " Bool)) true)))";
    auto rank_session = Require(Session::ForModelText(model, ranks));
    const auto rank_values =
        RequireNext(rank_session, CommandKind::kGetValue).values;
    const auto domain =
        model->GetQuery().GetFunctions()[2].Borrow().GetDomain();
    ASSERT_EQ(rank_values.size(), domain.size());
    for (std::size_t i = 0; i < rank_values.size(); ++i)
      EXPECT_EQ(rank_values[i].term.Borrow().GetSort().GetArrayIndex(),
                domain[i]);
    EXPECT_LT(sink.output.find("(define-fun plain"),
              sink.output.find("(define-fun unused"));
    EXPECT_LT(sink.output.size(), 250u);
    EXPECT_FALSE(sink.output.contains("(define-fun alias"));
    EXPECT_FALSE(sink.output.contains("(define-fun named"));
    if (capture) {
      EXPECT_EQ(sink.output, prior_model);
    }
    prior_model = sink.output;

    constexpr std::string_view text =
        "(set-info :category \"check\")(set-info :status sat)"
        "(get-value (|plain| alias (+ 8 7) (+ 7 8)))"
        "(get-value (((as const (Array S14 Bool)) true)))";
    for (const bool reader : {false, true}) {
      const ParseOptions options{.canonicalize_commutative = true,
                                 .capture_source_records = true,
                                 .strict_metadata = true};
      auto detached = Require(
          reader ? Session::ForModelReader(model, Fragmented(text), options, 19)
                 : Session::ForModelText(model, text, options, 19));
      ASSERT_TRUE(detached.GetSourceRecords()->metadata.empty());
      ASSERT_TRUE(detached.GetSourceRecords()->functions.empty());
      ExpectError(Next(detached), DiagnosticCode::kInvalidInput);
      EXPECT_TRUE(detached.GetSourceRecords()->metadata.empty());
      RequireNext(detached, CommandKind::kSetInfo);
      const auto command = RequireNext(detached, CommandKind::kGetValue);
      ASSERT_EQ(command.values.size(), 4);
      EXPECT_EQ(command.values[0].input, "|plain|");
      EXPECT_EQ(command.values[1].input, "alias");
      EXPECT_EQ(command.values[3].input, "(+ 7 8)");
      EXPECT_EQ(command.values[1].where.begin.offset, text.find("alias"));
      EXPECT_EQ(command.values[1].where.source, 19);
      EXPECT_EQ(command.values[1].term, command.values[2].term);
      EXPECT_EQ(command.values[2].term, command.values[3].term);
      const auto unseen = RequireNext(detached, CommandKind::kGetValue);
      EXPECT_EQ(unseen.values[0].term.Borrow().GetSort().GetArrayIndex(),
                model->GetQuery().GetFunctions()[2].Borrow().GetDomain()[0]);
      EXPECT_EQ(detached.GetSourceRecords()->metadata.size(), 1);

      auto defaults =
          Require(reader ? Session::ForModelReader(model, Fragmented(text),
                                                   ParseOptions{})
                         : Session::ForModelText(model, text, ParseOptions{}));
      EXPECT_EQ(defaults.GetSourceRecords(), nullptr);
      RequireNext(defaults, CommandKind::kSetInfo);
      RequireNext(defaults, CommandKind::kSetInfo);
      const auto values = RequireNext(defaults, CommandKind::kGetValue).values;
      EXPECT_EQ(values[1].input, command.values[1].input);
      EXPECT_EQ(values[1].where.begin, command.values[1].where.begin);
      EXPECT_EQ(values[1].term, values[2].term);
      EXPECT_NE(values[2].term, values[3].term);
    }
    auto other = Require(ModelOwner::Create(model->GetSharedQuery(),
                                            std::make_unique<ControlledModel>(),
                                            CheckAnswer::kSat));
    auto reader = Require(Session::ForModelReader(
        model, Fragmented("(get-value (alias))(get-value (named))")));
    ExpectError(reader.ReadCommand(other.get()), DiagnosticCode::kModelFailure);
    const auto named = RequireNext(reader, CommandKind::kGetValue);
    EXPECT_EQ(named.values[0].term.Borrow().GetKind(), Kind::kIntAdd);
  }
}

TEST(FrontendPhaseOne,
     AbstractReadbackKeepsSpansAndCapturesOnlyCommittedAnnotations) {
  SCOPED_TRACE("FE-P1-INSPECT");
  const auto query = RequireQuery(Parse(
      "(set-option :produce-models true)(set-logic ALL)(declare-sort U 0)"
      "(declare-const a (Array U Bool))(define-const alias (Array U Bool) a)"
      "(check-sat)"));
  auto owner = Require(ModelOwner::Create(
      query,
      std::make_unique<ControlledModel>(
          [](ModelOwner& model, NodeView term) -> Outcome<ValueRef> {
            return model.MakeValue(term.GetSort(), "array");
          }),
      CheckAnswer::kSat));
  auto expose = Require(Session::ForModelText(owner, "(get-value (a))"));
  CapturedSink sink;
  ASSERT_EQ(dandelion::frontend::Run(expose, {}, sink.Get()).status, 0);
  EXPECT_EQ(sink.output, "((a (as @value0 (Array U Bool))))\n");
  constexpr std::string_view text =
      "(get-value ((! alias :tag discarded) absent))"
      "(get-value (@value0 (as @value0 (Array U Bool)) (! alias :tag "
      "|written|) |a|))";
  for (const bool capture : {false, true}) {
    const ParseOptions options{.capture_source_records = capture};
    auto session =
        Require(Session::ForModelReader(owner, Fragmented(text), options, 19));
    ExpectError(Next(session), DiagnosticCode::kInvalidInput);
    if (capture) {
      EXPECT_TRUE(session.GetSourceRecords()->annotations.empty());
    }
    const auto typed = RequireNext(session, CommandKind::kGetValue);
    ASSERT_EQ(typed.values.size(), 4);
    EXPECT_EQ(typed.values[0].term, typed.values[1].term);
    EXPECT_EQ(typed.values[0].input, "(as @value0 (Array U Bool))");
    EXPECT_EQ(typed.values[1].input, typed.values[0].input);
    EXPECT_EQ(typed.values[2].input, "(! alias :tag |written|)");
    EXPECT_EQ(typed.values[3].input, "|a|");
    EXPECT_EQ(typed.values[2].term, typed.values[3].term);
    EXPECT_EQ(typed.values[0].term.Borrow().GetSort(),
              query->GetFunctions()[0].Borrow().GetRange());
    constexpr std::array<std::string_view, 4> spellings{
        "@value0", "(as @value0 (Array U Bool))", "(! alias :tag |written|)",
        "|a|"};
    for (std::size_t i = 0; i < spellings.size(); ++i) {
      const auto [source, begin, end] = typed.values[i].where;
      EXPECT_EQ(source, 19);
      EXPECT_EQ(text.substr(begin.offset, end.offset - begin.offset),
                spellings[i]);
    }
    if (capture) {
      const auto& annotations = session.GetSourceRecords()->annotations;
      ASSERT_EQ(annotations.size(), 1);
      EXPECT_EQ(annotations[0].where, typed.values[2].where);
      EXPECT_EQ(annotations[0].term, typed.values[2].term);
      EXPECT_EQ(annotations[0].attributes[0].value, "|written|");
    } else {
      EXPECT_EQ(session.GetSourceRecords(), nullptr);
    }
  }
}

TEST(FrontendPhaseOne, IndexedFactoriesAndWordLiteralsPreserveExactRanks) {
  SCOPED_TRACE("FE-P1-NODE");
  std::shared_ptr<const QueryContext> query;
  const auto typed = Require(Values(
      "((_ extract 3 1) #xff) ((_ zero_extend 8) #xff)"
      " ((_ rotate_left 18446744073709551617) #xff) ((_ rotate_right 2) #xff)"
      " #b1 (_ bv3 1) #xffffffffffffffff (_ bv36893488147419103231 64)"
      " #b10000000000000000000000000000000000000000000000000000000000000001"
      " (_ bv18446744073709551617 65) (_ bv1 64) (_ bv1 65)"
      " (_ bv55340232221128654849 65)",
      "(set-logic QF_UFBV)(declare-fun f ((_ BitVec 1) (_ BitVec 64)) (_ "
      "BitVec "
      "65))",
      &query));
  const auto& v = typed.values;
  ASSERT_EQ(v.size(), 13);
  auto manager = Require(NodeManager::Attach(query->GetManager()));
  const auto bv8 = Require(manager.MakeBvSort(8));
  const auto ff = Require(manager.MakeBvWordValue(bv8.Borrow(), 255));
  const std::array operand{ff.Borrow()};
  EXPECT_EQ(v[0].term, Require(manager.MakeWordIndexedBuiltin(
                           Kind::kBvExtract, std::array<std::uint64_t, 2>{3, 1},
                           operand)));
  EXPECT_EQ(v[1].term, Require(manager.MakeWordIndexedBuiltin(
                           Kind::kBvZeroExtend, std::array<std::uint64_t, 1>{8},
                           operand)));
  const auto big =
      Require(numeric::Integer::ParseDecimal("18446744073709551617"));
  const std::array indices{big};
  EXPECT_EQ(v[2].term, Require(manager.MakeBuiltin(
                           {Kind::kBvRotateLeft, indices, {}, {}}, operand)));
  EXPECT_EQ(v[3].term, Require(manager.MakeWordIndexedBuiltin(
                           Kind::kBvRotateRight,
                           std::array<std::uint64_t, 1>{2}, operand)));
  for (std::size_t i = 4; i < 10; i += 2) EXPECT_EQ(v[i].term, v[i + 1].term);
  EXPECT_NE(v[10].term, v[11].term);
  EXPECT_EQ(v[8].term, v[12].term);
  const auto f = query->GetFunctions()[0].Borrow();
  EXPECT_EQ(f.GetDomain()[0].GetWidth(), 1);
  EXPECT_EQ(f.GetDomain()[1].GetWidth(), 64);
  EXPECT_EQ(f.GetRange()->GetWidth(), 65);
  constexpr std::string_view rejected_extract =
      "(set-logic QF_BV)(assert (= ((_ extract 1 2) #xff) #b1))";
  const auto invalid = Parse(rejected_extract);
  ExpectError(invalid, DiagnosticCode::kInvalidInput);
  ASSERT_FALSE(invalid);
  ASSERT_TRUE(invalid.error().node_error);
  EXPECT_EQ(invalid.error().node_error->parameter_index, 1);
  EXPECT_EQ(invalid.error().where.begin.offset, rejected_extract.find("2)"));
  EXPECT_EQ(invalid.error().where.end.offset, rejected_extract.find("2)") + 1);
  constexpr std::string_view rejected_extension =
      "(set-logic QF_BV)(assert (= ((_ zero_extend 4294967296) #xff) #b1))";
  const auto oversized = Parse(rejected_extension);
  ExpectError(oversized, DiagnosticCode::kResource);
  ASSERT_FALSE(oversized);
  ASSERT_TRUE(oversized.error().node_error);
  EXPECT_EQ(oversized.error().node_error->resource, ResourceKind::kSize);
  const auto& where = oversized.error().where;
  EXPECT_EQ(rejected_extension.substr(where.begin.offset,
                                      where.end.offset - where.begin.offset),
            "((_ zero_extend 4294967296) #xff)");
}

TEST(FrontendPhaseOne, CanonicalizationRebuildsEachPayloadCarryingParent) {
  SCOPED_TRACE("FE-P1-NODE");
  std::shared_ptr<const QueryContext> query;
  const auto command = Require(Values(
      "true false #x01 #x02 RNE RTZ"
      " (keep (bvadd #x02 #x01))"
      " ((_ extract 4 1) (bvadd #x02 #x01))"
      " ((as const (Array Bool (_ BitVec 8))) (bvadd #x02 #x01))"
      " ((_ to_fp 5 11) (ite (or false true) RNE RTZ) (/ 1 3))",
      "(set-logic ALL)(declare-fun keep ((_ BitVec 8)) (_ BitVec 8))", &query));
  const auto& values = command.values;
  ASSERT_EQ(values.size(), 10);
  auto manager = Require(NodeManager::Attach(query->GetManager()));
  const auto child = [&](std::size_t i) { return values[i].term.Borrow(); };
  const auto sum = Builtin(manager, Kind::kBvAdd, {child(2), child(3)});
  const auto disjunction = Builtin(manager, Kind::kOr, {child(0), child(1)});
  const auto mode =
      Builtin(manager, Kind::kIte, {disjunction.Borrow(), child(4), child(5)});
  const auto array_sort =
      Require(manager.MakeArraySort(child(0).GetSort(), child(2).GetSort()));
  const auto third = Require(numeric::Rational::Make(numeric::IntegerView(1),
                                                     numeric::IntegerView(3)));
  const std::array sum_operand{sum.Borrow()};
  const std::array mode_operand{mode.Borrow()};
  const std::array fp_indices{numeric::Integer(5), numeric::Integer(11)};
  const std::array expected{
      ApplyDecl(manager, query->GetFunctions()[0].Borrow(), {sum.Borrow()}),
      Require(manager.MakeWordIndexedBuiltin(
          Kind::kBvExtract, std::array<std::uint64_t, 2>{4, 1}, sum_operand)),
      Require(manager.MakeBuiltin(
          {Kind::kArrayConst, {}, array_sort.Borrow(), {}}, sum_operand)),
      Require(manager.MakeBuiltin(
          {Kind::kFpFromRational, fp_indices, {}, third.Borrow()},
          mode_operand))};
  for (std::size_t i = 0; i < expected.size(); ++i) {
    const auto original = child(i + 6);
    const auto first = original.GetChildren()[0];
    EXPECT_EQ(Require(CanonicalizeCommutative(query->GetManager(), original)),
              expected[i]);
    EXPECT_NE(original, expected[i].Borrow());
    EXPECT_EQ(original.GetChildren()[0], first);
  }
}

TEST(FrontendPhaseOne, NameKeysSurviveCollisionsGrowthAndRollback) {
  SCOPED_TRACE("FE-P1-NAMES");
  const std::string name(180, 'x');
  std::string script =
      "(set-logic ALL)(declare-const |" + name +
      "| Int)"
      "(define-fun id ((formal Int)) Int formal)(define-const alias Int (id " +
      name + "))";
  constexpr int kConstants = 60;
  for (int i = 0; i < kConstants; ++i)
    script += "(declare-const c" + std::to_string(i) + " Bool)";
  script +=
      "(assert (= alias (! 1 :named pending) absent))"
      "(assert (= alias (! 1 :named pending) (let ((alias 2)) alias)))"
      "(assert (= alias (id |" +
      name + "|)))(check-sat)";
  auto session = Open(script, true);
  session.ForceNameHashCollisions();
  for (int i = 0; i < kConstants + 4; ++i) Require(Next(session));
  const auto stats = session.GetStats();
  EXPECT_EQ(stats.borrowed_term_names, kConstants + 2);
  EXPECT_EQ(stats.pooled_term_names, 1);
  ExpectError(Next(session), DiagnosticCode::kInvalidInput);
  EXPECT_EQ(session.GetStats().borrowed_term_names, kConstants + 2);
  EXPECT_EQ(session.GetStats().pooled_term_names, 1);
  RequireNext(session, CommandKind::kAssert);
  RequireNext(session, CommandKind::kAssert);
  const auto query = RequireCheck(session);
  EXPECT_EQ(Applied(Argument(query->GetAssertions()[0], 0)),
            query->GetDefinition(0).decl.Borrow());
  EXPECT_EQ(IntegerValue(Argument(query->GetAssertions()[0], 2)), "2");
  EXPECT_EQ(Argument(query->GetAssertions()[1], 0),
            Argument(query->GetAssertions()[1], 1));
  EXPECT_EQ(Applied(Argument(query->GetAssertions()[1], 0).GetChildren()[0]),
            query->GetFunctions()[0].Borrow());
  EXPECT_EQ(session.GetSourceRecords()->named_terms.size(), 1);
}

}  // namespace
}  // namespace dandelion::frontend::testing

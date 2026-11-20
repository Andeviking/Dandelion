#include "test_support.h"

#include "../node/node_test_peer.h"

namespace dandelion::frontend::testing {
namespace {

TEST(FrontendBindings, SimultaneousLetAndLexicalRestoration) {
  const auto query = RequireQuery(
      Parse("(set-logic ALL)(declare-const x Int)"
            "(assert (let ((x 1)) (and (let ((x 2) (y x)) (= x y)) (= x 1))))"
            "(assert (= x 3))(check-sat)"));
  const auto conjunction = query->GetAssertions()[0].Borrow();
  ASSERT_EQ(conjunction.GetKind(), Kind::kAnd);
  const auto inner = conjunction.GetChildren()[0].GetChildren();
  EXPECT_EQ(IntegerValue(inner[0]), "2");
  EXPECT_EQ(IntegerValue(inner[1]), "1");
  const auto outside = conjunction.GetChildren()[1].GetChildren();
  EXPECT_EQ(outside[0], inner[1]);
  EXPECT_EQ(outside[0], outside[1]);
  EXPECT_EQ(Applied(Argument(query->GetAssertions()[1], 0)),
            query->GetFunctions()[0].Borrow());
  for (auto input : {"(let ((x 1) (y x)) y)", "(let ((x 1) (|x| 2)) x)",
                     "(let ((true false)) true)", "(let ((x 1)) (as x Int))",
                     "(let ((f false)) (f 1))", "|@hidden|", ".hidden",
                     "(late (! 1 :named late))"}) {
    SCOPED_TRACE(input);
    ExpectError(Values(input), DiagnosticCode::kInvalidInput);
  }
  const auto shadow =
      Require(Values("(let ((f 0)) f) (f false)",
                     "(set-logic ALL)(declare-fun f (Bool) Bool)"));
  EXPECT_EQ(IntegerValue(shadow.values[0].term.Borrow()), "0");
  EXPECT_EQ(Applied(shadow.values[1].term.Borrow()).GetNameHint(), "f");
}

TEST(FrontendBindings,
     DefinitionIdentityRetainsOriginalBodyAndOrderedArguments) {
  std::shared_ptr<const QueryContext> query;
  {
    std::string input =
        "(set-logic QF_UFLIA)(declare-const g Int)"
        "(define-fun f ((x Int) (unused Bool)) Int (+ g x x))"
        "(define-fun h ((x Int)) Int (f x true))"
        "(define-const n Int (h 3))"
        "(assert (let ((g 5)) (= (h g) n)))(check-sat)";
    query = RequireQuery(Parse(input));
  }
  ASSERT_EQ(query->GetDefinitionCount(), 2);
  ASSERT_EQ(query->GetFunctions().size(), 1);
  const auto& f = query->GetDefinition(0);
  const auto& h = query->GetDefinition(1);
  ASSERT_EQ(f.formals.size(), 2);
  ASSERT_EQ(h.formals.size(), 1);
  EXPECT_NE(f.formals[0], h.formals[0]);
  EXPECT_EQ(f.formals[0].Borrow().GetKind(), DeclKind::kFormal);
  EXPECT_EQ(f.decl.Borrow().GetKind(), DeclKind::kInternalFunction);
  EXPECT_EQ(f.body.Borrow().GetKind(), Kind::kIntAdd);
  EXPECT_EQ(Applied(Argument(f.body, 0)), query->GetFunctions()[0].Borrow());
  EXPECT_EQ(Applied(Argument(f.body, 1)), f.formals[0].Borrow());
  EXPECT_EQ(Argument(f.body, 1), Argument(f.body, 2));
  EXPECT_EQ(Applied(h.body.Borrow()), f.decl.Borrow());
  EXPECT_EQ(Applied(Argument(h.body, 0)), h.formals[0].Borrow());
  EXPECT_EQ(Argument(h.body, 1).GetValue()->GetBool(), true);
  const auto root = query->GetAssertions()[0].Borrow();
  ASSERT_EQ(root.GetChildren().size(), 2);
  const auto actual = root.GetChildren()[0];
  EXPECT_EQ(query->FindDefinition(Applied(actual)), &h);
  EXPECT_EQ(IntegerValue(actual.GetChildren()[0]), "5");
  EXPECT_EQ(Applied(root.GetChildren()[1]), h.decl.Borrow());
  EXPECT_EQ(IntegerValue(root.GetChildren()[1].GetChildren()[0]), "3");
  auto other = RequireQuery(
      Parse("(set-logic ALL)(define-fun f ((x Int)) Int x)(check-sat)"));
  EXPECT_EQ(query->FindDefinition(other->GetDefinition(0).decl.Borrow()),
            nullptr);

  for (auto input :
       {"(define-const f Int f)", "(define-fun f ((x Int)) Int (f x))",
        "(define-fun f ((x Int) (|x| Bool)) Int x)",
        "(define-fun f ((int Int)) Int false)",
        "(define-const f Bool (! false :named f))",
        "(define-fun f ((x Int) (unused Bool)) Int x)(assert (= (f 0 1) 0))",
        "(define-fun f ((x Int)) Int x)(assert (= f 0))",
        "(define-fun f ((x Int)) Int x)(assert (= (f 0 1) 0))"}) {
    SCOPED_TRACE(input);
    ExpectError(Parse("(set-logic ALL)" + std::string(input)),
                DiagnosticCode::kInvalidInput);
  }
  const auto shadow = Require(
      Values("(g true)",
             "(set-logic ALL)"
             "(declare-fun h (Bool) Bool)(define-fun g ((h Bool)) Bool h)"));
  EXPECT_EQ(Applied(shadow.values[0].term.Borrow()).GetKind(),
            DeclKind::kInternalFunction);
}

TEST(FrontendBindings, WrittenDefinitionGraphDoesNotExpandCalls) {
  for (bool int_profile : {false, true}) {
    for (bool used : {false, true}) {
      const std::string sort = int_profile ? "Int" : "(_ BitVec 8)";
      std::string input =
          int_profile ? "(set-logic QF_LIA)" : "(set-logic QF_BV)";
      input += "(declare-const a " + sort + ")(define-fun f0 ((x " + sort +
               ")) " + sort + (int_profile ? " x)" : " (bvnot x))");
      const int depth = int_profile ? 12 : 48;
      for (int i = 1; i <= depth; ++i) {
        const auto name = "f" + std::to_string(i);
        const auto prior = "f" + std::to_string(i - 1);
        input += "(define-fun " + name + " ((x " + sort + ")) " + sort + " (" +
                 prior + " (" + prior + " x)))";
      }
      if (used) input += "(assert (= (f" + std::to_string(depth) + " a) a))";
      input += "(check-sat)";
      auto session = Require(Session::FromText(input));
      std::shared_ptr<const QueryContext> query;
      while (auto command = Require(Next(session)))
        if (command->query) query = command->query;
      ASSERT_TRUE(query);
      EXPECT_EQ(query->GetDefinitionCount(),
                static_cast<std::size_t>(depth + 1));
      const auto stats = session.GetStats();
      EXPECT_EQ(stats.definition_bodies, static_cast<std::size_t>(depth + 1));
      EXPECT_EQ(stats.defined_calls,
                static_cast<std::size_t>(2 * depth + used));
      const auto& last = query->GetDefinition(query->GetDefinitionCount() - 1);
      EXPECT_EQ(Applied(last.body.Borrow()).GetNameHint(),
                "f" + std::to_string(depth - 1));
      EXPECT_EQ(Applied(Argument(last.body, 0)), Applied(last.body.Borrow()));
    }
  }

  const auto q =
      RequireQuery(Parse("(set-logic QF_LIA)(declare-const x Int)"
                         "(define-fun f ((t Int)) Int (* 2 t))"
                         "(define-fun keep ((t Int) (discard Int)) Int (f t))"
                         "(assert (= (f (+ x 1)) 4))(assert (= (f x) 2))"
                         "(assert (= (keep x (* x x)) 2))(check-sat)"));
  const auto left = Argument(q->GetAssertions()[0], 0);
  const auto right = Argument(q->GetAssertions()[1], 0);
  EXPECT_EQ(Applied(left), Applied(right));
  EXPECT_EQ(left.GetChildren()[0].GetKind(), Kind::kIntAdd);
  EXPECT_EQ(Applied(right.GetChildren()[0]), q->GetFunctions()[0].Borrow());
  EXPECT_EQ(Argument(q->GetAssertions()[2], 0).GetChildren()[1].GetKind(),
            Kind::kIntMul);
}

TEST(FrontendNames, PostorderPublicationClosureAndMetadata) {
  SourceRecords records;
  const auto query = RequireQuery(Parse(
      "(set-logic ALL)(declare-const a Int)"
      "(assert (= (! 1 :source (x \"\" :keyword) :informative :named one) one))"
      "(assert (= (let ((x a)) (! (let ((unused x)) 0) :named zero)) zero))"
      "(assert (= (let ((x 2)) (! x :tag within)) 2))"
      "(assert (= (let ((one 3)) one) 3))"
      "(define-const f Int (! 5 :named five))(assert (= (let ((u f)) u) five))"
      "(assert (= (! (let ((a 2)) (+ a a)) :named four) four))"
      "(check-sat)",
      &records));
  ASSERT_EQ(records.named_terms.size(), 4);
  EXPECT_EQ(records.named_terms[0].name, "one");
  EXPECT_EQ(IntegerValue(records.named_terms[1].term.Borrow()), "0");
  EXPECT_EQ(records.named_terms[2].name, "five");
  EXPECT_EQ(records.named_terms[3].term.Borrow().GetKind(), Kind::kIntAdd);
  EXPECT_EQ(Argument(query->GetAssertions()[0], 0),
            Argument(query->GetAssertions()[0], 1));
  const auto& metadata = records.annotations[0].attributes;
  ASSERT_EQ(metadata.size(), 3);
  EXPECT_EQ(metadata[0].value, "(x \"\" :keyword)");
  EXPECT_FALSE(metadata[1].value);
  EXPECT_EQ(metadata[2].key, ":named");
  EXPECT_EQ(metadata[2].value, "one");

  for (auto input :
       {"(assert (= late (! 1 :named late)))",
        "(assert (= (let ((x 1)) (! x :named n)) 1))",
        "(assert (= (let ((x 1)) (! (let ((y x)) y) :named n)) 1))",
        "(define-fun ignore ((arg Int)) Int 0)"
        "(define-fun err ((x Int)) Int (! (ignore x) :named n))",
        "(define-fun err ((x Int)) Int (! x :named n))",
        "(assert (= (! 1 :named x :named y) 1))",
        "(assert (= (! 1 :named x) (! 2 :named |x|)))",
        "(assert (= (! 1 :named) 1))", "(assert (= (! 1 :named 1) 1))",
        "(assert (= (! 1 :named true) 1))",
        "(declare-const x Int)(assert (= (! 1 :named |x|) 1))",
        "(declare-datatype D ((c (field Int))))"
        "(assert (= (match (c 1) (((c v) (! v :named n)))) 1))"}) {
    SCOPED_TRACE(input);
    ExpectError(Parse("(set-logic ALL)" + std::string(input)),
                DiagnosticCode::kInvalidInput);
  }
  ExpectError(Parse("(set-logic ALL)(assert (! true :pattern (true)))"),
              DiagnosticCode::kUnsupported);
  ExpectError(Values("(! 1 :named n)"), DiagnosticCode::kUnsupported);
  const auto closed = RequireQuery(
      Parse("(set-logic ALL)(declare-datatype D ((c (val Int))(d)))"
            "(assert (= (! (match (c 1) (((c v) v) (_ 0))) :named n) n))"
            "(check-sat)"));
  EXPECT_EQ(Argument(closed->GetAssertions()[0], 0).GetKind(), Kind::kIte);
}

TEST(FrontendNames, ClosureReusesCheckedOccurrencesAndAliasesRollBack) {
  constexpr int kDepth = 200;
  std::string input = "(set-logic ALL)(assert (= ";
  for (int i = 0; i < kDepth; ++i) input += "(! ";
  input += "1";
  for (int i = 0; i < kDepth; ++i)
    input += " :named n" + std::to_string(i) + ")";
  input += " n0))(check-sat)";
  auto session = Require(
      Session::FromText(input, ParseOptions{.capture_source_records = true}));
  Require(Next(session));
  Require(Next(session));
  EXPECT_EQ(session.GetStats().closure_visits,
            static_cast<std::size_t>(kDepth));
  RequireCheck(session);
  EXPECT_EQ(session.GetSourceRecords()->named_terms.size(),
            static_cast<std::size_t>(kDepth));

  auto retry = Require(Session::FromText(
      "(set-logic ALL)(assert (! true :named good))"
      "(assert (and (! true :named first) (! false :named second) 9))"
      "(assert (and good (! true :named first) (! false :named second)))"
      "(check-sat)",
      ParseOptions{.capture_source_records = true}));
  Require(Next(retry));
  Require(Next(retry));
  ExpectError(Next(retry), DiagnosticCode::kInvalidInput);
  ASSERT_EQ(retry.GetSourceRecords()->named_terms.size(), 1);
  ASSERT_EQ(retry.GetSourceRecords()->annotations.size(), 1);
  ASSERT_EQ(retry.GetPrefix().GetAssertions().size(), 1);
  Require(Next(retry));
  RequireCheck(retry);
  EXPECT_EQ(retry.GetSourceRecords()->named_terms.size(), 3);

  std::size_t failures = 0;
  for (std::size_t checkpoint = 0; checkpoint < 64; ++checkpoint) {
    auto allocation = Require(Session::FromText(
        "(set-logic ALL)(assert (and (! true :named a) (! false :named b)))"
        "(assert (and (! true :named a) (! false :named b)))(check-sat)",
        ParseOptions{.capture_source_records = true}));
    Require(Next(allocation));
    allocation.FailAllocationAfter(checkpoint);
    const auto attempt = Next(allocation);
    if (attempt) break;
    ++failures;
    ExpectError(attempt, DiagnosticCode::kResource);
    EXPECT_TRUE(allocation.GetSourceRecords()->named_terms.empty());
    EXPECT_TRUE(allocation.GetSourceRecords()->annotations.empty());
    EXPECT_TRUE(allocation.GetPrefix().GetAssertions().empty());
    Require(Next(allocation));
    RequireCheck(allocation);
    EXPECT_EQ(allocation.GetSourceRecords()->named_terms.size(), 2);
  }
  EXPECT_GE(failures, 3);
}

TEST(FrontendScale, DeepTermsWideDistinctAndLargeExactNumeral) {
  constexpr std::size_t kDepth = 16000;
  std::string script = "(set-logic ALL)(assert ";
  for (std::size_t i = 0; i < kDepth; ++i) script += "(not ";
  script += "false" + std::string(kDepth + 1, ')');
  script += "(assert (distinct";
  for (std::size_t i = 0; i < 4096; ++i) script += " " + std::to_string(i);
  script += "))(assert (= 1" + std::string(4096, '0') + " 0))(check-sat)";
  auto query = RequireQuery(Parse(script));
  auto node = query->GetAssertions()[0].Borrow();
  for (std::size_t i = 0; i < kDepth; ++i) {
    ASSERT_EQ(node.GetKind(), Kind::kNot);
    node = node.GetChildren()[0];
  }
  EXPECT_EQ(node.GetValue()->GetBool(), false);
  const auto wide = query->GetAssertions()[1].Borrow();
  EXPECT_EQ(wide.GetKind(), Kind::kDistinct);
  ASSERT_EQ(wide.GetChildren().size(), 4096);
  EXPECT_EQ(IntegerValue(wide.GetChildren()[4095]), "4095");
  EXPECT_EQ(IntegerValue(Argument(query->GetAssertions()[2], 0)),
            "1" + std::string(4096, '0'));
  query.reset();
  auto malformed = script;
  malformed.pop_back();
  ExpectError(Parse(malformed), DiagnosticCode::kSyntax);
}

TEST(FrontendNames, RetainFailuresPreservePublishedPrefixAndAllowRetry) {
  std::size_t failures = 0;
  for (std::size_t checkpoint = 0; checkpoint < 64; ++checkpoint) {
    auto session = Require(Session::FromText(
        "(set-logic ALL)(assert (! true :named good))"
        "(assert (and (! good :named a) (! good :named b)))"
        "(assert (and (! good :named a) (! good :named b)))(check-sat)",
        ParseOptions{.capture_source_records = true}));
    Require(Next(session));
    Require(Next(session));
    auto manager =
        Require(NodeManager::Attach(session.GetPrefix().GetManager()));
    NodeTestPeer::FailRetainAfter(manager, checkpoint);
    const auto attempt = Next(session);
    NodeTestPeer::ClearFailures(manager);
    if (attempt) break;
    ++failures;
    ExpectError(attempt, DiagnosticCode::kResource);
    ASSERT_TRUE(attempt.error().node_error);
    EXPECT_EQ(attempt.error().node_error->resource,
              ResourceKind::kReferenceCount);
    EXPECT_EQ(session.GetSourceRecords()->named_terms.size(), 1);
    EXPECT_EQ(session.GetSourceRecords()->annotations.size(), 1);
    EXPECT_EQ(session.GetPrefix().GetAssertions().size(), 1);
    Require(Next(session));
    RequireCheck(session);
    EXPECT_EQ(session.GetSourceRecords()->named_terms.size(), 3);
    EXPECT_EQ(session.GetSourceRecords()->named_terms[2].term,
              session.GetSourceRecords()->named_terms[0].term);
  }
  EXPECT_GE(failures, 4);
}

TEST(FrontendNames, LetRhsNamesAndClosureFollowSourceOrder) {
  constexpr std::string_view input =
      "(assert (let ((x (! 1 :named n)) (y n)) (= x y)))";
  SCOPED_TRACE(::testing::Message() << "FE-LET-NAME-02 " << input);
  SourceRecords records;
  const auto query = RequireQuery(
      Parse("(set-logic ALL)" + std::string(input) + "(check-sat)", &records));
  ASSERT_EQ(records.named_terms.size(), 1);
  ASSERT_EQ(records.annotations.size(), 1);
  ASSERT_EQ(query->GetAssertions().size(), 1);
  const auto& name = records.named_terms[0];
  EXPECT_EQ(name.name, "n");
  EXPECT_EQ(IntegerValue(name.term.Borrow()), "1");
  ExpectChildren(query->GetAssertions()[0].Borrow(),
                 {name.term.Borrow(), name.term.Borrow()});
  ExpectError(Parse("(set-logic ALL)"
                    "(assert (let ((y n) (x (! 1 :named n))) (= x y)))"),
              DiagnosticCode::kInvalidInput);

  constexpr std::string_view closed =
      "(assert (= (let ((x 1)) (! 1 :named closed)) 1))";
  constexpr std::string_view open =
      "(assert (= (let ((x 1)) (! x :named open)) 1))";
  for (const bool closed_first : {true, false}) {
    std::string script = "(set-logic ALL)(assert (= 1 1))";
    script += closed_first ? closed : open;
    script += closed_first ? open : closed;
    script += "(assert (= open 1))(assert (= closed 1))(check-sat)";
    SCOPED_TRACE(script);
    auto session = Require(Session::FromText(
        script, ParseOptions{.capture_source_records = true}));
    RequireNext(session, CommandKind::kSetLogic);
    RequireNext(session, CommandKind::kAssert);
    ASSERT_EQ(session.GetPrefix().GetAssertions().size(), 1);
    const auto shared = Argument(session.GetPrefix().GetAssertions()[0], 0);
    if (closed_first) RequireNext(session, CommandKind::kAssert);
    ExpectError(Next(session), DiagnosticCode::kInvalidInput);
    EXPECT_EQ(session.GetSourceRecords()->named_terms.size(),
              closed_first ? 1u : 0u);
    EXPECT_EQ(session.GetSourceRecords()->annotations.size(),
              closed_first ? 1u : 0u);
    if (!closed_first) RequireNext(session, CommandKind::kAssert);
    ExpectError(Next(session), DiagnosticCode::kInvalidInput);
    RequireNext(session, CommandKind::kAssert);
    const auto sealed = RequireCheck(session);
    ASSERT_EQ(session.GetSourceRecords()->named_terms.size(), 1);
    EXPECT_EQ(session.GetSourceRecords()->named_terms[0].name, "closed");
    EXPECT_EQ(session.GetSourceRecords()->named_terms[0].term.Borrow(), shared);
    ASSERT_EQ(session.GetSourceRecords()->annotations.size(), 1);
    EXPECT_EQ(session.GetSourceRecords()->annotations[0].term.Borrow(), shared);
    EXPECT_EQ(sealed->GetAssertions().size(), 3);
  }
}

TEST(FrontendBindings, DefinitionCallsKeepActualsInEitherAssertionOrder) {
  constexpr std::array<std::string_view, 2> assertions{
      "(assert (= (f x) 2))", "(assert (= (f (+ x 1)) 4))"};
  for (bool reversed : {false, true}) {
    std::string script =
        "(set-logic QF_LIA)(declare-const x Int)"
        "(define-fun f ((t Int)) Int (* 2 t))";
    script += assertions[reversed];
    script += assertions[!reversed];
    script += "(check-sat)";
    SCOPED_TRACE(::testing::Message() << "FE-DEF-ORDER-02 " << script);
    const auto query = RequireQuery(Parse(script));
    ASSERT_EQ(query->GetAssertions().size(), 2);
    ASSERT_EQ(query->GetFunctions().size(), 1);
    ASSERT_EQ(query->GetDefinitionCount(), 1);
    const auto& definition = query->GetDefinition(0);
    const auto body = definition.body.Borrow();
    ASSERT_EQ(body.GetKind(), Kind::kIntMul);
    ASSERT_EQ(body.GetChildren().size(), 2);
    ASSERT_EQ(definition.formals.size(), 1);
    const auto coefficient = IntegerValue(body.GetChildren()[0]);
    EXPECT_EQ(coefficient, "2");
    EXPECT_EQ(Applied(body.GetChildren()[1]), definition.formals[0].Borrow());
    const auto x = query->GetFunctions()[0].Borrow();
    for (std::size_t index = 0; index < 2; ++index) {
      const bool incremented = (index == 1) != reversed;
      const auto root = query->GetAssertions()[index].Borrow();
      ASSERT_EQ(root.GetKind(), Kind::kEqual);
      ASSERT_EQ(root.GetChildren().size(), 2);
      const auto call = root.GetChildren()[0];
      ASSERT_EQ(call.GetKind(), Kind::kApply);
      EXPECT_EQ(Applied(call), definition.decl.Borrow());
      EXPECT_EQ(query->FindDefinition(Applied(call)), &definition);
      ASSERT_EQ(call.GetChildren().size(), 1);
      const auto actual = call.GetChildren()[0];
      constexpr int kXValue = 1;
      int actual_value = kXValue;
      if (incremented) {
        ASSERT_EQ(actual.GetKind(), Kind::kIntAdd);
        ASSERT_EQ(actual.GetChildren().size(), 2);
        EXPECT_EQ(Applied(actual.GetChildren()[0]), x);
        const auto increment = IntegerValue(actual.GetChildren()[1]);
        EXPECT_EQ(increment, "1");
        actual_value += std::stoi(increment);
      } else {
        EXPECT_EQ(Applied(actual), x);
      }
      const auto result = IntegerValue(root.GetChildren()[1]);
      EXPECT_EQ(result, incremented ? "4" : "2");
      EXPECT_EQ(std::stoi(coefficient) * actual_value, std::stoi(result));
    }
  }
}

TEST(FrontendNames, LaterInvalidAnnotationRollsBackEarlierStagedName) {
  constexpr std::string_view script =
      "(set-logic ALL)(assert (! true :named prior))"
      "(assert (and (! prior :named staged)"
      " (let ((x true)) (! (! x :named inside) :tag outer))))"
      "(assert staged)(assert inside)"
      "(assert (and (! prior :named staged)"
      " (let ((x true)) (! (! true :named inside) :tag outer))))"
      "(assert (and staged inside))(check-sat)";
  SCOPED_TRACE(::testing::Message() << "FE-ROLLBACK-NAME-02 " << script);
  auto session = Require(
      Session::FromText(script, ParseOptions{.capture_source_records = true}));
  RequireNext(session, CommandKind::kSetLogic);
  RequireNext(session, CommandKind::kAssert);
  const auto prior = session.GetPrefix().GetAssertions()[0];
  for (int i = 0; i < 3; ++i) {
    ExpectError(Next(session), DiagnosticCode::kInvalidInput);
    const auto& prefix = session.GetPrefix();
    ASSERT_EQ(prefix.GetAssertions().size(), 1);
    EXPECT_EQ(prefix.GetAssertions()[0], prior);
    ASSERT_EQ(session.GetSourceRecords()->named_terms.size(), 1);
    EXPECT_EQ(session.GetSourceRecords()->named_terms[0].name, "prior");
    ASSERT_EQ(session.GetSourceRecords()->annotations.size(), 1);
    EXPECT_EQ(session.GetSourceRecords()->annotations[0].term, prior);
  }
  RequireNext(session, CommandKind::kAssert);
  RequireNext(session, CommandKind::kAssert);
  const auto query = RequireCheck(session);
  const auto names = session.GetSourceRecords()->named_terms;
  ASSERT_EQ(names.size(), 3);
  EXPECT_EQ(names[0].name, "prior");
  EXPECT_EQ(names[1].name, "staged");
  EXPECT_EQ(names[2].name, "inside");
  const auto annotations = session.GetSourceRecords()->annotations;
  ASSERT_EQ(annotations.size(), 4);
  ASSERT_EQ(annotations[1].attributes.size(), 1);
  ASSERT_EQ(annotations[2].attributes.size(), 1);
  ASSERT_EQ(annotations[3].attributes.size(), 1);
  EXPECT_EQ(annotations[1].attributes[0].value, "staged");
  EXPECT_EQ(annotations[2].attributes[0].value, "inside");
  EXPECT_EQ(annotations[3].attributes[0].value, "outer");
  ASSERT_EQ(query->GetAssertions().size(), 3);
  EXPECT_EQ(query->GetAssertions()[0], prior);
  ExpectChildren(query->GetAssertions()[2].Borrow(),
                 {names[1].term.Borrow(), names[2].term.Borrow()});
}

}  // namespace
}  // namespace dandelion::frontend::testing

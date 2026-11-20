#include "model_support.h"

namespace dandelion::frontend::testing {
namespace {

Reader Chunks(std::string_view text, std::size_t split = 0,
              std::size_t* calls = nullptr) {
  return [=, cursor = std::size_t{},
          prior = std::size_t{}](std::span<char> out) mutable -> ReadResult {
    if (calls) ++*calls;
    std::ranges::fill(out.first(prior), '\x7f');
    const auto count = std::min(
        {out.size(), text.size() - cursor,
         split ? (cursor < split ? split - cursor : text.size() - cursor)
               : std::size_t{1}});
    std::ranges::copy(text.substr(cursor, count), out.begin());
    cursor += count;
    prior = count;
    return count;
  };
}

CommandResponse Tag(const TypedCommand& command) {
  return std::get<CommandResponse>(command.response);
}

TEST(FrontendPhaseTwo, Split) {
  SCOPED_TRACE("FE-P2-SPLIT");
  constexpr auto input = std::string_view(
      "; é\r\n(echo \"a\"\"🌱\")\r(set-logic ALL)\n"
      "(declare-const |x y| (_ BitVec 4))"
      "(set-info :extra (1.50 #xAB))"
      "(assert (= |x y| ((_ extract 7 4) #xAB)))(check-sat)");
  for (const auto capture : {false, true}) {
    for (std::size_t split = 0; split <= input.size() + 1; ++split) {
      SCOPED_TRACE(split);
      std::size_t reads = 0;
      const ParseOptions options{.capture_source_records = capture};
      auto session = Require(
          split > input.size()
              ? Session::FromText(input, options, 19)
              : Session::FromReader(Chunks(input, split, &reads), options, 19));
      const auto echo = RequireNext(session, CommandKind::kEcho);
      EXPECT_EQ(echo.header.where.begin, (SourcePosition{6, 2, 1}));
      EXPECT_EQ(echo.header.where.source, 19);
      EXPECT_EQ(std::get<std::string>(echo.response), "\"a\"\"🌱\"");
      RequireNext(session, CommandKind::kSetLogic);
      RequireNext(session, CommandKind::kDeclareConst);
      RequireNext(session, CommandKind::kSetInfo);
      const auto assertion = RequireNext(session, CommandKind::kAssert);
      EXPECT_EQ(assertion.header.where.begin.offset, input.find("(assert"));
      EXPECT_EQ(assertion.header.where.end.offset, input.find("(check-sat"));
      const auto query = RequireCheck(session);
      const auto children =
          query->GetAssertions().front().Borrow().GetChildren();
      EXPECT_EQ(Applied(children[0]), query->GetFunctions().front().Borrow());
      EXPECT_EQ(children[1].GetSort(), children[0].GetSort());
      EXPECT_EQ(children[1].GetKind(), Kind::kBvExtract);
      const auto literal = children[1].GetChildren()[0];
      ASSERT_TRUE(literal.GetValue()->GetBitVector());
      EXPECT_EQ(BvValue(literal), "171");
      EXPECT_EQ(children[1].GetChildren()[0].GetSort().GetWidth(), 8);
      EXPECT_EQ(children[1].GetSort().GetWidth(), 4);
      if (capture) {
        EXPECT_EQ(session.GetSourceRecords()->metadata.front().value,
                  "(1.50 #xAB)");
        EXPECT_EQ(
            session.GetSourceRecords()->assertions.front().where.begin.offset,
            assertion.header.where.begin.offset + 8);
      }
      const auto before_eof = reads;
      EXPECT_FALSE(Require(session.ReadCommand()));
      EXPECT_EQ(reads, before_eof + (split <= input.size() ? 1 : 0));
    }
  }
}

TEST(FrontendPhaseTwo, Coordinates) {
  SCOPED_TRACE("FE-P2-COORD");
  const std::string prefix = "(set-logic ALL)\r\n";
  for (const auto& [term, spelling, col] :
       {std::tuple{"(bvadd #x01 #b1\r\n; é\r\n)", "#b1", 21},
        {"((_ to_fp 5 1)\r\n; é\r\n RNE #x00)", "1", 21},
        {"(bvsub #x01)", "(bvsub #x01)", 9}}) {
    const auto text = prefix + "(assert " + term + ")";
    auto session = Require(Session::FromReader(Chunks(text), 17));
    RequireNext(session, CommandKind::kSetLogic);
    const auto failure = session.ReadCommand();
    ASSERT_FALSE(failure);
    ASSERT_TRUE(failure.error().node_error);
    const auto& detail = *failure.error().node_error;
    if (std::string_view(spelling) == "#b1") {
      EXPECT_EQ(detail.operand_index, 1);
    }
    if (std::string_view(spelling) == "1") {
      EXPECT_EQ(detail.parameter_index, 1);
    }
    const auto offset =
        text.find(spelling, prefix.size() + static_cast<std::size_t>(col - 1));
    const auto size = std::string_view(spelling).size();
    EXPECT_EQ(failure.error().where,
              (SourceSpan{
                  17,
                  {offset, 2, static_cast<std::uint64_t>(col)},
                  {offset + size, 2, static_cast<std::uint64_t>(col) + size}}));
  }
  for (const std::string_view bad :
       {"\xe2\x82", "\xff", "\xc0\x80", "\xed\xa0\x80", "\xe2\x41\x82"}) {
    const auto text =
        std::string("(echo \"é\")\r\n(echo \"") + std::string(bad);
    auto reader = Require(SyntaxValidator::FromReader(Chunks(text), 21));
    ASSERT_TRUE(Require(reader.ReadNext()));
    const auto failure = reader.ReadNext();
    ExpectError(failure, DiagnosticCode::kSyntax);
    ASSERT_FALSE(failure);
    EXPECT_EQ(failure.error().where, (SourceSpan{21, {20, 2, 8}, {20, 2, 8}}));
    EXPECT_FALSE(Require(reader.ReadNext()));
  }
}

TEST(FrontendPhaseTwo, Depth) {
  SCOPED_TRACE("FE-P2-DEPTH");
  constexpr std::size_t kDepth = 16384;
  std::string nested;
  for (std::size_t i = 0; i < kDepth; ++i) nested += "(not ";
  const auto suffix = std::string(kDepth, ')');
  const std::string prefix = "(set-logic ALL)(declare-const a Bool)";
  const auto good =
      prefix + "(assert " + nested + "a" + suffix + ")(check-sat)";
  const auto query = RequireQuery(Parse(good));
  auto node = query->GetAssertions().front().Borrow();
  for (std::size_t i = 0; i < kDepth; ++i) {
    ASSERT_EQ(node.GetKind(), Kind::kNot);
    ASSERT_EQ(node.GetChildren().size(), 1);
    node = node.GetChildren()[0];
  }
  EXPECT_EQ(Applied(node), query->GetFunctions().front().Borrow());
  const auto truncated = prefix + "(assert " + nested + "missing" + suffix;
  auto session = Require(Session::FromText(truncated));
  RequireNext(session, CommandKind::kSetLogic);
  RequireNext(session, CommandKind::kDeclareConst);
  ExpectError(session.ReadCommand(), DiagnosticCode::kSyntax);
  EXPECT_TRUE(session.GetPrefix().GetAssertions().empty());
  EXPECT_FALSE(Require(session.ReadCommand()));
  for (bool capture : {false, true}) {
    for (bool closed : {false, true}) {
      const auto text = "(set-info :opaque " + std::string(kDepth, '(') +
                        "leaf" + suffix + (closed ? ")" : "");
      auto metadata = Require(Session::FromText(
          text, ParseOptions{.capture_source_records = capture}));
      if (closed) {
        RequireNext(metadata, CommandKind::kSetInfo);
        if (capture) {
          EXPECT_EQ(metadata.GetSourceRecords()->metadata.size(), 1);
        }
      } else {
        ExpectError(metadata.ReadCommand(), DiagnosticCode::kSyntax);
        if (capture) {
          EXPECT_TRUE(metadata.GetSourceRecords()->metadata.empty());
        }
      }
      EXPECT_LE(metadata.GetStats().peak_retained_fragments, 4);
    }
  }
  const auto wide = [&] {
    std::string text = "(set-info :opaque (";
    for (std::size_t i = 0; i < kDepth; ++i) text += "sibling ";
    return text + "))";
  }();
  auto opaque = Require(Session::FromReader(Chunks(wide)));
  RequireNext(opaque, CommandKind::kSetInfo);
  EXPECT_EQ(opaque.GetSourceRecords(), nullptr);
  EXPECT_LE(opaque.GetStats().peak_retained_fragments, 4);
}

TEST(FrontendPhaseTwo, Width) {
  SCOPED_TRACE("FE-P2-WIDTH");
  const std::string prefix =
      "(set-logic ALL)(declare-const x Bool)(declare-const y Bool)";
  auto text = prefix + "(assert (and";
  for (std::size_t i = 0; i < 8192; ++i) text += i % 3 == 0 ? " x" : " y";
  text += "))(assert (let ((x y)(first (! y :named n))(later (and x n))";
  for (std::size_t i = 0; i < 4093; ++i)
    text += "(b" + std::to_string(i) + " true)";
  text += ") (and later x)))(check-sat)";
  for (bool canonical : {false, true}) {
    auto session = Require(Session::FromText(
        text, ParseOptions{.canonicalize_commutative = canonical}));
    for (const auto kind : {CommandKind::kSetLogic, CommandKind::kDeclareConst,
                            CommandKind::kDeclareConst, CommandKind::kAssert})
      RequireNext(session, kind);
    EXPECT_EQ(session.GetStats().staging_overlays, 0);
    EXPECT_EQ(session.GetStats().peak_live_operands, 8192);
    EXPECT_EQ(session.GetStats().escape_operands, 0);
    RequireNext(session, CommandKind::kAssert);
    EXPECT_EQ(session.GetStats().staging_overlays, 1);
    EXPECT_GT(session.GetStats().escape_operands, 0);
    const auto query = RequireCheck(session);
    const auto children = query->GetAssertions()[0].Borrow().GetChildren();
    ASSERT_EQ(children.size(), 8192);
    const auto functions = query->GetFunctions();
    std::size_t x_count = 0;
    for (std::size_t i = 0; i < children.size(); ++i) {
      const auto decl = Applied(children[i]);
      if (decl == functions[0].Borrow()) ++x_count;
      if (!canonical) {
        EXPECT_EQ(decl, functions[i % 3 == 0 ? 0 : 1].Borrow());
      }
    }
    EXPECT_EQ(x_count, 2731);
    if (!canonical) {
      const auto body = query->GetAssertions()[1].Borrow().GetChildren();
      EXPECT_EQ(Applied(body[1]), functions[1].Borrow());
      EXPECT_EQ(Applied(body[0].GetChildren()[0]), functions[0].Borrow());
      EXPECT_EQ(Applied(body[0].GetChildren()[1]), functions[1].Borrow());
    }
  }
  for (const auto body : {"(let ((a n)(b (! true :named n))) a)",
                          "(let ((unused missing)) true)"}) {
    const auto bad = prefix + "(assert " + body + ")";
    ExpectError(Parse(bad), DiagnosticCode::kInvalidInput);
  }
}

TEST(FrontendPhaseTwo, FlatReduction) {
  SCOPED_TRACE("FE-P2-FLAT");
  constexpr auto prefix = "(set-logic QF_BV)(declare-const x (_ BitVec 8))";
  for (const bool capture : {false, true}) {
    auto input = std::string(prefix) + "(assert (and ";
    for (int i = 0; i < 256; ++i) input += "(let ((x x)) ";
    input += "(= ((_ extract 7 0) (bvadd x (_ bv18446744073709551617 8))) x)";
    input.append(256, ')');
    for (int i = 0; i < 256; ++i) input += " true";
    input += "))(check-sat)";
    auto session = Require(Session::FromReader(
        Chunks(input), ParseOptions{.capture_source_records = capture}, 17));
    RequireNext(session, CommandKind::kSetLogic);
    RequireNext(session, CommandKind::kDeclareConst);
    RequireNext(session, CommandKind::kAssert);
    EXPECT_LT(session.GetStats().peak_retained_fragments, 32);
    const auto query = RequireCheck(session);
    const auto operands = query->GetAssertions()[0].Borrow().GetChildren();
    ASSERT_EQ(operands.size(), 257);
    const auto equality = operands[0].GetChildren();
    ASSERT_EQ(equality.size(), 2);
    ASSERT_EQ(equality[0].GetKind(), Kind::kBvExtract);
    const auto sum = equality[0].GetChildren()[0].GetChildren();
    ASSERT_EQ(sum.size(), 2);
    EXPECT_EQ(sum[0], equality[1]);
    EXPECT_EQ(BvValue(sum[1]), "1");
  }

  for (const bool malformed : {false, true}) {
    const auto input = std::string(prefix) +
                       "(assert (= ((_ extract 0\r\n 7) x) #b0)" +
                       (malformed ? " extra)" : ")");
    auto session = Require(Session::FromReader(Chunks(input), 17));
    RequireNext(session, CommandKind::kSetLogic);
    RequireNext(session, CommandKind::kDeclareConst);
    const auto failure = session.ReadCommand();
    ASSERT_FALSE(failure);
    EXPECT_TRUE(session.GetPrefix().GetAssertions().empty());
    if (malformed) {
      EXPECT_EQ(failure.error().code, DiagnosticCode::kSyntax);
    } else {
      ASSERT_TRUE(failure.error().node_error);
      EXPECT_EQ(failure.error().node_error->parameter_index, 1);
      const auto at = input.find("7) x");
      EXPECT_EQ(failure.error().where,
                (SourceSpan{17, {at, 2, 2}, {at + 1, 2, 3}}));
    }
  }
}

TEST(FrontendPhaseTwo, IndexedNumeralAcrossRefill) {
  SCOPED_TRACE("FE-P2-FLAT");
  constexpr auto numeral = "|bv18446744073709551617|";
  const auto input = std::string("(set-logic QF_BV)(assert (and (= (_ ") +
                     numeral + " 65) #b1" + std::string(63, '0') +
                     "1) (= (_ bv1 1) #b1)))(check-sat)";
  const auto start = input.find(numeral);
  const auto end = start + std::string_view(numeral).size();
  // Refill within the name, after an intact name, and after the width.
  for (const auto split : {start + 5, end + 1, end + 3}) {
    SCOPED_TRACE(split);
    auto session = Require(Session::FromReader(Chunks(input, split)));
    RequireNext(session, CommandKind::kSetLogic);
    RequireNext(session, CommandKind::kAssert);
    const auto query = RequireCheck(session);
    for (const auto eq : query->GetAssertions()[0].Borrow().GetChildren()) {
      const auto args = eq.GetChildren();
      ASSERT_EQ(args.size(), 2);
      EXPECT_EQ(args[0], args[1]);
    }
  }
}

TEST(FrontendPhaseTwo, IndexedNumeralWrongIndexCount) {
  SCOPED_TRACE("FE-P2-FLAT");
  constexpr std::string_view text =
      "(set-logic QF_BV)\r\n(assert (= (_ bv1 2 3) #b01))(assert true)";
  auto session = Require(Session::FromReader(Chunks(text), 17));
  RequireNext(session, CommandKind::kSetLogic);
  const auto attempted = session.ReadCommand();
  ASSERT_FALSE(attempted);
  EXPECT_EQ(attempted.error().code, DiagnosticCode::kInvalidInput);
  EXPECT_EQ(attempted.error().message, "Wrong number of indices");
  const auto begin = text.find("(_ bv1");
  EXPECT_EQ(attempted.error().where,
            (SourceSpan{17, {begin, 2, 12}, {begin + 11, 2, 23}}));
  EXPECT_TRUE(session.GetPrefix().GetAssertions().empty());
  RequireNext(session, CommandKind::kAssert);
}

TEST(FrontendPhaseTwo, Datatype) {
  SCOPED_TRACE("FE-P2-DATATYPE");
  constexpr std::string_view group = R"(
(declare-datatypes ((Tree 1) (Forest 1))
  ((par (T) ((leaf (value T)) (node (children (Forest T)))))
   (par (U) ((nil) (cons (first (Tree U)) (rest (Forest U)))))))
)";
  // The two mutually recursive sort applications use the same Int instance.
  const auto suffix = std::string(
      "(declare-const t (Tree Int))(declare-const f (Forest Int))"
      "(assert (= t (node f)))(assert (= f (cons t (as nil (Forest Int)))))"
      "(assert (= t (match t (((leaf v) t) ((node xs) (node xs))))))"
      "(check-sat)");
  for (std::string_view invalid :
       {"", "(declare-datatypes ((Tree 1) (Forest 1)) ())",
        "(declare-datatype Tree ((c (f Unknown))))",
        "(declare-datatype Tree ((c (f Tree))))"}) {
    const auto text = std::string(
                          "(set-logic ALL)(declare-sort Prior "
                          "0)(declare-datatype Old ((old)))") +
                      std::string(invalid) + std::string(group) + suffix;
    std::optional<Session> session;
    bool observe = false;
    auto read = Chunks(text);
    session.emplace(
        Require(Session::FromReader([&](std::span<char> out) -> ReadResult {
          if (observe) {
            EXPECT_EQ(session->GetPrefix().GetDatatypeGroups().size(), 1);
            EXPECT_EQ(session->GetPrefix().GetUserSorts().size(), 1);
          }
          return read(out);
        })));
    RequireNext(*session, CommandKind::kSetLogic);
    RequireNext(*session, CommandKind::kDeclareSort);
    RequireNext(*session, CommandKind::kDeclareDatatype);
    observe = true;
    if (!invalid.empty())
      ExpectError(session->ReadCommand(), DiagnosticCode::kInvalidInput);
    RequireNext(*session, CommandKind::kDeclareDatatypes);
    observe = false;
    EXPECT_EQ(session->GetPrefix().GetDatatypeGroups().size(), 2);
    RequireNext(*session, CommandKind::kDeclareConst);
    RequireNext(*session, CommandKind::kDeclareConst);
    for (int i = 0; i < 3; ++i) RequireNext(*session, CommandKind::kAssert);
    EXPECT_EQ(RequireCheck(*session)->GetAssertions().size(), 3);
  }
}

TEST(FrontendPhaseTwo, FloatingPoint) {
  SCOPED_TRACE("FE-P2-FP");
  constexpr std::string_view setup =
      "(set-logic QF_FP)(declare-const r RoundingMode)";
  const auto text = std::string("(set-option :produce-models true)") +
                    std::string(setup) +
                    "(check-sat)"
                    "(get-value (((_ to_fp 8 24) r (/ (- 1.5) (/ 3 2)))))";
  auto session = Require(Session::FromReader(Chunks(text)));
  RequireNext(session, CommandKind::kSetOption);
  RequireNext(session, CommandKind::kSetLogic);
  RequireNext(session, CommandKind::kDeclareConst);
  RequireCheck(session);
  const auto result = RequireNext(session, CommandKind::kGetValue);
  const auto fp = result.values.front().term.Borrow();
  ASSERT_EQ(fp.GetChildren().size(), 1);
  ASSERT_TRUE(fp.GetRationalParameter());
  EXPECT_EQ(fp.GetRationalParameter()->Numerator().ToDecimal(), "-1");
  EXPECT_EQ(fp.GetRationalParameter()->Denominator().ToDecimal(), "1");
  const auto zero =
      Values("((_ to_fp 8 24) r (/ 1\r\n; skip\n (/ 0 3)))", setup);
  ExpectError(zero, DiagnosticCode::kUnsupported);
  ASSERT_FALSE(zero);
  EXPECT_EQ(zero.error().where.end.offset - zero.error().where.begin.offset, 7);
  for (const auto term :
       {"((_ to_fp 8 24) r (- (- 4) 1))", "((_ to_fp 8 24) r (- 1) #b1)",
        "(let ((/ #b1)) ((_ to_fp 8 24) r (/ 1 2)))"})
    ExpectError(Values(term, "(set-logic ALL)(declare-const r RoundingMode)"),
                DiagnosticCode::kInvalidInput);
  const auto third = Values("((_ to_fp 8 24) r (/ 1 (/ 0 3)) #b1)",
                            "(set-logic ALL)(declare-const r RoundingMode)");
  ExpectError(third, DiagnosticCode::kUnsupported);
  EXPECT_FALSE(third.error().node_error);
  for (const auto name : {"/", "-"}) {
    const auto signature = std::string(
                               "(set-logic QF_UFFP)"
                               "(declare-const r RoundingMode)(declare-fun |") +
                           name + "| (Bool Bool) (_ BitVec 1))";
    const auto good = Require(
        Values(std::string("((_ to_fp 8 24) r (") + name + " true false))",
               signature));
    EXPECT_EQ(good.values[0].term.Borrow().GetKind(), Kind::kFpFromSbv);
    ExpectError(
        Values(std::string("((_ to_fp 8 24) r (") + name + " 1 2))", signature),
        DiagnosticCode::kInvalidInput);
  }
}

TEST(FrontendPhaseTwo, DatatypeCountMismatchPreservesFirstDeferredDiagnostic) {
  auto session = Require(Session::FromReader(
      Chunks("(set-logic QF_DT)(declare-datatypes ((D 0) (D 0)) (((d))))"
             "(declare-datatypes ((D 0) (E 0)) (((d))))"
             "(declare-datatype D ((d)))(check-sat)")));
  RequireNext(session, CommandKind::kSetLogic);
  const auto duplicate = session.ReadCommand();
  ExpectError(duplicate, DiagnosticCode::kInvalidInput);
  ASSERT_FALSE(duplicate);
  EXPECT_EQ(duplicate.error().message, "Sort name is already in use");
  const auto mismatch = session.ReadCommand();
  ExpectError(mismatch, DiagnosticCode::kInvalidInput);
  ASSERT_FALSE(mismatch);
  EXPECT_EQ(mismatch.error().message, "Mismatched datatype headers and bodies");
  EXPECT_TRUE(session.GetPrefix().GetDatatypeGroups().empty());
  RequireNext(session, CommandKind::kDeclareDatatype);
  EXPECT_EQ(RequireCheck(session)->GetDatatypeGroups().size(), 1);
}

TEST(FrontendPhaseTwo, DatatypeFieldResolvesGroupLocalArrayName) {
  auto session = Require(Session::FromReader(
      Chunks("(set-logic QF_DT)"
             "(declare-datatypes ((Array 0) (D 0)) (((a)) ((d (child Array)))))"
             "(assert (= (child (d a)) a))(check-sat)")));
  RequireNext(session, CommandKind::kSetLogic);
  RequireNext(session, CommandKind::kDeclareDatatypes);
  RequireNext(session, CommandKind::kAssert);
  const auto query = RequireCheck(session);
  ASSERT_EQ(query->GetDatatypeGroups().size(), 1);
  const auto assertion = query->GetAssertions()[0].Borrow();
  const auto child = assertion.GetChildren()[0];
  const auto a = assertion.GetChildren()[1];
  EXPECT_EQ(child.GetSort(), a.GetSort());
  EXPECT_EQ(child.GetDeclaration()->GetDeclaration().GetKind(),
            DeclKind::kDatatypeSelector);
}

TEST(FrontendPhaseTwo, Precedence) {
  SCOPED_TRACE("FE-P2-PRECEDENCE");
  for (const auto tail :
       {"(assert (and missing true) true)", "(assert (and missing #xg))",
        "(assert (and missing true)",
        "(define-fun-rec f () Bool (let () true))"}) {
    const auto text = std::string("(set-logic ALL)") + tail;
    auto session = Require(Session::FromReader(Chunks(text)));
    RequireNext(session, CommandKind::kSetLogic);
    ExpectError(session.ReadCommand(), DiagnosticCode::kSyntax);
    EXPECT_FALSE(Require(session.ReadCommand()));
  }
  auto session = Require(Session::FromText(
      "(set-logic ALL)(assert (and first second))(assert (missing true))"
      "(define-fun-rec f () Bool true)(assert true)"));
  RequireNext(session, CommandKind::kSetLogic);
  const auto first = session.ReadCommand();
  ExpectError(first, DiagnosticCode::kInvalidInput);
  ASSERT_FALSE(first);
  EXPECT_EQ(first.error().where.end.offset - first.error().where.begin.offset,
            5);
  ExpectError(session.ReadCommand(), DiagnosticCode::kInvalidInput);
  ExpectError(session.ReadCommand(), DiagnosticCode::kUnsupported);
  RequireNext(session, CommandKind::kAssert);
  EXPECT_EQ(session.GetPrefix().GetAssertions().size(), 1);
  constexpr auto open_head = std::string_view("(assert (forall ((x Bool)) x))");
  const auto script =
      std::string("(set-logic ALL)\r\n") + std::string(open_head);
  auto reader = Require(Session::FromReader(Chunks(script), 29));
  RequireNext(reader, CommandKind::kSetLogic);
  const auto binder = reader.ReadCommand();
  ExpectError(binder, DiagnosticCode::kUnsupported);
  ASSERT_FALSE(binder);
  EXPECT_EQ(binder.error().where, (SourceSpan{29, {25, 2, 9}, {46, 2, 30}}));
}

TEST(FrontendPhaseTwo, ReaderRejectsRecursiveSessionReadsAndAllowsInspection) {
  constexpr std::string_view prefix =
      "(set-option :print-success true)(set-logic QF_SAT)(assert true)(assert ";
  constexpr std::string_view text =
      "(set-option :print-success true)(set-logic QF_SAT)(assert true)(assert "
      "false)(check-sat)";
  for (const bool outer_run : {false, true}) {
    SCOPED_TRACE(outer_run);
    auto read = Chunks(text, prefix.size());
    std::optional<Session> session;
    std::size_t refills = 0;
    CapturedSink nested_sink;
    session.emplace(Require(Session::FromReader(
        [&](std::span<char> out) -> ReadResult {
          if (++refills == 2) {
            EXPECT_EQ(session->GetPrefix().GetAssertions().size(), 1);
            EXPECT_TRUE(session->GetOptions().print_success);
            EXPECT_EQ(session->GetPhase(), SessionPhase::kBuilding);
            EXPECT_EQ(session->GetSourceRecords()->assertions.size(), 1);
            ExpectError(session->ReadCommand(), DiagnosticCode::kInvalidState);
            const auto nested = session->Run({}, nested_sink.Get());
            EXPECT_NE(nested.status, 0);
            EXPECT_EQ(
                nested.diagnostic.transform(
                    [](const Diagnostic& failure) { return failure.code; }),
                DiagnosticCode::kInvalidState);
            EXPECT_TRUE(nested_sink.output.empty());
            EXPECT_EQ(refills, 2);
          }
          return read(out);
        },
        ParseOptions{.capture_source_records = true}, 23)));
    if (outer_run) {
      CapturedSink sink;
      const auto result = session->Run(
          [&](const QueryContext& query,
              SessionOptions) -> Outcome<CheckResult> {
            EXPECT_EQ(query.GetAssertions().size(), 2);
            return CheckResult{
                CheckAnswer::kUnknown, UnknownReason::kUnknown, {}};
          },
          sink.Get());
      EXPECT_EQ(result.status, 0);
      EXPECT_EQ(sink.output, "success\nsuccess\nsuccess\nsuccess\nunknown\n");
    } else {
      RequireNext(*session, CommandKind::kSetOption);
      RequireNext(*session, CommandKind::kSetLogic);
      RequireNext(*session, CommandKind::kAssert);
      RequireNext(*session, CommandKind::kAssert);
      EXPECT_EQ(RequireCheck(*session)->GetAssertions().size(), 2);
      EXPECT_FALSE(Require(session->ReadCommand()));
    }
    EXPECT_EQ(refills, 3);
  }
}

TEST(FrontendPhaseTwo, ReaderRejectsRecursiveSyntaxReads) {
  constexpr std::string_view text = "(assert true)(exit)";
  auto read = Chunks(text, std::string_view("(assert ").size());
  std::optional<SyntaxValidator> syntax;
  std::size_t refills = 0;
  syntax.emplace(Require(SyntaxValidator::FromReader(
      [&](std::span<char> out) -> ReadResult {
        if (++refills == 2) {
          ExpectError(syntax->ReadNext(), DiagnosticCode::kInvalidState);
          EXPECT_EQ(refills, 2);
        }
        return read(out);
      },
      24)));
  const auto assertion = Require(syntax->ReadNext());
  ASSERT_TRUE(assertion);
  EXPECT_EQ(assertion->kind, CommandKind::kAssert);
  EXPECT_EQ(assertion->where, (SourceSpan{24, {}, {13, 1, 14}}));
  const auto exit = Require(syntax->ReadNext());
  ASSERT_TRUE(exit);
  EXPECT_EQ(exit->kind, CommandKind::kExit);
  EXPECT_FALSE(Require(syntax->ReadNext()));
  EXPECT_EQ(refills, 2);
}

TEST(FrontendPhaseTwo, Staging) {
  SCOPED_TRACE("FE-P2-STAGING");
  const std::string prefix =
      "(set-logic ALL)(declare-const prior Bool)(assert (! prior :named keep))";
  for (const bool capture : {false, true}) {
    for (const auto tail :
         {"n))", "n 1))(assert (! true :named n))", "n) extra)"}) {
      const auto text = prefix + "(assert (and (! true :named n) " + tail;
      std::optional<Session> session;
      bool inspect = false;
      auto read = Chunks(text);
      session.emplace(Require(Session::FromReader(
          [&](std::span<char> out) -> ReadResult {
            if (inspect) {
              EXPECT_EQ(session->GetPhase(), SessionPhase::kBuilding);
              EXPECT_FALSE(session->GetOptions().print_success);
              EXPECT_EQ(session->GetPrefix().GetAssertions().size(), 1);
              EXPECT_EQ(session->GetPrefix().GetFunctions().size(), 1);
              if (capture) {
                EXPECT_EQ(session->GetSourceRecords()->named_terms.size(), 1);
              }
            }
            return read(out);
          },
          ParseOptions{.capture_source_records = capture})));
      RequireNext(*session, CommandKind::kSetLogic);
      const auto before = session->GetStats().staging_overlays;
      RequireNext(*session, CommandKind::kDeclareConst);
      EXPECT_EQ(session->GetStats().staging_overlays, before);
      RequireNext(*session, CommandKind::kAssert);
      inspect = true;
      const auto attempt = session->ReadCommand();
      inspect = false;
      if (std::string_view(tail).contains("extra")) {
        ExpectError(attempt, DiagnosticCode::kSyntax);
        EXPECT_EQ(session->GetPrefix().GetAssertions().size(), 1);
        EXPECT_FALSE(Require(session->ReadCommand()));
        continue;
      }
      if (std::string_view(tail).contains('1')) {
        ExpectError(attempt, DiagnosticCode::kInvalidInput);
        EXPECT_EQ(session->GetPrefix().GetAssertions().size(), 1);
        RequireNext(*session, CommandKind::kAssert);
      } else {
        ASSERT_TRUE(attempt && *attempt);
      }
      EXPECT_EQ(session->GetPrefix().GetAssertions().size(), 2);
      if (capture) {
        EXPECT_EQ(session->GetSourceRecords()->named_terms.size(), 2);
      }
    }
  }
}

TEST(FrontendPhaseTwo, Resource) {
  SCOPED_TRACE("FE-P2-RESOURCE");
  constexpr std::string_view input =
      "(set-logic ALL)(assert true)"
      "(assert (and (! false :named n) n))(assert (! true :named n))";
  for (const bool capture : {false, true}) {
    std::size_t failures = 0;
    for (std::size_t checkpoint = 0; checkpoint < 128; ++checkpoint) {
      auto session = Require(Session::FromReader(
          Chunks(input), ParseOptions{.capture_source_records = capture}));
      RequireNext(session, CommandKind::kSetLogic);
      RequireNext(session, CommandKind::kAssert);
      const auto original = session.GetPrefix().GetAssertions().front();
      session.FailAllocationAfter(checkpoint);
      const auto attempted = session.ReadCommand();
      if (attempted) break;
      ++failures;
      ExpectError(attempted, DiagnosticCode::kResource);
      ASSERT_TRUE(attempted.error().node_error);
      EXPECT_EQ(attempted.error().node_error->resource,
                ResourceKind::kCppAllocation);
      ASSERT_EQ(session.GetPrefix().GetAssertions().size(), 1);
      EXPECT_EQ(session.GetPrefix().GetAssertions().front(), original);
      if (session.GetPhase() == SessionPhase::kTerminated) {
        EXPECT_FALSE(Require(session.ReadCommand()));
      } else {
        RequireNext(session, CommandKind::kAssert);
        EXPECT_EQ(session.GetPrefix().GetAssertions().size(), 2);
        if (capture) {
          EXPECT_EQ(session.GetSourceRecords()->named_terms.size(), 1);
        }
      }
    }
    EXPECT_GT(failures, 0);
    EXPECT_LT(failures, 128);
  }
  constexpr auto nested =
      "(set-logic ALL)(assert true)"
      "(assert (not (not false)))(echo \"unread\")";
  std::size_t reads = 0;
  auto grammar = Require(Session::FromReader(Chunks(nested, 0, &reads)));
  RequireNext(grammar, CommandKind::kSetLogic);
  RequireNext(grammar, CommandKind::kAssert);
  grammar.FailGrammarAllocationAfter(0);
  ExpectError(grammar.ReadCommand(), DiagnosticCode::kResource);
  const auto stopped = reads;
  EXPECT_EQ(grammar.GetPhase(), SessionPhase::kTerminated);
  EXPECT_FALSE(Require(grammar.ReadCommand()));
  EXPECT_EQ(reads, stopped);

  const auto split_literal =
      std::string("(set-logic ALL)(echo \"") + std::string(128, 'x') + "\")";
  auto lexeme = Require(Session::FromReader(Chunks(split_literal)));
  RequireNext(lexeme, CommandKind::kSetLogic);
  lexeme.FailGrammarAllocationAfter(0);
  const auto scratch = lexeme.ReadCommand();
  ExpectError(scratch, DiagnosticCode::kResource);
  ASSERT_FALSE(scratch);
  ASSERT_TRUE(scratch.error().node_error);
  EXPECT_EQ(scratch.error().node_error->resource, ResourceKind::kCppAllocation);
  EXPECT_EQ(lexeme.GetPhase(), SessionPhase::kTerminated);
  EXPECT_FALSE(Require(lexeme.ReadCommand()));

  auto malformed = Require(Session::FromText(
      "(set-logic ALL)(assert (and (! true :named n) true) bad)"));
  RequireNext(malformed, CommandKind::kSetLogic);
  malformed.FailAllocationAfter(0);
  ExpectError(malformed.ReadCommand(), DiagnosticCode::kSyntax);
  EXPECT_EQ(malformed.GetPhase(), SessionPhase::kTerminated);
  EXPECT_TRUE(malformed.GetPrefix().GetAssertions().empty());
}

TEST(FrontendPhaseTwo, InputIo) {
  SCOPED_TRACE("FE-P2-INPUT-IO");
  constexpr std::string_view input =
      "(set-logic ALL)(assert (! true :named keep))(assert (and (! true :named "
      "n) ";
  const auto error = std::make_error_code(std::errc::broken_pipe);
  for (int fault = 0; fault < 4; ++fault) {
    std::size_t reads = 0;
    auto session = Require(Session::FromReader(
        [&](std::span<char> out) -> ReadResult {
          if (++reads == 1) {
            std::ranges::copy(input, out.begin());
            return input.size();
          }
          if (fault == 0) return std::unexpected(error);
          if (fault == 1) throw std::runtime_error("reader failed");
          if (fault == 2) return out.size() + 1;
          return 0;
        },
        ParseOptions{.capture_source_records = true}, 33));
    RequireNext(session, CommandKind::kSetLogic);
    RequireNext(session, CommandKind::kAssert);
    const auto next = session.ReadCommand();
    ExpectError(
        next, fault == 3 ? DiagnosticCode::kSyntax : DiagnosticCode::kInputIo);
    ASSERT_FALSE(next);
    if (fault != 3) {
      EXPECT_EQ(next.error().where.begin.offset, input.size());
    }
    if (fault == 0) {
      EXPECT_EQ(next.error().io_error, error);
    }
    EXPECT_EQ(session.GetPrefix().GetAssertions().size(), 1);
    EXPECT_EQ(session.GetSourceRecords()->named_terms.size(), 1);
    EXPECT_FALSE(Require(session.ReadCommand()));
    EXPECT_EQ(reads, 2);
  }
}

TEST(FrontendPhaseTwo, RunGate) {
  SCOPED_TRACE("FE-P2-RUN-GATE");
  for (const auto& [answer, tail, code] :
       {std::tuple{CheckAnswer::kUnknown, "(get-value (missing))",
                   DiagnosticCode::kModelUnavailable},
        {CheckAnswer::kUnknown, "(get-value ())", DiagnosticCode::kSyntax},
        {CheckAnswer::kUnknown, "(get-value (missing)",
         DiagnosticCode::kSyntax},
        {CheckAnswer::kUnknown, "(get-value (missing) extra)",
         DiagnosticCode::kSyntax},
        {CheckAnswer::kUnsat, "(get-model)", DiagnosticCode::kInvalidState},
        {CheckAnswer::kUnsat, "(get-model extra)", DiagnosticCode::kSyntax},
        {CheckAnswer::kSat, "(get-info :reason-unknown)",
         DiagnosticCode::kInvalidState},
        {CheckAnswer::kSat, "(get-info :reason-unknown extra)",
         DiagnosticCode::kSyntax}}) {
    const auto text =
        std::string(
            "(set-option :produce-models true)(set-logic ALL)(check-sat)") +
        tail;
    auto session = Require(Session::FromText(text));
    CapturedSink sink;
    int checks = 0;
    auto result = session.Run(
        [&](const QueryContext&, SessionOptions) -> Outcome<CheckResult> {
          ++checks;
          return CheckResult{answer, UnknownReason::kTimeout,
                             answer == CheckAnswer::kSat
                                 ? std::make_unique<ControlledModel>()
                                 : nullptr};
        },
        sink.Get());
    EXPECT_EQ(checks, 1);
    ASSERT_TRUE(result.diagnostic);
    EXPECT_EQ(result.diagnostic->code, code);
    EXPECT_EQ(sink.flushes, 2);
    EXPECT_TRUE(sink.output.contains("(error "));
  }
  for (bool checked : {false, true}) {
    const auto text =
        checked ? "(set-logic ALL)(check-sat)(get-model)"
                : "(set-option :produce-models true)(get-value (missing))";
    auto disabled = Require(Session::FromText(text));
    CapturedSink rejected;
    int checks = 0;
    const auto result = disabled.Run(
        [&](const QueryContext&, SessionOptions) -> Outcome<CheckResult> {
          ++checks;
          return CheckResult{CheckAnswer::kSat, UnknownReason::kUnknown, {}};
        },
        rejected.Get());
    EXPECT_EQ(checks, checked ? 1 : 0);
    ASSERT_TRUE(result.diagnostic);
    EXPECT_EQ(result.diagnostic->code, DiagnosticCode::kInvalidState);
    EXPECT_EQ(rejected.output, (checked ? "sat\n" : "") +
                                   DiagnosticResponse(*result.diagnostic));
  }
  CapturedSink sink;
  auto session = Require(Session::FromText(
      "(set-logic ALL)(check-sat)(get-info :reason-unknown)"));
  EXPECT_EQ(
      session
          .Run(
              [](const QueryContext&, SessionOptions) -> Outcome<CheckResult> {
                return CheckResult{
                    CheckAnswer::kUnknown, UnknownReason::kTimeout, {}};
              },
              sink.Get())
          .status,
      0);
  EXPECT_EQ(sink.output, "unknown\n(:reason-unknown timeout)\n");
}

TEST(FrontendPhaseTwo, CheckExit) {
  SCOPED_TRACE("FE-P2-CHECK-EXIT");
  constexpr std::string_view check = "(set-logic ALL)(check-sat)";
  constexpr std::string_view exit = "(exit)\xff))";
  for (bool buffered : {false, true}) {
    int checks = 0;
    int reads = 0;
    CapturedSink sink;
    const auto input = std::string(check) + std::string(exit);
    auto session =
        Require(Session::FromReader([&](std::span<char> out) -> ReadResult {
          ++reads;
          if (reads > 2) throw std::runtime_error("suffix read");
          if (reads == 2) {
            EXPECT_EQ(checks, 1);
            EXPECT_EQ(sink.output, "sat\n");
            EXPECT_EQ(sink.flushes, 1);
          }
          const auto part = buffered     ? std::string_view(input)
                            : reads == 1 ? check
                                         : exit;
          std::ranges::copy(part, out.begin());
          return part.size();
        }));
    EXPECT_EQ(session
                  .Run(
                      [&](const QueryContext&,
                          SessionOptions) -> Outcome<CheckResult> {
                        ++checks;
                        return CheckResult{
                            CheckAnswer::kSat, UnknownReason::kUnknown, {}};
                      },
                      sink.Get())
                  .status,
              0);
    EXPECT_EQ(checks, 1);
    EXPECT_EQ(reads, buffered ? 1 : 2);
    EXPECT_FALSE(Require(session.ReadCommand()));
  }
  CapturedSink sink;
  auto truncated = Require(Session::FromText("(set-logic ALL)(check-sat"));
  EXPECT_EQ(truncated.Run({}, sink.Get()).diagnostic->code,
            DiagnosticCode::kSyntax);
  auto malformed = Require(Session::FromText("(exit x)"));
  ExpectError(malformed.ReadCommand(), DiagnosticCode::kSyntax);
}

TEST(FrontendPhaseTwo, Responses) {
  SCOPED_TRACE("FE-P2-RESPONSES");
  constexpr std::string_view input =
      "(set-option :print-success true)(get-option :produce-models)"
      "(get-option :verbosity)(get-info :name)(get-info :custom)"
      "(set-option :print-success false)(echo \"a\"\"🌱\r\nnext\")(exit)";
  const auto echo = [&] {
    const auto text = std::string(input);
    auto session = Require(Session::FromReader(Chunks(text)));
    for (const auto& [kind, expected] :
         {std::pair{CommandKind::kSetOption, CommandResponse::kSuccess},
          {CommandKind::kGetOption, CommandResponse::kFalse},
          {CommandKind::kGetOption, CommandResponse::kZero},
          {CommandKind::kGetInfo, CommandResponse::kName},
          {CommandKind::kGetInfo, CommandResponse::kUnsupported},
          {CommandKind::kSetOption, CommandResponse::kNone}})
      EXPECT_EQ(Tag(RequireNext(session, kind)), expected);
    return RequireNext(session, CommandKind::kEcho);
  }();
  EXPECT_EQ(std::get<std::string>(echo.response), "\"a\"\"🌱\r\nnext\"");
  CapturedSink sink;
  auto session = Require(Session::FromText(input));
  EXPECT_EQ(session.Run({}, sink.Get()).status, 0);
  EXPECT_EQ(sink.output,
            "success\nfalse\n0\n(:name "
            "\"Dandelion\")\nunsupported\n\"a\"\"🌱\r\nnext\"\n");
  EXPECT_EQ(sink.flushes, 6);
}

TEST(FrontendPhaseTwo, Values) {
  SCOPED_TRACE("FE-P2-VALUES");
  const auto query = RequireQuery(
      Parse("(set-option :produce-models true)(set-logic ALL)(declare-sort U 0)"
            "(declare-const a U)(define-const alias U a)(declare-const "
            "|look@value0| Bool)(check-sat)"));
  int evaluations = 0;
  auto owner = Require(ModelOwner::Create(
      query,
      std::make_unique<ControlledModel>(
          [&](ModelOwner& model, NodeView term) -> Outcome<ValueRef> {
            ++evaluations;
            return model.MakeValue(term.GetSort(), "a");
          }),
      CheckAnswer::kSat));
  CapturedSink sink;
  auto expose = Require(Session::ForModelText(owner, "(get-value (a))"));
  EXPECT_EQ(expose.Run({}, sink.Get()).status, 0);
  ASSERT_TRUE(sink.output.contains("@value0"));
  auto foreign = Require(ModelOwner::Create(
      query, std::make_unique<ControlledModel>(), CheckAnswer::kSat));
  for (bool capture : {false, true}) {
    const auto result = [&] {
      const std::string text =
          "(get-value ( alias ; comment\r\n |look@value0| @value0 (as @value0 "
          "U)))";
      auto reader = Require(Session::ForModelReader(
          owner, Chunks(text), ParseOptions{.capture_source_records = capture},
          38));
      return RequireNext(reader, CommandKind::kGetValue);
    }();
    ASSERT_EQ(result.values.size(), 4);
    EXPECT_EQ(result.values[0].input, "alias");
    EXPECT_EQ(Applied(result.values[0].term.Borrow()),
              query->GetFunctions()[0].Borrow());
    EXPECT_EQ(result.values[1].input, "|look@value0|");
    EXPECT_EQ(result.values[2].input, "(as @value0 U)");
    EXPECT_EQ(result.values[3].input, "(as @value0 U)");
    EXPECT_EQ(result.values[2].term, result.values[3].term);
    EXPECT_EQ(result.values[0].where.begin, (SourcePosition{13, 1, 14}));
    EXPECT_EQ(result.values[0].where.source, 38);
  }
  auto detached = Require(Session::ForModelText(
      owner, "(get-value (a))(get-value (a @value42))(get-value (a))"));
  ExpectError(detached.ReadCommand(foreign.get()),
              DiagnosticCode::kModelFailure);
  ExpectError(detached.ReadCommand(), DiagnosticCode::kInvalidInput);
  ASSERT_TRUE(Require(detached.ReadCommand(owner.get())));
  for (const auto& [request, code] :
       {std::pair{"(get-value (a (and true 1)))",
                  DiagnosticCode::kInvalidInput},
        {"(get-value (a (not)) extra)", DiagnosticCode::kSyntax},
        {"(get-value (a (let () true)))", DiagnosticCode::kSyntax},
        {"(get-value (a (forall ((x Bool)) x)))",
         DiagnosticCode::kUnsupported}}) {
    CapturedSink rejected;
    auto inspect = Require(Session::ForModelText(owner, request));
    const auto result = inspect.Run({}, rejected.Get());
    ASSERT_TRUE(result.diagnostic);
    EXPECT_EQ(result.diagnostic->code, code);
    EXPECT_FALSE(rejected.output.contains("((a "));
    EXPECT_EQ(rejected.flushes, 1);
  }
  auto no_owner =
      Require(Session::FromText("(set-option :produce-models true)(set-logic "
                                "ALL)(check-sat)(get-value (@value0))"));
  RequireNext(no_owner, CommandKind::kSetOption);
  RequireNext(no_owner, CommandKind::kSetLogic);
  RequireCheck(no_owner);
  ExpectError(no_owner.ReadCommand(), DiagnosticCode::kModelDependent);
  EXPECT_EQ(evaluations, 1);
}

TEST(FrontendPhaseTwo, ValueOutput) {
  SCOPED_TRACE("FE-P2-VALUE-OUTPUT");
  const auto query = RequireQuery(
      Parse("(set-option :produce-models true)(set-logic ALL)(declare-sort U 0)"
            "(declare-const a U)(declare-const b U)(check-sat)"));
  for (int fail = 0; fail < 3; ++fail) {
    bool fail_eval = false;
    auto owner = Require(ModelOwner::Create(
        query,
        std::make_unique<ControlledModel>(
            [&](ModelOwner& model, NodeView term) -> Outcome<ValueRef> {
              if (fail_eval && Applied(term).GetNameHint() == "a")
                return std::unexpected(ModelError());
              return model.MakeValue(term.GetSort(),
                                     std::string(Applied(term).GetNameHint()));
            }),
        CheckAnswer::kSat));
    auto initial = Require(Session::ForModelText(owner, "(get-value (a))"));
    CapturedSink first;
    ASSERT_EQ(initial.Run({}, first.Get()).status, 0);
    auto later = Require(
        Session::ForModelText(owner, "(get-value (b a))(echo \"later\")"));
    CapturedSink sink;
    if (fail == 0) sink.StopAfter(11);
    if (fail == 1) sink.BreakFlush(1);
    if (fail == 2) fail_eval = true;
    const auto failed = later.Run({}, sink.Get());
    EXPECT_NE(failed.status, 0);
    EXPECT_EQ(failed.model, owner);
    EXPECT_EQ(later.GetPhase(), SessionPhase::kTerminated);
    EXPECT_FALSE(sink.output.contains("later"));
    EXPECT_FALSE(sink.output.contains("(error "));
    fail_eval = false;
    auto detached = Require(Session::ForModelText(
        owner, "(get-value (@value0))(get-value (@value1))(get-value (b))"));
    RequireNext(detached, CommandKind::kGetValue);
    ExpectError(detached.ReadCommand(), DiagnosticCode::kInvalidInput);
    CapturedSink retry;
    EXPECT_EQ(detached.Run({}, retry.Get()).status, 0);
    EXPECT_TRUE(retry.output.contains("@value1"));
  }
}

TEST(FrontendPhaseTwo, ValueOutputRegistryAllocationAndPreResponseFailure) {
  SCOPED_TRACE("FE-P2-VALUE-OUTPUT");
  const auto query = RequireQuery(
      Parse("(set-option :produce-models true)(set-logic ALL)(declare-sort U 0)"
            "(declare-const a U)(declare-const b U)(check-sat)"));
  for (const bool fail_allocation : {false, true}) {
    SCOPED_TRACE(fail_allocation);
    bool fail_eval = false;
    auto owner = Require(ModelOwner::Create(
        query,
        std::make_unique<ControlledModel>(
            [&](ModelOwner& model, NodeView term) -> Outcome<ValueRef> {
              if (fail_eval) return std::unexpected(ModelError());
              return model.MakeValue(term.GetSort(),
                                     std::string(Applied(term).GetNameHint()));
            }),
        CheckAnswer::kSat));
    auto initial = Require(Session::ForModelText(owner, "(get-value (a))"));
    CapturedSink prior;
    ASSERT_EQ(initial.Run({}, prior.Get()).status, 0);
    ASSERT_EQ(prior.output, "((a (as @value0 U)))\n");

    auto later = Require(
        Session::ForModelText(owner, "(get-value (a b))(echo \"later\")"));
    CapturedSink sink;
    auto response = sink.Get();
    auto write = std::move(response.write);
    response.write = [&](std::string_view bytes) -> WriteResult {
      if (fail_allocation && sink.output.empty()) later.FailAllocationAfter(0);
      return write(bytes);
    };
    fail_eval = !fail_allocation;
    const auto failed = later.Run({}, response);
    EXPECT_EQ(failed.status, 1);
    ASSERT_TRUE(failed.diagnostic);
    EXPECT_EQ(failed.model, owner);
    EXPECT_EQ(later.GetPhase(), SessionPhase::kTerminated);
    if (fail_allocation) {
      EXPECT_EQ(failed.diagnostic->code, DiagnosticCode::kResource);
      ASSERT_TRUE(failed.diagnostic->node_error);
      EXPECT_EQ(failed.diagnostic->node_error->resource,
                ResourceKind::kCppAllocation);
      EXPECT_EQ(sink.output, "((a (as @value0 U))");
      EXPECT_EQ(sink.flushes, 0);
    } else {
      EXPECT_EQ(failed.diagnostic->code, DiagnosticCode::kModelFailure);
      EXPECT_EQ(sink.output, DiagnosticResponse(*failed.diagnostic));
      EXPECT_EQ(sink.flushes, 1);
    }

    fail_eval = false;
    auto detached = Require(Session::ForModelText(
        owner, "(get-value (@value0))(get-value (@value1))(get-value (b))"));
    RequireNext(detached, CommandKind::kGetValue);
    ExpectError(detached.ReadCommand(), DiagnosticCode::kInvalidInput);
    CapturedSink retry;
    EXPECT_EQ(detached.Run({}, retry.Get()).status, 0);
    EXPECT_EQ(retry.output, "((b (as @value1 U)))\n");
  }
}

TEST(FrontendPhaseTwo, SyntaxOnly) {
  SCOPED_TRACE("FE-P2-SYNTAX-ONLY");
  constexpr std::string_view text = "(assert (missing x))(set-info :x (λ))";
  auto invalid = Require(SyntaxValidator::FromReader(Chunks(text), 13));
  const auto header = Require(invalid.ReadNext());
  ASSERT_TRUE(header);
  EXPECT_EQ(header->kind, CommandKind::kAssert);
  EXPECT_EQ(header->where, (SourceSpan{13, {}, {20, 1, 21}}));
  ExpectError(invalid.ReadNext(), DiagnosticCode::kSyntax);
  auto semantic = Require(Session::FromText(text));
  ExpectError(semantic.ReadCommand(), DiagnosticCode::kInvalidState);
  constexpr std::string_view valid =
      "(assert (missing x))(set-info :x (|λ|))(push 1)(exit)\xff";
  auto syntax =
      Require(SyntaxValidator::FromReader(Chunks(valid, valid.size())));
  for (auto kind : {CommandKind::kAssert, CommandKind::kSetInfo,
                    CommandKind::kUnsupported, CommandKind::kExit}) {
    const auto next = Require(syntax.ReadNext());
    ASSERT_TRUE(next);
    EXPECT_EQ(next->kind, kind);
  }
  EXPECT_FALSE(Require(syntax.ReadNext()));
  auto malformed = Require(SyntaxValidator::FromText("(assert (let ((a)) a))"));
  ExpectError(malformed.ReadNext(), DiagnosticCode::kSyntax);
}

TEST(FrontendPhaseTwo, BenchmarkFixture) {
  SCOPED_TRACE("FE-P2-BENCH");
  constexpr std::string_view text =
      "(set-info :status unknown)(set-info :status sat)(set-logic QF_LIA)"
      "(define-fun a () Int 1)(define-const b Int a)"
      "(define-fun id ((x Int)) Int x)(assert (= (id (! b :named n)) n))"
      "(check-sat)";
  for (auto session : {false, true}) {
    auto parser = Require(session ? Session::FromReader(Chunks(text))
                                  : Session::FromText(text));
    std::size_t count = 0;
    std::size_t definitions = 0;
    std::size_t end = 0;
    while (auto command = Require(parser.ReadCommand())) {
      ++count;
      end = command->header.where.end.offset;
      const auto kind = command->header.kind;
      if (kind == CommandKind::kDefineFun || kind == CommandKind::kDefineConst)
        ++definitions;
    }
    EXPECT_EQ(count, 8);
    EXPECT_EQ(definitions, 3);
    EXPECT_EQ(end, text.size());
    EXPECT_EQ(parser.GetPrefix().GetDefinitionCount(), 1);
    EXPECT_EQ(parser.GetPrefix().GetAssertions().size(), 1);
  }
}

}  // namespace
}  // namespace dandelion::frontend::testing

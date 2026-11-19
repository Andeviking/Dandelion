#include "test_support.h"

namespace dandelion::frontend::testing {
namespace {

constexpr std::uint32_t kSource = 73;
constexpr std::size_t kWindow = 64 * 1024;
constexpr std::array<std::size_t, 4> kIrregularChunks{3, 1, 29, 7};

Reader Chunked(std::string_view text, std::span<const std::size_t> chunks,
               std::size_t split = std::string_view::npos) {
  return [text, chunks, split, pos = std::size_t{0},
          next = std::size_t{0}](std::span<char> out) mutable -> ReadResult {
    const auto remaining = pos < split ? split - pos : text.size() - pos;
    const auto count =
        std::min({out.size(), text.size() - pos, remaining, chunks[next]});
    std::ranges::copy(text.substr(pos, count), out.begin());
    pos += count;
    next = (next + 1) % chunks.size();
    return count;
  };
}

SourcePosition PositionAt(std::string_view text, std::size_t offset) {
  SourcePosition result{offset, 1, 1};
  for (std::size_t i = 0; i < offset; ++i) {
    if (text[i] == '\r' || text[i] == '\n') {
      if (text[i] != '\n' || i == 0 || text[i - 1] != '\r') ++result.line;
      result.column = 1;
    } else {
      ++result.column;
    }
  }
  return result;
}

SourceSpan SpanAt(std::string_view text, std::size_t offset,
                  std::size_t length) {
  return {kSource, PositionAt(text, offset), PositionAt(text, offset + length)};
}

void ExpectSpan(SourceSpan actual, std::string_view text,
                std::string_view spelling, std::size_t from = 0) {
  const auto offset = text.find(spelling, from);
  ASSERT_NE(offset, std::string_view::npos) << spelling;
  const auto expected = SpanAt(text, offset, spelling.size());
  EXPECT_EQ(actual.source, expected.source);
  EXPECT_EQ(actual.begin.offset, expected.begin.offset);
  EXPECT_EQ(actual.end.offset, expected.end.offset);
  EXPECT_EQ(actual.begin.line, expected.begin.line);
  EXPECT_EQ(actual.end.line, expected.end.line);
  EXPECT_EQ(actual.begin.column, expected.begin.column);
  EXPECT_EQ(actual.end.column, expected.end.column);
}

constexpr std::string_view kProduceModels = "(set-option :produce-models true)";
constexpr std::string_view kLogic = "(set-logic ALL)";
constexpr std::string_view kMetadata =
    "(set-info :tag ( |π;\r\nx|  \"a\"\"🌱\" ))";
constexpr std::string_view kEcho = "(echo \"a\"\"🌱\")";

std::string StreamingScript(const std::string& symbol) {
  return std::string(kProduceModels) + "\r\n; ignored 🌱\r\n" +
         std::string(kLogic) + std::string(kMetadata) + "\n(declare-const " +
         symbol + " Int)\r\n(assert (= " + symbol +
         " 7))(check-sat)(get-value (  (+ " + symbol + " ; comment\r\n 7)\t" +
         symbol + "))" + std::string(kEcho) + "(exit)";
}

void ExpectStreamingScript(Session session, std::string_view text,
                           const std::string& symbol) {
  const auto apply = [&](CommandKind kind, std::string_view spelling) {
    const auto syntax = Require(session.NextSyntax());
    if (!syntax) throw std::runtime_error("Missing streaming command");
    EXPECT_EQ(syntax->kind, kind);
    ExpectSpan(syntax->where, text, spelling);
    auto typed = Require(session.Apply());
    EXPECT_EQ(typed.header.kind, kind);
    EXPECT_EQ(typed.header.where, syntax->where);
    return typed;
  };

  apply(CommandKind::kSetOption, kProduceModels);
  apply(CommandKind::kSetLogic, kLogic);
  apply(CommandKind::kSetInfo, kMetadata);
  const auto metadata = session.GetMetadata();
  ASSERT_EQ(metadata.size(), 1u);
  EXPECT_EQ(metadata[0].key, ":tag");
  EXPECT_EQ(metadata[0].value, "(|π;\r\nx| \"a\"\"🌱\")");
  ExpectSpan(metadata[0].where, text, ":tag ( |π;\r\nx|  \"a\"\"🌱\" )");

  const auto declaration = "(declare-const " + symbol + " Int)";
  const auto asserted_term = "(= " + symbol + " 7)";
  const auto assertion = "(assert " + asserted_term + ")";
  apply(CommandKind::kDeclareConst, declaration);
  apply(CommandKind::kAssert, assertion);
  const auto checked = apply(CommandKind::kCheckSat, "(check-sat)");
  const auto query = RequireQuery(checked.query);
  ASSERT_EQ(query->GetFunctions().size(), 1u);
  ASSERT_EQ(query->GetAssertions().size(), 1u);
  EXPECT_EQ(query->GetFunctions()[0].source_name, std::string_view(symbol));
  ExpectSpan(query->GetFunctions()[0].where, text, declaration);
  ExpectSpan(query->GetAssertions()[0].where, text, asserted_term);

  const auto operand = "(+ " + symbol + " ; comment\r\n 7)";
  const auto request = "(get-value (  " + operand + "\t" + symbol + "))";
  const auto values = apply(CommandKind::kGetValue, request);
  ASSERT_EQ(values.values.size(), 2u);
  EXPECT_EQ(values.values[0].input, "(+ " + symbol + " 7)");
  EXPECT_EQ(values.values[1].input, symbol);
  const auto request_start = text.find(request);
  ExpectSpan(values.values[0].where, text, operand, request_start);
  ExpectSpan(values.values[1].where, text, symbol,
             request_start + request.find('\t'));
  const auto sum = values.values[0].term.Borrow();
  ASSERT_EQ(sum.GetKind(), Kind::kIntAdd);
  const auto children = sum.GetChildren();
  ASSERT_EQ(children.size(), 2u);
  EXPECT_EQ(children[0], values.values[1].term.Borrow());
  EXPECT_EQ(IntegerValue(children[1]), "7");

  EXPECT_EQ(apply(CommandKind::kEcho, kEcho).text, "\"a\"\"🌱\"");
  apply(CommandKind::kExit, "(exit)");
  EXPECT_FALSE(Require(session.NextSyntax()));
}

TEST(FrontendOverhaul, ExactSpansAndRenderingAtEverySmallSplit) {
  SCOPED_TRACE("FE-OVERHAUL-01");
  const std::string symbol = "|α;🌱|";
  const auto text = StreamingScript(symbol);
  ExpectStreamingScript(Require(Session::FromText(text, kSource)), text,
                        symbol);
  constexpr std::array full_window{kWindow};
  for (std::size_t split = 1; split < text.size(); ++split) {
    SCOPED_TRACE(split);
    ExpectStreamingScript(Require(Session::FromReader(
                              Chunked(text, full_window, split), kSource)),
                          text, symbol);
  }
  for (const auto chunk : {std::size_t{1}, std::size_t{4096}, kWindow}) {
    SCOPED_TRACE(chunk);
    ExpectStreamingScript(Require(Session::FromReader(
                              Chunked(text, std::span(&chunk, 1)), kSource)),
                          text, symbol);
  }
  ExpectStreamingScript(
      Require(Session::FromReader(Chunked(text, kIrregularChunks), kSource)),
      text, symbol);
}

TEST(FrontendOverhaul, ReaderWindowBoundariesAndMultiWindowToken) {
  SCOPED_TRACE("FE-OVERHAUL-02");
  const std::string symbol = "|α;🌱|";
  const auto unpadded = StreamingScript(symbol);
  const auto quoted_data = unpadded.find(kMetadata);
  ASSERT_NE(quoted_data, std::string::npos);
  constexpr std::array full_window{kWindow};
  for (std::string_view target : {"\r\n", "🌱", "\"\""}) {
    const auto marker = unpadded.find(target, quoted_data);
    ASSERT_NE(marker, std::string::npos);
    for (std::size_t byte = 0; byte <= target.size(); ++byte) {
      SCOPED_TRACE(::testing::Message() << target << " at byte " << byte);
      const auto text = std::string(kWindow - marker - byte, ' ') + unpadded;
      ExpectStreamingScript(Require(Session::FromText(text, kSource)), text,
                            symbol);
      ExpectStreamingScript(
          Require(Session::FromReader(Chunked(text, full_window), kSource)),
          text, symbol);
    }
  }

  const auto long_symbol =
      "|" + std::string(2 * kWindow + 17, 'x') + "🌱\r\nend|";
  const auto text = StreamingScript(long_symbol);
  ExpectStreamingScript(Require(Session::FromText(text, kSource)), text,
                        long_symbol);
  for (const auto chunk : {std::size_t{1}, std::size_t{4096}, kWindow}) {
    SCOPED_TRACE(chunk);
    ExpectStreamingScript(Require(Session::FromReader(
                              Chunked(text, std::span(&chunk, 1)), kSource)),
                          text, long_symbol);
  }
  ExpectStreamingScript(
      Require(Session::FromReader(Chunked(text, kIrregularChunks), kSource)),
      text, long_symbol);
}

TEST(FrontendOverhaul, ReadErrorsAndMalformedUtf8HaveExactPositions) {
  SCOPED_TRACE("FE-OVERHAUL-02");
  constexpr std::string_view prefix = "(set-info :tag committed)";
  const auto text = std::string(prefix) + "\r\n" + std::string(kEcho);
  const auto io_error = std::make_error_code(std::errc::io_error);
  for (std::size_t stop = prefix.size(); stop < text.size(); ++stop) {
    SCOPED_TRACE(stop);
    auto read =
        Chunked(std::string_view(text).substr(0, stop), kIrregularChunks);
    auto session = Require(Session::FromReader(
        [read = std::move(read), io_error](std::span<char> out) -> ReadResult {
          const auto count = read(out);
          if (count == 0u) return std::unexpected(io_error);
          return count;
        },
        kSource));
    RequireNext(session, CommandKind::kSetInfo);
    const auto failed = session.NextSyntax();
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, DiagnosticCode::kInputIo);
    EXPECT_EQ(failed.error().io_error, io_error);
    EXPECT_EQ(failed.error().where, SpanAt(text, stop, 0));
    ASSERT_EQ(session.GetMetadata().size(), 1u);
    EXPECT_EQ(session.GetMetadata()[0].value, "committed");
    EXPECT_EQ(session.GetPhase(), SessionPhase::kTerminated);
    EXPECT_FALSE(Require(session.NextSyntax()));
  }

  const auto incomplete = std::string(prefix) + "\r\n(echo \"\xf0\x9f";
  const auto check = [&](Session session) {
    RequireNext(session, CommandKind::kSetInfo);
    const auto failed = session.NextSyntax();
    ASSERT_FALSE(failed);
    EXPECT_EQ(failed.error().code, DiagnosticCode::kSyntax);
    EXPECT_EQ(failed.error().where,
              SpanAt(incomplete, incomplete.size() - 2, 0));
    EXPECT_FALSE(Require(session.NextSyntax()));
  };
  check(Require(Session::FromText(incomplete, kSource)));
  constexpr std::array full_window{kWindow};
  for (std::size_t split = 1; split < incomplete.size(); ++split) {
    SCOPED_TRACE(split);
    check(Require(
        Session::FromReader(Chunked(incomplete, full_window, split), kSource)));
  }
}

TEST(FrontendOverhaul, ExitLeavesSuffixUnreadEvenWithBufferedGarbage) {
  SCOPED_TRACE("FE-OVERHAUL-02");
  constexpr std::string_view tail = "unterminated suffix";
  for (std::string_view first : {"(exit)", "(exit)|"}) {
    SCOPED_TRACE(first);
    const auto text = std::string(first) + std::string(tail);
    std::size_t cursor = 0;
    std::size_t calls = 0;
    auto session = Require(Session::FromReader(
        [&](std::span<char> out) -> ReadResult {
          ++calls;
          const auto chunk = std::string_view(text).substr(
              cursor, cursor == 0 ? first.size() : tail.size());
          std::ranges::copy(chunk, out.begin());
          cursor += chunk.size();
          return chunk.size();
        },
        kSource));
    const auto command = RequireNext(session, CommandKind::kExit);
    ExpectSpan(command.header.where, text, "(exit)");
    EXPECT_EQ(session.GetPhase(), SessionPhase::kTerminated);
    EXPECT_FALSE(Require(session.NextSyntax()));
    EXPECT_EQ(calls, 1u);
    EXPECT_EQ(cursor, first.size());
  }
}

TEST(FrontendOverhaul, PagedGrowthAndForcedWideningPreserveLists) {
  SCOPED_TRACE("FE-OVERHAUL-03");
  constexpr std::size_t kWidth = 2304;
  constexpr std::size_t kDepth = 1024;
  std::string wide = "(distinct";
  for (std::size_t i = 0; i < kWidth; ++i) wide += " " + std::to_string(i);
  wide += ")";
  std::string deep;
  for (std::size_t i = 0; i < kDepth; ++i) deep += "(not ";
  deep += "false" + std::string(kDepth, ')');
  const auto request = "(get-value (\r\n" + wide + "\r\n" + deep + "))";
  const auto text = std::string(kProduceModels) + std::string(kLogic) +
                    "(check-sat)" + request;

  for (const bool reader : {false, true}) {
    for (const bool promote : {false, true}) {
      SCOPED_TRACE(::testing::Message()
                   << "reader=" << reader << " promote=" << promote);
      auto session = Require(
          reader ? Session::FromReader(Chunked(text, kIrregularChunks), kSource)
                 : Session::FromText(text, kSource));
      if (promote) session.ForceSyntaxPromotionThreshold(257);
      RequireNext(session, CommandKind::kSetOption);
      RequireNext(session, CommandKind::kSetLogic);
      RequireCheck(session);
      const auto before = session.GetStats();
      const auto values = RequireNext(session, CommandKind::kGetValue);
      ExpectSpan(values.header.where, text, request);
      ASSERT_EQ(values.values.size(), 2u);
      ExpectSpan(values.values[0].where, text, wide);
      ExpectSpan(values.values[1].where, text, deep);
      EXPECT_EQ(values.values[0].input, wide);
      EXPECT_EQ(values.values[1].input, deep);

      const auto root = values.values[0].term.Borrow();
      ASSERT_EQ(root.GetKind(), Kind::kDistinct);
      const auto children = root.GetChildren();
      ASSERT_EQ(children.size(), kWidth);
      for (std::size_t i = 0; i < kWidth; ++i)
        EXPECT_EQ(IntegerValue(children[i]), std::to_string(i)) << i;
      auto nested = values.values[1].term.Borrow();
      for (std::size_t i = 0; i < kDepth; ++i) {
        ASSERT_EQ(nested.GetKind(), Kind::kNot) << i;
        ASSERT_EQ(nested.GetChildren().size(), 1u);
        nested = nested.GetChildren()[0];
      }
      ASSERT_EQ(nested.GetKind(), Kind::kBoolValue);
      EXPECT_EQ(nested.GetValue()->GetBool(), false);
      const auto stats = session.GetStats();
      EXPECT_GT(stats.syntax_page_allocations - before.syntax_page_allocations,
                1u);
      if (promote) {
        EXPECT_GT(stats.syntax_promoted_pages, before.syntax_promoted_pages);
      } else {
        EXPECT_EQ(stats.syntax_promoted_pages, 0u);
        EXPECT_EQ(stats.syntax_page_copies, 0u);
      }
    }
  }
}

TEST(FrontendOverhaul, FrameLocalBindingsAndUnreachableBranches) {
  SCOPED_TRACE("FE-OVERHAUL-04");
  constexpr std::string_view text =
      "(set-logic ALL)(declare-datatype Choice ((empty)))"
      "(define-fun kept ((outer Int)) Int"
      " (+ (! (let ((unused outer)) 7) :named closedUnused)"
      "    (let ((outer 3)) (let ((outer 4) (copy outer)) (+ outer copy)))"
      "    (! (let ((outer 8)) (let ((copy outer)) copy)) :named closedUsed)))"
      "(assert (= (let ((unused (+ true 1))) 0) 0))"
      "(assert (= (! 7 :named temporary)"
      "           (let ((outer 7)) (! outer :named sameNodeOpen))))"
      "(define-fun retry ((outer Int)) Int"
      " (! (match empty ((_ (! 1 :named staged)) (empty outer)))"
      "    :named wholeMatch))"
      "(assert (= staged 1))"
      "(define-fun retry ((outer Int)) Int"
      " (! (match empty ((_ (! 1 :named staged))"
      "                  (empty (! 9 :named unreachable)))) :named wholeMatch))"
      "(assert (= (retry 42) staged wholeMatch))(assert (= unreachable 9))"
      "(check-sat)";
  auto session = Require(Session::FromText(text, kSource));
  RequireNext(session, CommandKind::kSetLogic);
  RequireNext(session, CommandKind::kDeclareDatatype);
  RequireNext(session, CommandKind::kDefineFun);

  const auto& prefix = session.GetPrefix();
  ASSERT_EQ(prefix.GetDefinitions().size(), 1u);
  const auto body = prefix.GetDefinitions()[0].body.Borrow();
  ASSERT_EQ(body.GetKind(), Kind::kIntAdd);
  const auto children = body.GetChildren();
  ASSERT_EQ(children.size(), 3u);
  EXPECT_EQ(IntegerValue(children[0]), "7");
  EXPECT_EQ(IntegerValue(children[2]), "8");
  ASSERT_EQ(children[1].GetKind(), Kind::kIntAdd);
  const auto shadowed = children[1].GetChildren();
  ASSERT_EQ(shadowed.size(), 2u);
  EXPECT_EQ(IntegerValue(shadowed[0]), "4");
  EXPECT_EQ(IntegerValue(shadowed[1]), "3");

  ExpectError(Next(session), DiagnosticCode::kInvalidInput);
  const auto open_local = Next(session);
  ASSERT_FALSE(open_local);
  EXPECT_EQ(open_local.error().code, DiagnosticCode::kInvalidInput);
  const auto open_local_offset = text.find("(! outer :named sameNodeOpen)");
  ASSERT_NE(open_local_offset, std::string_view::npos);
  ExpectSpan(open_local.error().where, text, "outer", open_local_offset);
  const auto open_match = Next(session);
  ASSERT_FALSE(open_match);
  EXPECT_EQ(open_match.error().code, DiagnosticCode::kInvalidInput);
  ExpectSpan(open_match.error().where, text,
             "(match empty ((_ (! 1 :named staged)) (empty outer)))");
  ASSERT_EQ(prefix.GetNamedTerms().size(), 2u);
  ASSERT_EQ(prefix.GetAnnotations().size(), 2u);
  ASSERT_EQ(prefix.GetDefinitions().size(), 1u);
  EXPECT_TRUE(prefix.GetAssertions().empty());
  ExpectError(Next(session), DiagnosticCode::kInvalidInput);

  RequireNext(session, CommandKind::kDefineFun);
  RequireNext(session, CommandKind::kAssert);
  RequireNext(session, CommandKind::kAssert);
  const auto query = RequireCheck(session);
  const auto names = query->GetNamedTerms();
  constexpr std::array expected{"closedUnused", "closedUsed", "staged",
                                "unreachable", "wholeMatch"};
  ASSERT_EQ(names.size(), expected.size());
  ASSERT_EQ(query->GetAnnotations().size(), expected.size());
  for (std::size_t i = 0; i < names.size(); ++i) {
    EXPECT_EQ(names[i].name, expected[i]);
    EXPECT_EQ(query->GetAnnotations()[i].term, names[i].term);
  }
  ASSERT_EQ(query->GetDefinitions().size(), 2u);
  EXPECT_EQ(query->GetDefinitions()[1].body, names[2].term);
  EXPECT_EQ(names[2].term, names[4].term);
  EXPECT_EQ(IntegerValue(names[3].term.Borrow()), "9");
  ASSERT_EQ(query->GetAssertions().size(), 2u);
  ExpectChildren(query->GetAssertions()[1].term.Borrow(),
                 {names[3].term.Borrow(), names[3].term.Borrow()});
}

TEST(FrontendOverhaul, NestedAnnotationsVisitOnceAndPublishInsideOut) {
  SCOPED_TRACE("FE-OVERHAUL-04");
  constexpr std::size_t kDepth = 257;
  std::string text = "(set-logic ALL)(assert ";
  for (std::size_t i = 0; i < kDepth; ++i) text += "(! ";
  text += "true";
  for (std::size_t i = 0; i < kDepth; ++i)
    text += " :named nested" + std::to_string(i) + ")";
  text += ")(check-sat)";
  auto session = Require(Session::FromReader(Chunked(text, kIrregularChunks)));
  RequireNext(session, CommandKind::kSetLogic);
  RequireNext(session, CommandKind::kAssert);
  EXPECT_EQ(session.GetStats().closure_visits, kDepth);
  const auto query = RequireCheck(session);
  const auto names = query->GetNamedTerms();
  const auto annotations = query->GetAnnotations();
  const auto assertions = query->GetAssertions();
  ASSERT_EQ(names.size(), kDepth);
  ASSERT_EQ(annotations.size(), kDepth);
  ASSERT_EQ(assertions.size(), 1u);
  for (std::size_t i = 0; i < kDepth; ++i) {
    EXPECT_EQ(names[i].name, "nested" + std::to_string(i));
    EXPECT_EQ(names[i].term, assertions[0].term);
    EXPECT_EQ(annotations[i].term, assertions[0].term);
    if (i > 0) {
      EXPECT_LT(annotations[i].where.begin.offset,
                annotations[i - 1].where.begin.offset);
      EXPECT_GT(annotations[i].where.end.offset,
                annotations[i - 1].where.end.offset);
    }
  }
}

TEST(FrontendOverhaul, PooledRankViewsSurviveSourceAndSparseDeclarationIds) {
  SCOPED_TRACE("FE-OVERHAUL-05");
  static_assert(!std::same_as<decltype(UserFunction::source_rank),
                              std::vector<std::string>>,
                "UserFunction rank records borrow their spelling storage");
  std::shared_ptr<const QueryContext> query;
  {
    const std::string text =
        "(set-logic ALL)(declare-const first Bool)"
        "(define-fun identity ((x Int)) Int x)"
        "(declare-const middle Int)"
        "(define-fun failed ((x Int)) Int (+ x true))"
        "(declare-const afterFailure Int)"
        "(declare-datatype Choice ((choice (part Int))))"
        "(declare-fun |pooled name| (Int (_ BitVec 32) Int) (_ BitVec 32))"
        "(assert (= (|pooled name| middle (_ bv2 32) afterFailure)"
        "           (_ bv2 32)))"
        "(check-sat)";
    auto session =
        Require(Session::FromReader(Chunked(text, kIrregularChunks), kSource));
    RequireNext(session, CommandKind::kSetLogic);
    RequireNext(session, CommandKind::kDeclareConst);
    RequireNext(session, CommandKind::kDefineFun);
    RequireNext(session, CommandKind::kDeclareConst);
    ExpectError(Next(session), DiagnosticCode::kInvalidInput);
    RequireNext(session, CommandKind::kDeclareConst);
    RequireNext(session, CommandKind::kDeclareDatatype);
    const auto declaration = RequireNext(session, CommandKind::kDeclareFun);
    ASSERT_EQ(session.GetPrefix().GetFunctions().size(), 4u);
    ExpectSpan(session.GetPrefix().GetFunctions()[3].where, text,
               "(declare-fun |pooled name| (Int (_ BitVec 32) Int)"
               " (_ BitVec 32))");
    EXPECT_EQ(session.GetPrefix().GetFunctions()[3].where,
              declaration.header.where);
    RequireNext(session, CommandKind::kAssert);
    query = RequireCheck(session);
  }

  const auto functions = query->GetFunctions();
  ASSERT_EQ(functions.size(), 4u);
  const auto& function = functions[3];
  EXPECT_EQ(function.source_name, std::string_view("|pooled name|"));
  EXPECT_EQ(function.source_name, std::string("|pooled name|"));
  const std::vector<std::string> rank{"Int", "(_ BitVec 32)", "Int",
                                      "(_ BitVec 32)"};
  EXPECT_EQ(function.source_rank, rank);
  ASSERT_EQ(function.source_rank.size(), rank.size());
  EXPECT_EQ(function.source_rank.back(), rank.back());
  for (std::size_t i = 0; i < rank.size(); ++i)
    EXPECT_EQ(function.source_rank[i], rank[i]);
  std::size_t i = 0;
  for (const auto& spelling : function.source_rank) {
    ASSERT_LT(i, rank.size());
    EXPECT_EQ(spelling, rank[i++]);
  }
  EXPECT_EQ(i, rank.size());
  EXPECT_EQ(functions[0].source_rank.back(), "Bool");
  for (const auto& declared : functions)
    EXPECT_TRUE(query->HasFunction(declared.decl.Borrow()));
  for (std::size_t index = 1; index < functions.size(); ++index)
    EXPECT_GT(functions[index].decl.GetId(),
              functions[index - 1].decl.GetId() + 1)
        << index;
  ASSERT_EQ(query->GetDefinitions().size(), 1u);
  EXPECT_FALSE(query->HasFunction(query->GetDefinitions()[0].decl.Borrow()));
  ASSERT_EQ(query->GetDefinitions()[0].formals.size(), 1u);
  EXPECT_FALSE(
      query->HasFunction(query->GetDefinitions()[0].formals[0].Borrow()));
  ASSERT_EQ(query->GetDatatypeGroups().size(), 1u);
  const auto group = query->GetDatatypeGroups()[0].Borrow();
  EXPECT_FALSE(query->HasFunction(Require(group.GetConstructor(0, 0))));
  EXPECT_FALSE(query->HasFunction(Require(group.GetSelector(0, 0, 0))));
  EXPECT_FALSE(query->HasFunction(Require(group.GetTester(0, 0))));
  EXPECT_FALSE(query->HasFunction(DeclView{}));
  const auto other = RequireQuery(
      Parse("(set-logic ALL)(declare-const first Bool)(check-sat)"));
  ASSERT_EQ(other->GetFunctions().size(), 1u);
  const auto foreign = other->GetFunctions()[0].decl.Borrow();
  EXPECT_EQ(foreign.GetId(), functions[0].decl.GetId());
  EXPECT_FALSE(query->HasFunction(foreign));

  ASSERT_EQ(query->GetAssertions().size(), 1u);
  const auto equality = query->GetAssertions()[0].term.Borrow();
  ASSERT_EQ(equality.GetKind(), Kind::kEqual);
  ASSERT_EQ(equality.GetChildren().size(), 2u);
  EXPECT_EQ(Applied(equality.GetChildren()[0]), function.decl.Borrow());
}

TEST(FrontendOverhaul, SpellingAndLiteralIdentitiesSurviveHashCollisions) {
  SCOPED_TRACE("FE-OVERHAUL-05");
  constexpr std::string_view text =
      "(set-option :produce-models true)(set-logic ALL)"
      "(declare-const collisionA Int)(declare-const |collisionB| Int)"
      "(declare-const |collisionA| Int)(check-sat)"
      "(get-value (collisionA |collisionA| collisionB |collisionB|"
      " 41041 41041 41042 #b1111 #xf (_ bv15 4) #x0f (_ bv15 8)))";
  auto session = Require(Session::FromText(text));
  auto manager = Require(NodeManager::Attach(session.GetPrefix().GetManager()));
  NodeTestPeer::ForceCollisions(manager, 7);
  RequireNext(session, CommandKind::kSetOption);
  RequireNext(session, CommandKind::kSetLogic);
  RequireNext(session, CommandKind::kDeclareConst);
  RequireNext(session, CommandKind::kDeclareConst);
  ExpectError(Next(session), DiagnosticCode::kInvalidInput);
  const auto query = RequireCheck(session);
  const auto command = RequireNext(session, CommandKind::kGetValue);
  const auto& values = command.values;
  ASSERT_EQ(values.size(), 12u);
  ASSERT_EQ(query->GetFunctions().size(), 2u);
  const auto& first = query->GetFunctions()[0];
  const auto& second = query->GetFunctions()[1];
  EXPECT_EQ(first.source_name, std::string_view("collisionA"));
  EXPECT_EQ(second.source_name, std::string_view("|collisionB|"));
  EXPECT_EQ(Applied(values[0].term.Borrow()), first.decl.Borrow());
  EXPECT_EQ(Applied(values[2].term.Borrow()), second.decl.Borrow());
  EXPECT_EQ(values[0].term, values[1].term);
  EXPECT_EQ(values[2].term, values[3].term);
  EXPECT_NE(values[0].term, values[2].term);
  EXPECT_EQ(NodeTestPeer::StructuralHash(values[0].term),
            NodeTestPeer::StructuralHash(values[2].term));
  EXPECT_EQ(IntegerValue(values[4].term.Borrow()), "41041");
  EXPECT_EQ(IntegerValue(values[6].term.Borrow()), "41042");
  EXPECT_EQ(values[4].term, values[5].term);
  EXPECT_NE(values[4].term, values[6].term);
  EXPECT_EQ(values[7].term, values[8].term);
  EXPECT_EQ(values[7].term, values[9].term);
  EXPECT_EQ(values[10].term, values[11].term);
  EXPECT_NE(values[7].term, values[10].term);
  EXPECT_EQ(values[7].term.Borrow().GetSort().GetWidth(), 4u);
  EXPECT_EQ(values[10].term.Borrow().GetSort().GetWidth(), 8u);
  EXPECT_GT(session.GetStats().literal_cache_hits, 0u);
}

TEST(FrontendOverhaul, LiteralCacheHitsMissesAndOversizedBypass) {
  SCOPED_TRACE("FE-OVERHAUL-06");
  const auto oversized = "1" + std::string(16383, '2');
  const auto text = std::string(kProduceModels) + std::string(kLogic) +
                    "(check-sat)(get-value (41041 41042))"
                    "(get-value (51051 51051 " +
                    oversized + " " + oversized + "))";
  auto session = Require(Session::FromText(text));
  RequireNext(session, CommandKind::kSetOption);
  RequireNext(session, CommandKind::kSetLogic);
  RequireCheck(session);
  const auto before = session.GetStats().literal_cache_hits;
  const auto cold = RequireNext(session, CommandKind::kGetValue);
  EXPECT_EQ(session.GetStats().literal_cache_hits, before);
  ASSERT_EQ(cold.values.size(), 2u);
  EXPECT_EQ(IntegerValue(cold.values[0].term.Borrow()), "41041");
  EXPECT_EQ(IntegerValue(cold.values[1].term.Borrow()), "41042");

  const auto warm = RequireNext(session, CommandKind::kGetValue);
  EXPECT_EQ(session.GetStats().literal_cache_hits - before, 1u);
  ASSERT_EQ(warm.values.size(), 4u);
  EXPECT_EQ(IntegerValue(warm.values[0].term.Borrow()), "51051");
  EXPECT_EQ(warm.values[0].term, warm.values[1].term);
  EXPECT_EQ(IntegerValue(warm.values[2].term.Borrow()), oversized);
  EXPECT_EQ(warm.values[2].term, warm.values[3].term);
  EXPECT_NE(warm.values[2].where, warm.values[3].where);
}

TEST(FrontendOverhaul, LiteralCacheEvictsUnderDistinctKeyPressure) {
  SCOPED_TRACE("FE-OVERHAUL-06");
  constexpr std::size_t kKeys = 8192;
  std::string pairs;
  std::string replay;
  for (std::size_t i = 0; i < kKeys; ++i) {
    const auto key = " " + std::to_string(10000 + i);
    pairs += key + key;
    replay += key;
  }
  const auto read = [&](std::string_view tail, std::size_t& hits) {
    const auto text = std::string(kProduceModels) + std::string(kLogic) +
                      "(check-sat)(get-value (" + pairs + std::string(tail) +
                      "))";
    auto session = Require(Session::FromText(text));
    RequireNext(session, CommandKind::kSetOption);
    RequireNext(session, CommandKind::kSetLogic);
    RequireCheck(session);
    const auto before = session.GetStats().literal_cache_hits;
    auto values = RequireNext(session, CommandKind::kGetValue);
    hits = session.GetStats().literal_cache_hits - before;
    return values;
  };
  std::size_t warm_hits = 0;
  read("", warm_hits);
  EXPECT_EQ(warm_hits, kKeys);
  std::size_t total_hits = 0;
  const auto values = read(replay, total_hits);
  ASSERT_GE(total_hits, warm_hits);
  EXPECT_LT(total_hits - warm_hits, kKeys);
  ASSERT_EQ(values.values.size(), 3 * kKeys);
  for (std::size_t i = 0; i < kKeys; ++i) {
    EXPECT_EQ(IntegerValue(values.values[2 * i].term.Borrow()),
              std::to_string(10000 + i));
    EXPECT_EQ(values.values[2 * i].term, values.values[2 * kKeys + i].term);
  }
}

TEST(FrontendOverhaul, ConcreteSortCachePreservesRankAndExactSorts) {
  SCOPED_TRACE("FE-OVERHAUL-06");
  constexpr std::string_view text =
      "(set-logic ALL)"
      "(declare-fun cached ((_ BitVec 32) (_ FloatingPoint 8 24)"
      " (_ BitVec 32) (_ FloatingPoint 8 24) (_ BitVec 33)) (_ BitVec 32))"
      "(declare-const wrong (_ BitVec 0))"
      "(declare-const wrong (_ BitVec 32))(check-sat)";
  auto session = Require(Session::FromText(text, kSource));
  RequireNext(session, CommandKind::kSetLogic);
  const auto before = session.GetStats().sort_cache_hits;
  RequireNext(session, CommandKind::kDeclareFun);
  EXPECT_GT(session.GetStats().sort_cache_hits, before);
  const auto wrong = Next(session);
  ASSERT_FALSE(wrong);
  EXPECT_EQ(wrong.error().code, DiagnosticCode::kInvalidInput);
  ASSERT_EQ(session.GetPrefix().GetFunctions().size(), 1u);
  RequireNext(session, CommandKind::kDeclareConst);
  const auto query = RequireCheck(session);
  const auto functions = query->GetFunctions();
  ASSERT_EQ(functions.size(), 2u);
  EXPECT_EQ(functions[0].source_rank,
            (std::vector<std::string>{"(_ BitVec 32)", "(_ FloatingPoint 8 24)",
                                      "(_ BitVec 32)", "(_ FloatingPoint 8 24)",
                                      "(_ BitVec 33)", "(_ BitVec 32)"}));
  const auto declaration = functions[0].decl.Borrow();
  const auto domain = declaration.GetDomain();
  ASSERT_EQ(domain.size(), 5u);
  EXPECT_EQ(domain[0].GetWidth(), 32u);
  EXPECT_EQ(domain[4].GetWidth(), 33u);
  EXPECT_NE(domain[0], domain[4]);
  EXPECT_EQ(domain[0], domain[2]);
  EXPECT_EQ(domain[1], domain[3]);
  EXPECT_EQ(domain[1].GetFpExponentWidth(), 8u);
  EXPECT_EQ(domain[1].GetFpSignificandWidth(), 24u);
  EXPECT_EQ(declaration.GetRange(), domain[0]);
  EXPECT_EQ(functions[1].decl.Borrow().GetRange(), domain[0]);
}

TEST(FrontendOverhaul, ConcreteSortCacheEvictsUnderDistinctKeyPressure) {
  SCOPED_TRACE("FE-OVERHAUL-06");
  constexpr std::size_t kKeys = 8192;
  std::string pairs;
  std::string replay;
  for (std::size_t width = 1; width <= kKeys; ++width) {
    const auto key = " (_ BitVec " + std::to_string(width) + ")";
    pairs += key + key;
    replay += key;
  }
  const auto read = [&](std::string_view tail, std::size_t& hits) {
    const auto text = "(set-logic ALL)(declare-fun wide (" + pairs +
                      std::string(tail) + ") Bool)(check-sat)";
    auto session = Require(Session::FromText(text));
    RequireNext(session, CommandKind::kSetLogic);
    const auto before = session.GetStats().sort_cache_hits;
    RequireNext(session, CommandKind::kDeclareFun);
    hits = session.GetStats().sort_cache_hits - before;
    return RequireCheck(session);
  };
  std::size_t warm_hits = 0;
  read("", warm_hits);
  EXPECT_EQ(warm_hits, kKeys);
  std::size_t total_hits = 0;
  const auto query = read(replay, total_hits);
  ASSERT_GE(total_hits, warm_hits);
  EXPECT_LT(total_hits - warm_hits, kKeys);
  ASSERT_EQ(query->GetFunctions().size(), 1u);
  const auto domain = query->GetFunctions()[0].decl.Borrow().GetDomain();
  ASSERT_EQ(domain.size(), 3 * kKeys);
  for (std::size_t i = 0; i < kKeys; ++i) {
    EXPECT_EQ(domain[2 * i].GetWidth(), i + 1);
    EXPECT_EQ(domain[2 * i], domain[2 * kKeys + i]);
  }
}

TEST(FrontendOverhaul, AllocationFailureRollsBackCachedCommandsAndAllowsRetry) {
  SCOPED_TRACE("FE-OVERHAUL-07");
  constexpr std::string_view prefix =
      "(set-logic ALL)(declare-const prior Bool)(assert (! prior :named keep))";
  const auto sweep = [&](std::string_view target, CommandKind kind,
                         auto inspect) {
    SCOPED_TRACE(target);
    const auto text =
        std::string(prefix) + std::string(target) + std::string(target);
    constexpr std::size_t kMaxCheckpoints = 128;
    for (std::size_t checkpoint = 0; checkpoint < kMaxCheckpoints;
         ++checkpoint) {
      SCOPED_TRACE(checkpoint);
      auto session = Require(Session::FromText(text));
      RequireNext(session, CommandKind::kSetLogic);
      RequireNext(session, CommandKind::kDeclareConst);
      RequireNext(session, CommandKind::kAssert);
      const auto prior_id = session.GetPrefix().GetAssertions()[0].term.GetId();
      const auto header = Require(session.NextSyntax());
      ASSERT_TRUE(header);
      ASSERT_EQ(header->kind, kind);
      session.FailAllocationAfter(checkpoint);
      const auto attempted = session.Apply();
      if (attempted) {
        EXPECT_GT(checkpoint, 0u);
        inspect(session.GetPrefix());
        return;
      }

      EXPECT_EQ(attempted.error().code, DiagnosticCode::kResource);
      ASSERT_TRUE(attempted.error().node_error);
      EXPECT_EQ(attempted.error().node_error->resource,
                ResourceKind::kCppAllocation);
      const auto& committed = session.GetPrefix();
      ASSERT_EQ(committed.GetFunctions().size(), 1u);
      EXPECT_EQ(committed.GetFunctions()[0].source_name, std::string("prior"));
      EXPECT_TRUE(
          committed.HasFunction(committed.GetFunctions()[0].decl.Borrow()));
      ASSERT_EQ(committed.GetAssertions().size(), 1u);
      ASSERT_EQ(committed.GetNamedTerms().size(), 1u);
      ASSERT_EQ(committed.GetAnnotations().size(), 1u);
      EXPECT_EQ(committed.GetNamedTerms()[0].name, "keep");
      EXPECT_EQ(committed.GetAssertions()[0].term.GetId(), prior_id);
      EXPECT_EQ(committed.GetNamedTerms()[0].term.GetId(), prior_id);
      EXPECT_EQ(committed.GetAnnotations()[0].term.GetId(), prior_id);
      RequireNext(session, kind);
      inspect(session.GetPrefix());
      EXPECT_FALSE(Require(session.NextSyntax()));
    }
    ADD_FAILURE() << "The allocation checkpoint sweep never reached success";
  };

  sweep("(declare-fun |new rank| ((_ BitVec 32) (_ BitVec 32)) (_ BitVec 32))",
        CommandKind::kDeclareFun, [](const QueryContext& query) {
          const auto functions = query.GetFunctions();
          ASSERT_EQ(functions.size(), 2u);
          EXPECT_EQ(functions[1].source_name, std::string_view("|new rank|"));
          EXPECT_EQ(functions[1].source_rank,
                    (std::vector<std::string>{"(_ BitVec 32)", "(_ BitVec 32)",
                                              "(_ BitVec 32)"}));
          const auto declaration = functions[1].decl.Borrow();
          EXPECT_TRUE(query.HasFunction(declaration));
          ASSERT_EQ(declaration.GetDomain().size(), 2u);
          EXPECT_EQ(declaration.GetDomain()[0], declaration.GetDomain()[1]);
          EXPECT_EQ(declaration.GetDomain()[0].GetWidth(), 32u);
          EXPECT_EQ(declaration.GetRange(), declaration.GetDomain()[0]);
          EXPECT_EQ(query.GetAssertions().size(), 1u);
        });

  sweep("(assert (and (! (= 7447 7447) :named fresh) (= 7448 7448)))",
        CommandKind::kAssert, [](const QueryContext& query) {
          ASSERT_EQ(query.GetFunctions().size(), 1u);
          ASSERT_EQ(query.GetAssertions().size(), 2u);
          ASSERT_EQ(query.GetNamedTerms().size(), 2u);
          ASSERT_EQ(query.GetAnnotations().size(), 2u);
          EXPECT_EQ(query.GetNamedTerms()[1].name, "fresh");
          const auto root = query.GetAssertions()[1].term.Borrow();
          ASSERT_EQ(root.GetKind(), Kind::kAnd);
          ASSERT_EQ(root.GetChildren().size(), 2u);
          const auto first = root.GetChildren()[0];
          const auto second = root.GetChildren()[1];
          EXPECT_EQ(first, query.GetNamedTerms()[1].term.Borrow());
          EXPECT_EQ(first, query.GetAnnotations()[1].term.Borrow());
          for (const auto& [node, literal] :
               {std::pair{first, "7447"}, {second, "7448"}}) {
            ASSERT_EQ(node.GetKind(), Kind::kEqual);
            ASSERT_EQ(node.GetChildren().size(), 2u);
            EXPECT_EQ(node.GetChildren()[0], node.GetChildren()[1]);
            EXPECT_EQ(IntegerValue(node.GetChildren()[0]), literal);
          }
        });
}

TEST(FrontendOverhaul, BatchTeardownPreservesExternalOwnersAndDetachesTable) {
  SCOPED_TRACE("FE-OVERHAUL-08");
  std::string text =
      "(set-logic ALL)(define-const frozen Bool (! (not false) :named seed))"
      "(assert (! seed :named alias))(assert alias)(assert ";
  constexpr std::size_t kDepth = 256;
  for (std::size_t i = 0; i < kDepth; ++i) text += "(not ";
  text += "true" + std::string(kDepth + 1, ')') + "(check-sat)";

  enum class Retained { kNeither, kRoot, kChild, kBoth };
  for (const auto retained : {Retained::kNeither, Retained::kRoot,
                              Retained::kChild, Retained::kBoth}) {
    SCOPED_TRACE(static_cast<int>(retained));
    const auto keep_root =
        retained == Retained::kRoot || retained == Retained::kBoth;
    const auto keep_child =
        retained == Retained::kChild || retained == Retained::kBoth;
    auto query = RequireQuery(Parse(text));
    auto manager = Require(NodeManager::Attach(query->GetManager()));
    ASSERT_EQ(query->GetDefinitions().size(), 1u);
    ASSERT_EQ(query->GetNamedTerms().size(), 2u);
    ASSERT_EQ(query->GetAnnotations().size(), 2u);
    ASSERT_EQ(query->GetAssertions().size(), 3u);
    const auto& seed = query->GetDefinitions()[0].body;
    EXPECT_EQ(query->GetAssertions()[0].term, seed);
    EXPECT_EQ(query->GetAssertions()[1].term, seed);
    for (const auto& named : query->GetNamedTerms())
      EXPECT_EQ(named.term, seed);
    for (const auto& annotation : query->GetAnnotations())
      EXPECT_EQ(annotation.term, seed);

    const auto root_id = seed.GetId();
    const auto child_id = Argument(seed, 0).GetId();
    auto root = keep_root ? seed : Node{};
    auto child = keep_child ? Require(seed.Child(0)) : Node{};
    const auto before = NodeTestPeer::GetStats(manager);
    EXPECT_EQ(before.live_nodes, kDepth + 3);
    ASSERT_GT(before.table_capacity, 0u);
    query.reset();

    const auto after = NodeTestPeer::GetStats(manager);
    const std::uint64_t expected_survivors = keep_root ? 2 : keep_child ? 1 : 0;
    EXPECT_EQ(after.live_nodes, expected_survivors);
    EXPECT_EQ(after.node_reclamations - before.node_reclamations,
              before.live_nodes - expected_survivors);
    EXPECT_LT(after.table_capacity, before.table_capacity);
    if (keep_root) {
      EXPECT_EQ(root.GetId(), root_id);
      EXPECT_EQ(NodeTestPeer::RefCount(root), 1u);
      const auto view = root.Borrow();
      ASSERT_EQ(view.GetKind(), Kind::kNot);
      ASSERT_EQ(view.GetChildren().size(), 1u);
      EXPECT_EQ(view.GetChildren()[0].GetId(), child_id);
    }
    if (keep_child) {
      EXPECT_EQ(child.GetId(), child_id);
      EXPECT_EQ(NodeTestPeer::RefCount(child), keep_root ? 2u : 1u);
      EXPECT_EQ(child.Borrow().GetValue()->GetBool(), false);
      EXPECT_EQ(Require(manager.MakeBool(false)), child);
    }
    if (!keep_root && !keep_child) {
      EXPECT_EQ(after.table_capacity, 0u);
      EXPECT_GT(Require(manager.MakeBool(false)).GetId(), child_id);
    }
  }
}

TypedCommand ReadValues(Session& session) {
  for (;;) {
    auto command = Require(Next(session));
    if (!command) throw std::runtime_error("Expected a value request");
    if (command->header.kind == CommandKind::kGetValue)
      return std::move(*command);
  }
}

enum class Permutation { kKeep, kAll, kFpOperands };

struct OperatorCase {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  Kind kind;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::string_view name;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::string_view first;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::string_view second;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  Permutation permutation;
};

using enum Kind;
using enum Permutation;
constexpr OperatorCase kOperatorCases[] = {
    {kAnd, "and", "r p q", "p q r", kAll},
    {kOr, "or", "r p q", "q r p", kAll},
    {kXor, "xor", "r p q", "p q r", kAll},
    {kEqual, "=", "c a b", "a b c", kAll},
    {kDistinct, "distinct", "c a b", "b c a", kAll},
    {kIntAdd, "+", "b a", "a b", kAll},
    {kIntAdd, "+", "c b a", "b a c", kAll},
    {kIntMul, "*", "c a b", "b c a", kAll},
    {kBvAnd, "bvand", "w u v", "u v w", kAll},
    {kBvOr, "bvor", "w u v", "u v w", kAll},
    {kBvXor, "bvxor", "w u v", "u v w", kAll},
    {kBvAdd, "bvadd", "w u v", "u v w", kAll},
    {kBvMul, "bvmul", "w u v", "u v w", kAll},
    {kBvAdd, "bvadd", "v u", "u v", kAll},
    {kFpEq, "fp.eq", "z x y", "x y z", kAll},
    {kBvNand, "bvnand", "v u", "u v", kAll},
    {kBvNor, "bvnor", "v u", "u v", kAll},
    {kBvXnor, "bvxnor", "v u", "u v", kAll},
    {kBvComp, "bvcomp", "v u", "u v", kAll},
    {kBvUaddo, "bvuaddo", "v u", "u v", kAll},
    {kBvSaddo, "bvsaddo", "v u", "u v", kAll},
    {kBvUmulo, "bvumulo", "v u", "u v", kAll},
    {kBvSmulo, "bvsmulo", "v u", "u v", kAll},
    {kFpAdd, "fp.add", "RNE y x", "RNE x y", kFpOperands},
    {kFpMul, "fp.mul", "RNE y x", "RNE x y", kFpOperands},
    {kFpFma, "fp.fma", "RNE y x z", "RNE x y z", kFpOperands},
    {kFpFma, "fp.fma", "RNE y x z", "RTZ x y z", kFpOperands},
    {kFpFma, "fp.fma", "RNE y x z", "RNE z x y", kFpOperands},
    {kFpMin, "fp.min", "x y", "y x", kKeep},
    {kFpMax, "fp.max", "x y", "y x", kKeep},
    {kIntSub, "-", "b a", "a b", kKeep},
    {kBvSub, "bvsub", "v u", "u v", kKeep},
    {kBvConcat, "concat", "v u", "u v", kKeep},
    {kImplies, "=>", "q p", "p q", kKeep},
    {kIntLt, "<", "b a", "a b", kKeep},
    {kFpLt, "fp.lt", "y x", "x y", kKeep},
    {kApply, "f", "b a", "a b", kKeep},
    {kIte, "ite", "p b a", "p a b", kKeep},
    {kArraySelect, "select", "array b", "array a", kKeep},
    {kArrayStore, "store", "array b a", "array a b", kKeep},
};
constexpr std::string_view kOperatorSetup =
    "(set-option :produce-models true)(set-logic ALL)"
    "(declare-const p Bool)(declare-const q Bool)(declare-const r Bool)"
    "(declare-const a Int)(declare-const b Int)(declare-const c Int)"
    "(declare-const u (_ BitVec 8))(declare-const v (_ BitVec 8))"
    "(declare-const w (_ BitVec 8))"
    "(declare-const x Float32)(declare-const y Float32)"
    "(declare-const z Float32)(declare-const array (Array Int Int))"
    "(declare-fun f (Int Int) Int)(check-sat)";
constexpr std::array<std::string_view, 15> kOperands{
    "p", "q", "r", "a", "b",   "c",   "u",    "v",
    "w", "x", "y", "z", "RNE", "RTZ", "array"};

using Operands = std::map<std::string_view, NodeView>;

std::vector<NodeView> ExpectedChildren(std::string_view arguments,
                                       const Operands& operands,
                                       Permutation permutation) {
  std::vector<NodeView> expected;
  for (const auto spelling : std::views::split(arguments, ' ')) {
    expected.push_back(operands.at(std::string_view(spelling)));
  }
  if (permutation == kAll) std::ranges::sort(expected, {}, &NodeView::GetId);
  if (permutation == kFpOperands)
    std::ranges::sort(expected.begin() + 1, expected.begin() + 3, {},
                      &NodeView::GetId);
  return expected;
}

void ExpectOrder(NodeView node, Kind kind, std::span<const NodeView> expected) {
  EXPECT_EQ(node.GetKind(), kind);
  const auto children = node.GetChildren();
  ASSERT_EQ(children.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i)
    EXPECT_EQ(children[i], expected[i]) << "child " << i;
}

std::string Application(std::string_view name, std::string_view arguments) {
  return "(" + std::string(name) + " " + std::string(arguments) + ")";
}

TEST(FrontendOverhaul, CanonicalizesExactlyTheAllowedOperandPositions) {
  SCOPED_TRACE("FE-OVERHAUL-09");
  std::string text = std::string(kOperatorSetup) + "(get-value (";
  for (const auto operand : kOperands) text += std::string(operand) + " ";
  for (const auto& entry : kOperatorCases) {
    text += Application(entry.name, entry.first);
    text += Application(entry.name, entry.second);
  }
  text += "))";

  for (const bool parse_enabled : {false, true}) {
    SCOPED_TRACE(parse_enabled);
    auto session = Require(Session::FromText(
        text, ParseOptions{.canonicalize_commutative = parse_enabled},
        kSource));
    const auto typed = ReadValues(session);
    ASSERT_EQ(typed.values.size(),
              kOperands.size() + 2 * std::size(kOperatorCases));
    Operands operands;
    for (std::size_t i = 0; i < kOperands.size(); ++i)
      operands.emplace(kOperands[i], typed.values[i].term.Borrow());
    std::vector<Node> original;
    for (const auto& request :
         std::span(typed.values).subspan(kOperands.size()))
      original.push_back(request.term);

    const auto& manager = session.GetPrefix().GetManager();
    const auto normalized =
        parse_enabled ? original
                      : Require(CanonicalizeCommutative(manager, original));
    ASSERT_EQ(normalized.size(), original.size());
    for (std::size_t i = 0; i < std::size(kOperatorCases); ++i) {
      const auto& entry = kOperatorCases[i];
      SCOPED_TRACE(entry.name);
      const auto first =
          ExpectedChildren(entry.first, operands, entry.permutation);
      const auto second =
          ExpectedChildren(entry.second, operands, entry.permutation);
      ExpectOrder(normalized[2 * i].Borrow(), entry.kind, first);
      ExpectOrder(normalized[2 * i + 1].Borrow(), entry.kind, second);
      const bool same = normalized[2 * i] == normalized[2 * i + 1];
      EXPECT_EQ(same, first == second);
      EXPECT_EQ(typed.values[kOperands.size() + 2 * i].input,
                Application(entry.name, entry.first));
      EXPECT_EQ(typed.values[kOperands.size() + 2 * i + 1].input,
                Application(entry.name, entry.second));
      if (!parse_enabled) {
        ExpectOrder(original[2 * i].Borrow(), entry.kind,
                    ExpectedChildren(entry.first, operands, kKeep));
        ExpectOrder(original[2 * i + 1].Borrow(), entry.kind,
                    ExpectedChildren(entry.second, operands, kKeep));
        EXPECT_NE(original[2 * i], original[2 * i + 1]);
      }
    }
    if (parse_enabled) {
      EXPECT_GT(session.GetStats().commutative_canonicalizations, 0u);
    } else {
      EXPECT_EQ(session.GetStats().commutative_canonicalizations, 0u);
    }
  }
}

TEST(FrontendOverhaul, FloatingMinAndMaxPreserveOppositeZeroOrder) {
  SCOPED_TRACE("FE-OVERHAUL-09");
  constexpr std::string_view text =
      "(set-option :produce-models true)(set-logic ALL)(check-sat)"
      "(get-value ((fp.min (_ +zero 8 24) (_ -zero 8 24))"
      "            (fp.min (_ -zero 8 24) (_ +zero 8 24))"
      "            (fp.max (_ +zero 8 24) (_ -zero 8 24))"
      "            (fp.max (_ -zero 8 24) (_ +zero 8 24))))";
  for (const bool parse_enabled : {false, true}) {
    SCOPED_TRACE(parse_enabled);
    auto session = Require(Session::FromText(
        text, ParseOptions{.canonicalize_commutative = parse_enabled}));
    const auto typed = ReadValues(session);
    ASSERT_EQ(typed.values.size(), 4u);
    for (std::size_t i = 0; i < typed.values.size(); ++i) {
      const auto& term = typed.values[i].term;
      auto result = parse_enabled
                        ? term
                        : Require(CanonicalizeCommutative(
                              session.GetPrefix().GetManager(), term.Borrow()));
      EXPECT_EQ(result, term);
      const auto view = result.Borrow();
      EXPECT_EQ(view.GetKind(), i < 2 ? kFpMin : kFpMax);
      const auto children = view.GetChildren();
      ASSERT_EQ(children.size(), 2u);
      for (std::size_t j = 0; j < children.size(); ++j) {
        ASSERT_EQ(children[j].GetKind(), kFpValue);
        EXPECT_EQ(children[j].GetValue()->GetFpCategory(),
                  (i + j) % 2 == 0 ? FpCategory::kPositiveZero
                                   : FpCategory::kNegativeZero);
      }
    }
    EXPECT_NE(typed.values[0].term, typed.values[1].term);
    EXPECT_NE(typed.values[2].term, typed.values[3].term);
  }
}

TEST(FrontendOverhaul, LegacyAndExplicitDefaultsKeepOperandOrder) {
  SCOPED_TRACE("FE-OVERHAUL-10");
  const auto text =
      std::string(kOperatorSetup) + "(get-value (a b (+ b a) (+ a b)))";
  const auto check = [](Session session) {
    const auto values = ReadValues(session);
    ASSERT_EQ(values.values.size(), 4u);
    const auto a = values.values[0].term.Borrow();
    const auto b = values.values[1].term.Borrow();
    ExpectChildren(values.values[2].term.Borrow(), {b, a});
    ExpectChildren(values.values[3].term.Borrow(), {a, b});
    EXPECT_NE(values.values[2].term, values.values[3].term);
    EXPECT_EQ(session.GetStats().commutative_canonicalizations, 0u);
  };
  check(Require(Session::FromText(text)));
  check(Require(Session::FromText(text, ParseOptions{})));
  check(Require(Session::FromReader(Chunked(text, kIrregularChunks))));
  check(Require(
      Session::FromReader(Chunked(text, kIrregularChunks), ParseOptions{})));
}

TEST(FrontendOverhaul, BottomUpMergesReachOrderedParentsAndKeepShape) {
  SCOPED_TRACE("FE-OVERHAUL-10");
  const auto text = std::string(kOperatorSetup) +
                    "(get-value (a b q u v"
                    " (+ b a) (+ a b)"
                    " (ite p (+ b a) a) (ite p (+ a b) a)"
                    " (and q (= (+ b a) a)) (and (= (+ a b) a) q)"
                    " (f (ite p (+ b a) a) b) (f (ite p (+ a b) a) b)"
                    " (+ b a a) (+ a b a) (+ (+ b a) a)"
                    " ((_ rotate_left 18446744073709551617) (bvadd v u))"
                    " ((_ rotate_left 18446744073709551617) (bvadd u v))))";

  for (const bool parse_enabled : {false, true}) {
    SCOPED_TRACE(parse_enabled);
    auto session = Require(Session::FromText(
        text, ParseOptions{.canonicalize_commutative = parse_enabled}));
    const auto command = ReadValues(session);
    ASSERT_EQ(command.values.size(), 18u);
    std::vector<Node> originals;
    for (const auto& request : command.values)
      originals.push_back(request.term);
    const auto& manager = session.GetPrefix().GetManager();
    const auto results =
        parse_enabled ? originals
                      : Require(CanonicalizeCommutative(manager, originals));
    ASSERT_EQ(results.size(), originals.size());
    constexpr std::array<std::pair<std::size_t, std::size_t>, 6> pairs{
        {{5, 6}, {7, 8}, {9, 10}, {11, 12}, {13, 14}, {16, 17}}};
    for (const auto& [first, second] : pairs) {
      EXPECT_EQ(results[first], results[second]) << first;
      if (!parse_enabled) {
        EXPECT_NE(originals[first], originals[second]);
      }
    }
    for (std::size_t i = 0; i < 5; ++i) EXPECT_EQ(results[i], originals[i]);
    if (!parse_enabled) {
      EXPECT_EQ(
          Require(CanonicalizeCommutative(manager, originals[11].Borrow())),
          results[11]);
      EXPECT_TRUE(
          Require(CanonicalizeCommutative(manager, std::span<const Node>{}))
              .empty());
    }

    const auto view = [&](std::size_t i) { return results[i].Borrow(); };
    ASSERT_LT(view(0).GetId(), view(1).GetId());
    ExpectOrder(view(5), kIntAdd, std::array{view(0), view(1)});
    ASSERT_EQ(view(7).GetKind(), kIte);
    ASSERT_EQ(view(7).GetChildren().size(), 3u);
    EXPECT_EQ(view(7).GetChildren()[1], view(5));
    ASSERT_EQ(view(9).GetKind(), kAnd);
    ASSERT_EQ(view(9).GetChildren().size(), 2u);
    const auto conjunction = view(9).GetChildren();
    EXPECT_LT(conjunction[0].GetId(), conjunction[1].GetId());
    EXPECT_EQ(conjunction[0], view(2));
    ASSERT_EQ(conjunction[1].GetKind(), kEqual);
    ASSERT_EQ(conjunction[1].GetChildren().size(), 2u);
    EXPECT_EQ(conjunction[1].GetChildren()[1], view(5));
    ExpectOrder(view(11), kApply, std::array{view(7), view(1)});
    EXPECT_EQ(Applied(view(11)), Applied(originals[11].Borrow()));
    ExpectOrder(view(13), kIntAdd, std::array{view(0), view(0), view(1)});
    ExpectOrder(view(15), kIntAdd, std::array{view(0), view(5)});
    EXPECT_NE(view(13), view(15));
    const auto rotated = view(16);
    ASSERT_EQ(rotated.GetKind(), kBvRotateLeft);
    ASSERT_EQ(rotated.GetIndices().size(), 1u);
    EXPECT_EQ(rotated.GetIndices()[0].ToDecimal(), "18446744073709551617");
    ASSERT_EQ(rotated.GetChildren().size(), 1u);
    ExpectOrder(rotated.GetChildren()[0], kBvAdd, std::array{view(3), view(4)});
  }
}

TEST(FrontendOverhaul, EnabledParsingPublishesCanonicalQueryTerms) {
  SCOPED_TRACE("FE-OVERHAUL-10");
  constexpr std::string_view text =
      "(set-logic ALL)(declare-const a Int)(declare-const b Int)"
      "(define-const left Int (! (+ b a) :named sumLeft))"
      "(define-const right Int (! (+ a b) :named sumRight))"
      "(assert (= (+ true a) 3))"
      "(assert (! (= sumLeft 3) :named equalityLeft))"
      "(assert (! (= 3 sumRight) :named equalityRight))(check-sat)";
  auto session = Require(Session::FromReader(
      Chunked(text, kIrregularChunks),
      ParseOptions{.canonicalize_commutative = true}, kSource));
  RequireNext(session, CommandKind::kSetLogic);
  RequireNext(session, CommandKind::kDeclareConst);
  RequireNext(session, CommandKind::kDeclareConst);
  RequireNext(session, CommandKind::kDefineConst);
  RequireNext(session, CommandKind::kDefineConst);
  const auto invalid = Next(session);
  ASSERT_FALSE(invalid);
  EXPECT_EQ(invalid.error().code, DiagnosticCode::kInvalidInput);
  ExpectSpan(invalid.error().where, text, "true");
  RequireNext(session, CommandKind::kAssert);
  RequireNext(session, CommandKind::kAssert);
  const auto query = RequireCheck(session);

  const auto definitions = query->GetDefinitions();
  const auto names = query->GetNamedTerms();
  const auto annotations = query->GetAnnotations();
  const auto assertions = query->GetAssertions();
  ASSERT_EQ(definitions.size(), 2u);
  ASSERT_EQ(names.size(), 4u);
  ASSERT_EQ(annotations.size(), 4u);
  ASSERT_EQ(assertions.size(), 2u);
  EXPECT_EQ(definitions[0].body, definitions[1].body);
  EXPECT_EQ(assertions[0].term, assertions[1].term);
  EXPECT_EQ(names[0].term, definitions[0].body);
  EXPECT_EQ(names[1].term, definitions[1].body);
  EXPECT_EQ(names[2].term, assertions[0].term);
  EXPECT_EQ(names[3].term, assertions[1].term);
  for (std::size_t i = 0; i < names.size(); ++i)
    EXPECT_EQ(annotations[i].term, names[i].term);
  ExpectSpan(annotations[0].where, text, "(! (+ b a) :named sumLeft)");
  ExpectSpan(annotations[1].where, text, "(! (+ a b) :named sumRight)");
  ExpectSpan(assertions[1].where, text,
             "(! (= 3 sumRight) :named equalityRight)");
  EXPECT_GT(session.GetStats().commutative_canonicalizations, 0u);
}

TEST(FrontendOverhaul, WideAritySortingAndIndependentSessionIdsAreStable) {
  SCOPED_TRACE("FE-OVERHAUL-10");
  constexpr std::size_t kArity = 512;
  std::string descending = "(distinct";
  std::string alternating = "(distinct";
  std::string seeds;
  for (std::size_t i = 0; i < kArity; ++i) {
    seeds += " " + std::to_string(i);
    descending += " " + std::to_string(kArity - 1 - i);
    alternating +=
        " " + std::to_string(i % 2 == 0 ? i / 2 : kArity - 1 - i / 2);
  }
  descending += ")";
  alternating += ")";
  const auto text = std::string(kProduceModels) + std::string(kLogic) +
                    "(check-sat)(get-value (" + seeds + " " + descending +
                    alternating + "))";
  const auto inspect = [&](Session session, bool parse_enabled,
                           std::vector<NodeId>& ids) {
    const auto command = ReadValues(session);
    ASSERT_EQ(command.values.size(), kArity + 2);
    const auto& first = command.values[kArity];
    const auto& second = command.values[kArity + 1];
    EXPECT_EQ(first.input, descending);
    EXPECT_EQ(second.input, alternating);
    ExpectSpan(first.where, text, descending);
    ExpectSpan(second.where, text, alternating);
    const std::array original{first.term, second.term};
    const auto result = parse_enabled ? original : [&] {
      EXPECT_NE(original[0], original[1]);
      const auto rewritten = Require(
          CanonicalizeCommutative(session.GetPrefix().GetManager(), original));
      if (rewritten.size() != original.size())
        throw std::runtime_error("Missing canonicalized wide roots");
      return std::array{rewritten[0], rewritten[1]};
    }();
    EXPECT_EQ(result[0], result[1]);

    const auto term = result[0].Borrow();
    ASSERT_EQ(term.GetKind(), kDistinct);
    const auto children = term.GetChildren();
    ASSERT_EQ(children.size(), kArity);
    std::vector<NodeView> expected;
    for (const auto& seed : std::span(command.values).first(kArity))
      expected.push_back(seed.term.Borrow());
    std::ranges::sort(expected, {}, &NodeView::GetId);
    for (std::size_t i = 0; i < kArity; ++i) {
      EXPECT_EQ(children[i], expected[i]) << i;
      if (i > 0) {
        EXPECT_LT(children[i - 1].GetId(), children[i].GetId());
      }
      ids.push_back(children[i].GetId());
    }
    ids.push_back(term.GetId());
  };

  constexpr ParseOptions kEnabled{.canonicalize_commutative = true};
  std::vector<NodeId> text_ids;
  inspect(Require(Session::FromText(text, kEnabled, kSource)), true, text_ids);
  std::vector<NodeId> reader_ids;
  inspect(Require(Session::FromReader(Chunked(text, kIrregularChunks), kEnabled,
                                      kSource)),
          true, reader_ids);
  EXPECT_EQ(text_ids, reader_ids);
  std::vector<NodeId> standalone_ids;
  inspect(Require(Session::FromText(text, kSource)), false, standalone_ids);
}

}  // namespace
}  // namespace dandelion::frontend::testing

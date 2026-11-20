#include "test_support.h"

namespace dandelion::frontend::testing {
namespace {

TEST(FrontendSyntax, ExactSpansAndLexemesAtEverySplitPoint) {
  const std::string input =
      "; comment\r\n(echo \"a\"\"b \\ ; café λ\")\r"
      "(set-info :custom (|match| \"\" #xAB #b00 ()))\n"
      "(set-logic QF_UF) (declare-const |let| Bool)\n"
      "(assert |let|)(check-sat); EOF";
  for (std::size_t split = 0; split <= input.size() + 1; ++split) {
    SCOPED_TRACE(split);
    std::size_t offset = 0;
    auto session = Require(Session::FromReader(
        [&](std::span<char> output) -> ReadResult {
          auto end =
              offset < split && split <= input.size() ? split : input.size();
          if (split > input.size()) end = std::min(offset + 1, input.size());
          const auto count = std::min(output.size(), end - offset);
          std::copy_n(input.data() + offset, count, output.data());
          offset += count;
          return count;
        },
        ParseOptions{.capture_source_records = true}, 42));
    const auto echo = RequireNext(session, CommandKind::kEcho);
    EXPECT_EQ(std::get<std::string>(echo.response), "\"a\"\"b \\ ; café λ\"");
    EXPECT_EQ(echo.header.where.source, 42);
    EXPECT_EQ(echo.header.where.begin, (SourcePosition{11, 2, 1}));
    EXPECT_EQ(echo.header.where.end, (SourcePosition{input.find(")\r") + 1, 2,
                                                     input.find(")\r") - 9}));
    const auto info = RequireNext(session, CommandKind::kSetInfo);
    EXPECT_EQ(info.header.where.begin.line, 3);
    ASSERT_EQ(session.GetSourceRecords()->metadata.size(), 1);
    EXPECT_EQ(session.GetSourceRecords()->metadata[0].value,
              "(|match| \"\" #xAB #b00 ())");
    RequireNext(session, CommandKind::kSetLogic);
    RequireNext(session, CommandKind::kDeclareConst);
    RequireNext(session, CommandKind::kAssert);
    const auto checked = RequireCheck(session);
    ASSERT_EQ(checked->GetAssertions().size(), 1);
    EXPECT_EQ(Applied(checked->GetAssertions()[0].Borrow()).GetNameHint(),
              "let");
    EXPECT_FALSE(Require(Next(session)));
  }
}

TEST(FrontendSyntax, MalformedTokensAndTruncatedCommands) {
  const std::array<std::string, 21> inputs{
      "\xef\xbb\xbf(exit)",
      "(assert #b)",
      "(assert #b102)",
      "(assert #xg)",
      "(assert #XAB)",
      "(assert 08)",
      "(assert 0.)",
      "(assert 01.2)",
      "(echo \"bad\x7f\")",
      "(echo \"bad\xc0\x80\")",
      "(echo \"bad\xed\xa0\x80\")",
      "(echo \"bad\xf4\x90\x80\x80\")",
      "(echo \"bad\xc3\")",
      "(echo \"missing",
      "(assert |missing)",
      "(assert |no\\escape|)",
      "(set-info :x (a (b))",
      "()",
      ")(exit)",
      "(; comment\n",
      std::string("; nul\0\n", 7),
  };
  for (const auto& input : inputs) {
    SCOPED_TRACE(input);
    auto session = Require(Session::FromText(input));
    ExpectError(session.ReadCommand(), DiagnosticCode::kSyntax);
  }
  auto session = Require(Session::FromText("; lead\r\n  (echo \"open"));
  const auto error = session.ReadCommand();
  ASSERT_FALSE(error);
  EXPECT_EQ(error.error().where.begin, (SourcePosition{16, 2, 9}));
}

TEST(FrontendSyntax, ReportsReadFailureSeparatelyFromEof) {
  const std::string prefix = "(echo \"in progress";
  bool consumed = false;
  const auto failed = std::make_error_code(std::errc::io_error);
  auto session =
      Require(Session::FromReader([&](std::span<char> chunk) -> ReadResult {
        if (std::exchange(consumed, true)) return std::unexpected(failed);
        std::copy(prefix.begin(), prefix.end(), chunk.begin());
        return prefix.size();
      }));
  const auto result = session.ReadCommand();
  ExpectError(result, DiagnosticCode::kInputIo);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().io_error, failed);
  EXPECT_EQ(result.error().where.begin.offset, prefix.size());
  auto eof = Require(Session::FromText(prefix));
  ExpectError(eof.ReadCommand(), DiagnosticCode::kSyntax);
  auto complete = Require(Session::FromText("(echo \"done\")"));
  EXPECT_EQ(
      std::get<std::string>(RequireNext(complete, CommandKind::kEcho).response),
      "\"done\"");
  EXPECT_FALSE(Require(Next(complete)));
}

TEST(FrontendSyntax, ChecksTermAndCommandGrammarBeforeSemanticState) {
  constexpr std::array inputs{"(set-logic \"ALL\")",
                              "(set-logic)",
                              "(set-logic ALL extra)",
                              "(get-value ())",
                              "(get-value ((let () 1)))",
                              "(get-model junk)",
                              "(declare-fun f Bool Bool)",
                              "(declare-const x (Bool))",
                              "(declare-sort x x)",
                              "(define-sort T X Int)",
                              "(define-fun f ((a Int extra)) Int a)",
                              "(assert (! true))",
                              "(assert ((_ f) true))",
                              "(assert (as x Int extra))",
                              "(assert (match x ()))",
                              "(assert (forall () true))",
                              "(assert (match x (((c (d b)) true))))",
                              "(set-info :meta reserved (a))",
                              "(set-option :print-success)",
                              "(push)",
                              "(pop true)",
                              "(reset unexpected)",
                              "(check-sat-assuming ()) extra)",
                              "(declare-sort-parameter)",
                              "(define-fun-rec f () Bool)",
                              "(define-funs-rec (() ()) ())",
                              "(get-proof x)",
                              "(get-assignment x)",
                              "(exit x)"};
  for (std::string_view input : inputs) {
    SCOPED_TRACE(input);
    auto session = Require(Session::FromText(input));
    if (input == "(check-sat-assuming ()) extra)") {
      auto validator = Require(SyntaxValidator::FromText(input));
      ASSERT_TRUE(Require(validator.ReadNext()));
      ExpectError(session.ReadCommand(), DiagnosticCode::kUnsupported);
      continue;
    }
    ExpectError(session.ReadCommand(), DiagnosticCode::kSyntax);
  }
}

TEST(FrontendSyntax, ReservedKeywordsAndMalformedCommandArguments) {
  for (const auto input :
       {"(set-info :let 1)", "(|exit|)", "(echo false)", "(check-sat 1)",
        "(declare-datatype D (par () ((c))))",
        "(declare-datatype D (par (T) ((c)) extra))",
        "(declare-datatype D (par T ((c))))",
        "(declare-datatype D ((c (field))))",
        "(declare-datatype D ((c (field Int Bool))))",
        "(declare-datatype D ((c field)))", "(check-sat-assuming () extra)"}) {
    SCOPED_TRACE(input);
    auto session = Require(Session::FromText(input));
    ExpectError(session.ReadCommand(), DiagnosticCode::kSyntax);
    EXPECT_EQ(session.GetPhase(), SessionPhase::kTerminated);
  }
}

TEST(FrontendSyntax, QuotedDataKeepsUtf8AndLineBreaksAtEverySplit) {
  constexpr std::string_view input =
      "(echo \"🌱;()\r\nnext\")\n"
      "(set-logic ALL)(declare-const |🌱;()\nnext| Bool)"
      "(assert |🌱;()\nnext|)(check-sat)";
  for (std::size_t split = 1; split <= input.size(); ++split) {
    SCOPED_TRACE(::testing::Message() << input << " split " << split);
    std::size_t offset = 0;
    auto session = Require(Session::FromReader(
        [&](std::span<char> chunk) -> ReadResult {
          const auto end = offset < split ? split : input.size();
          const auto count = std::min(chunk.size(), end - offset);
          std::copy_n(input.data() + offset, count, chunk.data());
          offset += count;
          return count;
        },
        ParseOptions{.capture_source_records = true}, 27));
    const auto echo = RequireNext(session, CommandKind::kEcho);
    EXPECT_EQ(std::get<std::string>(echo.response), "\"🌱;()\r\nnext\"");
    EXPECT_EQ(echo.header.where, (SourceSpan{27, {0, 1, 1}, {22, 2, 7}}));
    RequireNext(session, CommandKind::kSetLogic);
    RequireNext(session, CommandKind::kDeclareConst);
    RequireNext(session, CommandKind::kAssert);
    const auto checked = RequireCheck(session);
    ASSERT_EQ(checked->GetFunctions().size(), 1);
    ASSERT_EQ(checked->GetAssertions().size(), 1);
    // The source spelling retains delimiters. The declaration hint is the
    // canonical dequoted name.
    EXPECT_EQ(session.GetSourceRecords()->functions[0].source_name,
              "|🌱;()\nnext|");
    EXPECT_EQ(checked->GetFunctions()[0].Borrow().GetNameHint(), "🌱;()\nnext");
    EXPECT_EQ(Applied(checked->GetAssertions()[0].Borrow()),
              checked->GetFunctions()[0].Borrow());
    EXPECT_FALSE(Require(session.ReadCommand()));
  }
}

TEST(FrontendSyntax, ThrowingReaderTerminatesAtIncompleteCommandBoundary) {
  constexpr std::string_view prefix =
      "(set-logic QF_SAT)\r\n(assert false)\n(check-sat";
  constexpr std::uint32_t kSource = 53;
  constexpr SourcePosition kBoundary{prefix.size(), 3, 11};
  const auto io_error = std::make_error_code(std::errc::broken_pipe);
  enum class End { kThrow, kError, kEof };
  for (const auto end : {End::kEof, End::kError, End::kThrow}) {
    SCOPED_TRACE(::testing::Message() << "FE-READER-02 " << prefix << " end "
                                      << std::to_underlying(end));
    int reads = 0;
    auto session = Require(Session::FromReader(
        [&](std::span<char> bytes) -> ReadResult {
          if (++reads == 1) {
            std::ranges::copy(prefix, bytes.begin());
            return prefix.size();
          }
          if (end == End::kThrow) throw std::runtime_error("Reader fault");
          if (end == End::kError) return std::unexpected(io_error);
          return 0;
        },
        kSource));
    RequireNext(session, CommandKind::kSetLogic);
    RequireNext(session, CommandKind::kAssert);
    EXPECT_EQ(reads, 1);
    const auto next = session.ReadCommand();
    ASSERT_FALSE(next);
    EXPECT_EQ(next.error().code, end == End::kEof ? DiagnosticCode::kSyntax
                                                  : DiagnosticCode::kInputIo);
    if (end != End::kEof) {
      EXPECT_EQ(next.error().where,
                (SourceSpan{kSource, kBoundary, kBoundary}));
    }
    if (end == End::kError) {
      EXPECT_EQ(next.error().io_error, io_error);
    }
    EXPECT_EQ(session.GetPhase(), SessionPhase::kTerminated);
    EXPECT_EQ(session.GetPrefix().GetLogic(), "QF_SAT");
    ASSERT_EQ(session.GetPrefix().GetAssertions().size(), 1);
    EXPECT_EQ(
        session.GetPrefix().GetAssertions()[0].Borrow().GetValue()->GetBool(),
        false);
    EXPECT_EQ(reads, 2);
    EXPECT_FALSE(Require(session.ReadCommand()));
    EXPECT_EQ(reads, 2);
  }
}

TEST(FrontendSyntax, AllReaderExceptionsUseTheCurrentReadBoundary) {
  constexpr std::string_view prefix = "(set-logic QF_SAT)\n(assert\n  true";
  constexpr std::uint32_t kSource = 54;
  constexpr SourcePosition kBoundary{prefix.size(), 3, 7};
  enum class Fault { kAllocation, kRuntime, kLength, kReference, kOther };
  for (auto fault : {Fault::kAllocation, Fault::kRuntime, Fault::kLength,
                     Fault::kReference, Fault::kOther}) {
    SCOPED_TRACE(::testing::Message()
                 << "FE-READER-EXCEPTIONS-02 " << prefix << " fault "
                 << std::to_underlying(fault));
    int reads = 0;
    auto session = Require(Session::FromReader(
        [&](std::span<char> bytes) -> ReadResult {
          if (++reads == 1) {
            std::ranges::copy(prefix, bytes.begin());
            return prefix.size();
          }
          switch (fault) {
            case Fault::kAllocation:
              throw std::bad_alloc();
            case Fault::kRuntime:
              throw std::runtime_error("Reader fault");
            case Fault::kLength:
              throw std::length_error("Reader fault");
            case Fault::kReference:
              throw ReferenceLimit();
            case Fault::kOther:
              throw fault;
          }
          std::unreachable();
        },
        kSource));
    RequireNext(session, CommandKind::kSetLogic);
    EXPECT_EQ(reads, 1);
    const auto next = session.ReadCommand();
    ASSERT_FALSE(next);
    EXPECT_EQ(next.error().code, DiagnosticCode::kInputIo);
    EXPECT_EQ(next.error().where, (SourceSpan{kSource, kBoundary, kBoundary}));
    EXPECT_FALSE(next.error().node_error);
    EXPECT_EQ(session.GetPhase(), SessionPhase::kTerminated);
    EXPECT_EQ(session.GetPrefix().GetLogic(), "QF_SAT");
    EXPECT_TRUE(session.GetPrefix().GetAssertions().empty());
    EXPECT_FALSE(Require(session.ReadCommand()));
    EXPECT_EQ(reads, 2);
  }
}

TEST(FrontendSyntax, MissingReaderAndOversizedReadCountReportInputIo) {
  const auto check = [](Reader reader) {
    auto session = Session::FromReader(std::move(reader), 45);
    const auto result = session ? session->ReadCommand()
                                : Outcome<std::optional<TypedCommand>>(
                                      std::unexpected(session.error()));
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().code, DiagnosticCode::kInputIo);
    EXPECT_EQ(result.error().where, (SourceSpan{45, {}, {}}));
    if (session) {
      EXPECT_EQ(session->GetPhase(), SessionPhase::kTerminated);
    }
  };
  check({});
  int reads = 0;
  check([&](std::span<char> bytes) -> ReadResult {
    ++reads;
    return bytes.size() + 1;
  });
  EXPECT_EQ(reads, 1);
}

TEST(FrontendSyntax, StopsOnExitWithUnreadOrAlreadyBufferedSuffix) {
  const std::string input = "(exit)\xff\0 malformed";
  int reads = 0;
  auto session =
      Require(Session::FromReader([&](std::span<char> output) -> ReadResult {
        if (++reads > 1)
          return std::unexpected(std::make_error_code(std::errc::io_error));
        std::copy(input.begin(), input.end(), output.begin());
        return input.size();
      }));
  RequireNext(session, CommandKind::kExit);
  EXPECT_EQ(session.GetPhase(), SessionPhase::kTerminated);
  EXPECT_FALSE(Require(session.ReadCommand()));
  EXPECT_EQ(reads, 1);

  const auto unread = std::string_view(input).substr(6);
  ASSERT_FALSE(unread.empty());
  reads = 0;
  auto split =
      Require(Session::FromReader([&](std::span<char> bytes) -> ReadResult {
        const auto chunk = ++reads == 1 ? std::string_view("(exit)") : unread;
        std::ranges::copy(chunk, bytes.begin());
        return chunk.size();
      }));
  RequireNext(split, CommandKind::kExit);
  EXPECT_EQ(split.GetPhase(), SessionPhase::kTerminated);
  EXPECT_FALSE(Require(split.ReadCommand()));
  EXPECT_EQ(reads, 1);
}

TEST(FrontendSyntax, DeepOpaqueAttributesAndLongUtf8Symbol) {
  const std::string name(1024 * 1024 + 1, 'x');
  constexpr std::size_t kDepth = 16000;
  const auto input = "(set-info :opaque " + std::string(kDepth, '(') + "|as|" +
                     std::string(kDepth, ')') +
                     ")(set-logic QF_UF)(declare-const |" + name +
                     "λ| Bool)(assert |" + name + "λ|)(check-sat)";
  std::size_t offset = 0;
  auto session = Require(Session::FromReader(
      [&](std::span<char> chunk) -> ReadResult {
        const auto count = std::min(chunk.size(), input.size() - offset);
        std::copy_n(input.data() + offset, count, chunk.data());
        offset += count;
        return count;
      },
      ParseOptions{.capture_source_records = true}));
  RequireNext(session, CommandKind::kSetInfo);
  ASSERT_EQ(session.GetSourceRecords()->metadata.size(), 1);
  EXPECT_EQ(session.GetSourceRecords()->metadata[0].value->size(),
            2 * kDepth + 4);
  RequireNext(session, CommandKind::kSetLogic);
  RequireNext(session, CommandKind::kDeclareConst);
  RequireNext(session, CommandKind::kAssert);
  const auto query = RequireCheck(session);
  ASSERT_EQ(query->GetFunctions().size(), 1);
  EXPECT_EQ(query->GetFunctions()[0].Borrow().GetNameHint(), name + "λ");
}

}  // namespace
}  // namespace dandelion::frontend::testing

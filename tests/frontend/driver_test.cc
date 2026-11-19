#include "model_support.h"

namespace dandelion::frontend::testing {
namespace {

TEST(FrontendDriver, CheckWritesAndFlushesBeforeReadingAnOpenStream) {
  const std::string first =
      "(set-option :print-success true)(set-info :status unsat)(set-logic "
      "QF_BV)"
      "(assert true)(set-option :print-success false)(check-sat";
  const std::array<std::string_view, 3> chunks{first, ")", "(exit)\xff"};
  std::size_t reads = 0;
  int checks = 0;
  CapturedSink sink;
  auto session =
      Require(Session::FromReader([&](std::span<char> buffer) -> ReadResult {
        if (reads == chunks.size())
          return std::unexpected(std::make_error_code(std::errc::io_error));
        if (reads == 1) {
          EXPECT_EQ(checks, 0);
        }
        if (reads == 2) {
          EXPECT_EQ(checks, 1);
          EXPECT_EQ(sink.output, "success\nsuccess\nsuccess\nsuccess\nsat\n");
          EXPECT_EQ(sink.flushes, 5);
        }
        const auto chunk = chunks[reads++];
        std::copy(chunk.begin(), chunk.end(), buffer.begin());
        return chunk.size();
      }));
  const auto result = dandelion::frontend::Run(
      session,
      [&](const QueryContext& query,
          SessionOptions options) -> Outcome<CheckResult> {
        ++checks;
        EXPECT_EQ(query.GetLogic(), "QF_BV");
        EXPECT_FALSE(options.produce_models);
        EXPECT_EQ(query.GetMetadata()[0].value, "unsat");
        EXPECT_EQ(query.GetAssertions().size(), 1);
        return CheckResult{CheckAnswer::kSat, UnknownReason::kUnknown, nullptr};
      },
      sink.Get());
  EXPECT_EQ(result.status, 0);
  EXPECT_EQ(checks, 1);
  EXPECT_EQ(reads, 3);
  EXPECT_FALSE(result.diagnostic);
}

TEST(FrontendDriver, BordersOfTheCommittedPrefixAndImmediateExit) {
  const auto execute = [](std::string_view script, std::string_view prefix,
                          int expected_checks, int expected_status,
                          std::optional<DiagnosticCode> code) {
    SCOPED_TRACE(script);
    auto session = Require(Session::FromText(script));
    CapturedSink sink;
    int checks = 0;
    const auto result = dandelion::frontend::Run(
        session,
        [&](const QueryContext&, SessionOptions) -> Outcome<CheckResult> {
          ++checks;
          return CheckResult{CheckAnswer::kUnsat, UnknownReason::kUnknown,
                             nullptr};
        },
        sink.Get());
    EXPECT_EQ(result.status, expected_status);
    EXPECT_EQ(checks, expected_checks);
    EXPECT_TRUE(sink.output.starts_with(prefix)) << sink.output;
    if (code) {
      ASSERT_TRUE(result.diagnostic);
      EXPECT_EQ(result.diagnostic->code, *code);
    } else {
      EXPECT_FALSE(result.diagnostic);
      EXPECT_EQ(sink.output, prefix);
    }
  };
  execute("(set-logic ALL); eof", "", 0, 0, std::nullopt);
  execute("(set-option :print-success true)(exit) bad )\xff",
          "success\nsuccess\n", 0, 0, std::nullopt);
  execute(
      "(get-option :custom)(get-info :custom)(echo \"a\"\"b\")"
      "(set-logic ALL)(check-sat)",
      "unsupported\nunsupported\n\"a\"\"b\"\nunsat\n", 1, 0, std::nullopt);
  execute("(set-option :custom false)(set-logic ALL)(check-sat)",
          "unsupported\n", 0, 1, DiagnosticCode::kUnsupported);
  execute("(set-logic ALL)(assert true)(reset-assertions)(check-sat)",
          "unsupported\n", 0, 1, DiagnosticCode::kUnsupported);
  execute("(set-logic ALL)(check-sat)(check-sat)", "unsat\n(error ", 1, 1,
          DiagnosticCode::kInvalidState);
  execute("(set-logic ALL)(check-sat)(assert #xg)", "unsat\n(error ", 1, 1,
          DiagnosticCode::kSyntax);
  execute("(set-logic ALL)(check-sat)(get-info :reason-unknown)",
          "unsat\n(error ", 1, 1, DiagnosticCode::kInvalidState);
  execute("(get-info :reason-unknown)", "(error ", 0, 1,
          DiagnosticCode::kInvalidState);
}

TEST(FrontendDriver, ResultPrerequisitesAndUnknownCandidates) {
  constexpr std::string_view prefix =
      "(set-option :produce-models true)(set-logic QF_IDL)(declare-const x Int)"
      "(check-sat)";
  for (auto answer :
       {CheckAnswer::kSat, CheckAnswer::kUnsat, CheckAnswer::kUnknown}) {
    auto script = std::string(prefix) + "(get-value (@unregistered))";
    CapturedSink sink;
    auto session = Require(Session::FromText(script));
    const auto result = dandelion::frontend::Run(
        session,
        [&](const QueryContext&, SessionOptions) -> Outcome<CheckResult> {
          return CheckResult{answer, UnknownReason::kUnknown, nullptr};
        },
        sink.Get());
    EXPECT_EQ(result.status, 1);
    ASSERT_TRUE(result.diagnostic);
    if (answer == CheckAnswer::kSat) {
      EXPECT_EQ(result.diagnostic->code, DiagnosticCode::kEngineFailure);
      EXPECT_TRUE(sink.output.starts_with("(error "));
    } else if (answer == CheckAnswer::kUnsat) {
      EXPECT_EQ(result.diagnostic->code, DiagnosticCode::kInvalidState);
      EXPECT_TRUE(sink.output.starts_with("unsat\n(error "));
    } else {
      EXPECT_EQ(result.diagnostic->code, DiagnosticCode::kModelUnavailable);
      EXPECT_TRUE(sink.output.starts_with("unknown\n(error "));
    }
  }
  const auto script =
      std::string(prefix) +
      "(get-info :reason-unknown)(get-value ((+ x 2) x))(get-value ((+ x 4)))";
  auto session = Require(Session::FromText(script));
  CapturedSink sink;
  const auto result = dandelion::frontend::Run(
      session,
      [](const QueryContext&, SessionOptions) -> Outcome<CheckResult> {
        return CheckResult{CheckAnswer::kUnknown, UnknownReason::kIncomplete,
                           std::make_unique<ControlledModel>(
                               [](ModelOwner& model, NodeView term) {
                                 const auto checked =
                                     term.GetKind() == Kind::kIntAdd
                                         ? 1 + term.GetChildren()[1]
                                                   .GetValue()
                                                   ->GetInteger()
                                                   ->TryInt64()
                                                   .value()
                                         : 1;
                                 return IntegerScalar(model, checked);
                               })};
      },
      sink.Get());
  ASSERT_EQ(result.status, 0);
  ASSERT_TRUE(result.model);
  EXPECT_EQ(result.model->GetAnswer(), CheckAnswer::kUnknown);
  EXPECT_EQ(sink.output,
            "unknown\n(:reason-unknown incomplete)\n(((+ x 2) 3) (x 1))\n(((+ "
            "x 4) 5))\n");

  auto generic = Require(Session::FromText(
      "(set-logic ALL)(check-sat)(get-info :reason-unknown)"));
  CapturedSink generic_sink;
  EXPECT_EQ(
      dandelion::frontend::Run(
          generic,
          [](const QueryContext&, SessionOptions) -> Outcome<CheckResult> {
            return CheckResult{CheckAnswer::kUnknown, UnknownReason::kUnknown,
                               nullptr};
          },
          generic_sink.Get())
          .status,
      0);
  EXPECT_EQ(generic_sink.output, "unknown\n(:reason-unknown unknown)\n");
}

TEST(FrontendDriver, ErrorsBeforeAndAfterResponseBytesAndSinkFailures) {
  constexpr std::string_view input =
      "(set-option :produce-models true)(set-logic ALL)(check-sat)(get-value "
      "(1 2))(echo \"tail\")";
  for (int at : {1, 2}) {
    for (bool representation : {false, true}) {
      CapturedSink sink;
      int calls = 0;
      const auto run = WithModel(
          input,
          std::make_unique<ControlledModel>(
              [&](ModelOwner& model, NodeView term) -> Outcome<ValueRef> {
                if (++calls == at) {
                  if (representation)
                    return std::unexpected(
                        Diagnostic{DiagnosticCode::kResource,
                                   {},
                                   "Controlled size error",
                                   NodeError{ErrorCode::kResourceLimit,
                                             ResourceKind::kSize}});
                  return std::unexpected(ModelError());
                }
                auto manager =
                    Require(NodeManager::Attach(model.GetQuery().GetManager()));
                return Scalar(model, Require(manager.RetainNode(term)));
              }),
          sink);
      EXPECT_EQ(run.status, 1);
      ASSERT_TRUE(run.diagnostic);
      EXPECT_EQ(run.diagnostic->code, representation
                                          ? DiagnosticCode::kResource
                                          : DiagnosticCode::kModelFailure);
      EXPECT_EQ(calls, at);
      if (at == 1) {
        EXPECT_TRUE(sink.output.starts_with("sat\n(error ")) << sink.output;
        EXPECT_TRUE(sink.output.ends_with("\")\n"));
      } else {
        EXPECT_EQ(sink.output, "sat\n((1 1)");
        EXPECT_FALSE(sink.output.contains("error"));
      }
    }
  }
  for (bool flush : {false, true}) {
    CapturedSink sink;
    if (flush)
      sink.BreakFlush(2);
    else
      sink.StopAfter(9);
    const auto result = WithModel(
        input,
        std::make_unique<ControlledModel>([](ModelOwner& model, NodeView term) {
          auto manager =
              Require(NodeManager::Attach(model.GetQuery().GetManager()));
          return Scalar(model, Require(manager.RetainNode(term)));
        }),
        sink);
    EXPECT_EQ(result.status, 1);
    ASSERT_TRUE(result.diagnostic);
    EXPECT_EQ(result.diagnostic->code, DiagnosticCode::kOutputIo);
    EXPECT_TRUE(sink.output.starts_with("sat\n"));
    EXPECT_FALSE(sink.output.contains("error"));
    EXPECT_FALSE(sink.output.contains("tail"));
  }
}

TEST(FrontendDriver,
     NoResultOnEngineOrInputFailureAndMalformedModelSyntaxFirst) {
  CapturedSink engine_failure;
  auto session = Require(Session::FromText("(set-logic ALL)(check-sat)"));
  const auto failed = dandelion::frontend::Run(
      session,
      [](const QueryContext&, SessionOptions) -> Outcome<CheckResult> {
        return std::unexpected(Diagnostic{
            DiagnosticCode::kEngineFailure, {}, "Quoted \"engine\" failure"});
      },
      engine_failure.Get());
  ASSERT_TRUE(failed.diagnostic);
  EXPECT_EQ(failed.diagnostic->code, DiagnosticCode::kEngineFailure);
  EXPECT_TRUE(engine_failure.output.starts_with("(error "));
  EXPECT_TRUE(engine_failure.output.contains("\"\"engine\"\""));

  CapturedSink model_syntax;
  auto invalid =
      Require(Session::FromText("(set-option :produce-models true)(set-logic "
                                "ALL)(check-sat)(get-value ((let () 0)))"));
  const auto result = dandelion::frontend::Run(
      invalid,
      [](const QueryContext&, SessionOptions) -> Outcome<CheckResult> {
        return CheckResult{CheckAnswer::kUnknown, UnknownReason::kUnknown,
                           nullptr};
      },
      model_syntax.Get());
  ASSERT_TRUE(result.diagnostic);
  EXPECT_EQ(result.diagnostic->code, DiagnosticCode::kSyntax);
  EXPECT_TRUE(model_syntax.output.starts_with("unknown\n(error "));

  auto read_failure =
      Require(Session::FromReader([](std::span<char>) -> ReadResult {
        return std::unexpected(std::make_error_code(std::errc::io_error));
      }));
  CapturedSink bad_read;
  const auto io = dandelion::frontend::Run(read_failure, {}, bad_read.Get());
  ASSERT_TRUE(io.diagnostic);
  EXPECT_EQ(io.diagnostic->code, DiagnosticCode::kInputIo);
  EXPECT_TRUE(bad_read.output.starts_with("(error "));
}

TEST(FrontendDriver, ExactResponsesAndFlushesFromCommittedState) {
  int checks = 0;
  const Engine engine = [&](const QueryContext& query,
                            SessionOptions options) -> Outcome<CheckResult> {
    ++checks;
    EXPECT_FALSE(options.print_success);
    const auto roots = query.GetAssertions();
    const bool has_false =
        roots.size() == 1 &&
        roots[0].term.Borrow().GetKind() == Kind::kBoolValue &&
        roots[0].term.Borrow().GetValue()->GetBool() == false;
    return CheckResult{has_false ? CheckAnswer::kUnsat : CheckAnswer::kSat,
                       UnknownReason::kUnknown, nullptr};
  };
  constexpr std::string_view input =
      "(set-option :print-success true)(set-logic QF_SAT)(assert false)"
      "(set-option :print-success false)(get-option :other)(check-sat)";
  for (const bool bad_tail : {false, true}) {
    auto script = std::string(input);
    if (bad_tail) script += "(assert true)(echo \"unread\")";
    SCOPED_TRACE(::testing::Message() << "FE-DRIVER-02 " << script);
    checks = 0;
    auto session = Require(Session::FromText(script));
    CapturedSink sink;
    const auto result = dandelion::frontend::Run(session, engine, sink.Get());
    auto expected =
        std::string("success\nsuccess\nsuccess\nunsupported\nunsat\n");
    EXPECT_EQ(result.status, bad_tail ? 1 : 0);
    if (bad_tail) {
      ASSERT_TRUE(result.diagnostic);
      EXPECT_EQ(result.diagnostic->code, DiagnosticCode::kInvalidState);
      EXPECT_EQ(result.diagnostic->where.begin.offset, input.size());
      expected += DiagnosticResponse(*result.diagnostic);
    } else {
      EXPECT_FALSE(result.diagnostic);
    }
    EXPECT_EQ(sink.output, expected);
    EXPECT_EQ(sink.flushes, bad_tail ? 6u : 5u);
    EXPECT_EQ(checks, 1);
  }

  constexpr std::string_view exit =
      "(set-option :print-success true)(exit) (assert #xg)";
  SCOPED_TRACE(::testing::Message() << "FE-DRIVER-02 " << exit);
  auto session = Require(Session::FromText(exit));
  CapturedSink sink;
  checks = 0;
  const auto result = dandelion::frontend::Run(session, engine, sink.Get());
  EXPECT_EQ(result.status, 0);
  EXPECT_FALSE(result.diagnostic);
  EXPECT_EQ(session.GetPhase(), SessionPhase::kTerminated);
  EXPECT_EQ(sink.output, "success\nsuccess\n");
  EXPECT_EQ(sink.flushes, 2);
  EXPECT_EQ(checks, 0);
}

TEST(FrontendDriver, ResetAssertionsPreservesFalseAndStopsBeforeCheck) {
  for (const bool reset : {false, true}) {
    std::string script = "(set-logic QF_SAT)(assert false)";
    if (reset) script += "(reset-assertions)";
    script += "(check-sat)";
    SCOPED_TRACE(::testing::Message() << "FE-DRIVER-02 " << script);
    int calls = 0;
    auto session = Require(Session::FromText(script));
    CapturedSink sink;
    const auto result = dandelion::frontend::Run(
        session,
        [&](const QueryContext& query, SessionOptions) -> Outcome<CheckResult> {
          ++calls;
          const auto roots = query.GetAssertions();
          const bool has_false =
              roots.size() == 1 &&
              roots[0].term.Borrow().GetKind() == Kind::kBoolValue &&
              roots[0].term.Borrow().GetValue()->GetBool() == false;
          return CheckResult{
              has_false ? CheckAnswer::kUnsat : CheckAnswer::kSat,
              UnknownReason::kUnknown, nullptr};
        },
        sink.Get());
    EXPECT_EQ(result.status, reset ? 1 : 0);
    if (reset) {
      ASSERT_TRUE(result.diagnostic);
      EXPECT_EQ(result.diagnostic->code, DiagnosticCode::kUnsupported);
    } else {
      EXPECT_FALSE(result.diagnostic);
    }
    EXPECT_EQ(sink.output, reset ? "unsupported\n" : "unsat\n");
    EXPECT_EQ(sink.flushes, 1);
    EXPECT_EQ(calls, reset ? 0 : 1);
    const auto roots = session.GetPrefix().GetAssertions();
    ASSERT_EQ(roots.size(), 1);
    ASSERT_EQ(roots[0].term.Borrow().GetKind(), Kind::kBoolValue);
    EXPECT_EQ(roots[0].term.Borrow().GetValue()->GetBool(), false);
  }
}

TEST(FrontendDriver, FixedFalseOptionsAndGettersUseExactProtocolResponses) {
  for (const std::string_view option :
       {":interactive-mode", ":produce-assertions", ":produce-assignments",
        ":produce-proofs", ":produce-unsat-assumptions",
        ":produce-unsat-cores"}) {
    for (const bool enable : {false, true}) {
      auto input = std::string("(set-option :print-success true)(set-option ") +
                   std::string(option) + " false)(get-option " +
                   std::string(option) + ")";
      if (enable) input += "(set-option " + std::string(option) + " true)";
      input += "(set-logic QF_SAT)(check-sat)";
      SCOPED_TRACE(::testing::Message() << "FE-DRIVER-02 " << input);
      auto session = Require(Session::FromText(input));
      CapturedSink sink;
      int checks = 0;
      const auto result = dandelion::frontend::Run(
          session,
          [&](const QueryContext&, SessionOptions) -> Outcome<CheckResult> {
            ++checks;
            return CheckResult{CheckAnswer::kSat, UnknownReason::kUnknown,
                               nullptr};
          },
          sink.Get());
      EXPECT_EQ(result.status, enable ? 1 : 0);
      EXPECT_EQ(checks, enable ? 0 : 1);
      EXPECT_EQ(sink.flushes, enable ? 4u : 5u);
      EXPECT_EQ(sink.output, enable
                                 ? "success\nsuccess\nfalse\nunsupported\n"
                                 : "success\nsuccess\nfalse\nsuccess\nsat\n");
      if (enable) {
        ASSERT_TRUE(result.diagnostic);
        EXPECT_EQ(result.diagnostic->code, DiagnosticCode::kUnsupported);
      } else {
        EXPECT_FALSE(result.diagnostic);
      }
    }
  }
}

TEST(FrontendDriver, EngineAndWriterFaultsStopDispatchImmediately) {
  constexpr std::string_view input =
      "(set-logic QF_SAT)(check-sat)(echo \"unread\")";
  SCOPED_TRACE(::testing::Message() << "FE-DRIVER-02 " << input);
  for (const bool throw_engine : {false, true}) {
    int checks = 0;
    int provider_calls = 0;
    auto session = Require(Session::FromText(input));
    CapturedSink sink;
    const auto result = dandelion::frontend::Run(
        session,
        [&](const QueryContext&, SessionOptions) -> Outcome<CheckResult> {
          ++checks;
          if (throw_engine) throw std::runtime_error("Engine fault");
          return CheckResult{CheckAnswer::kUnsat, UnknownReason::kUnknown,
                             std::make_unique<ControlledModel>(
                                 [&](ModelOwner& model, NodeView) {
                                   ++provider_calls;
                                   return IntegerScalar(model, 0);
                                 })};
        },
        sink.Get());
    EXPECT_EQ(result.status, 1);
    ASSERT_TRUE(result.diagnostic);
    EXPECT_EQ(result.diagnostic->code, DiagnosticCode::kEngineFailure);
    EXPECT_EQ(sink.output, DiagnosticResponse(*result.diagnostic));
    EXPECT_EQ(sink.flushes, 1);
    EXPECT_EQ(checks, 1);
    EXPECT_EQ(provider_calls, 0);
    EXPECT_FALSE(result.model);
    EXPECT_EQ(session.GetPhase(), SessionPhase::kTerminated);
  }
  for (const bool throw_writer : {false, true}) {
    int checks = 0;
    int writes = 0;
    int flushes = 0;
    auto session = Require(Session::FromText(input));
    const auto io_error = std::make_error_code(std::errc::broken_pipe);
    const auto result = dandelion::frontend::Run(
        session,
        [&](const QueryContext&, SessionOptions) -> Outcome<CheckResult> {
          ++checks;
          return CheckResult{CheckAnswer::kSat, UnknownReason::kUnknown,
                             nullptr};
        },
        {[&](std::string_view bytes) -> WriteResult {
           ++writes;
           EXPECT_EQ(bytes, "sat");
           if (throw_writer) throw std::runtime_error("Writer fault");
           return std::unexpected(io_error);
         },
         [&]() -> std::expected<void, std::error_code> {
           ++flushes;
           return {};
         }});
    EXPECT_EQ(result.status, 1);
    ASSERT_TRUE(result.diagnostic);
    EXPECT_EQ(result.diagnostic->code, DiagnosticCode::kOutputIo);
    if (!throw_writer) {
      EXPECT_EQ(result.diagnostic->io_error, io_error);
    }
    EXPECT_EQ(writes, 1);
    EXPECT_EQ(flushes, 0);
    EXPECT_EQ(checks, 1);
    EXPECT_EQ(session.GetPhase(), SessionPhase::kTerminated);
  }
}

TEST(FrontendDriver,
     ReaderFaultDuringCheckPreservesPrefixWithoutInvokingEngine) {
  constexpr std::string_view prefix =
      "(set-logic QF_SAT)\r\n(assert false)\n(check-sat";
  constexpr std::uint32_t kSource = 63;
  constexpr SourcePosition kBoundary{prefix.size(), 3, 11};
  const auto io_error = std::make_error_code(std::errc::broken_pipe);
  enum class End { kThrow, kError, kEof };
  for (const auto end : {End::kEof, End::kError, End::kThrow}) {
    SCOPED_TRACE(::testing::Message() << "FE-READER-02 " << prefix << " end "
                                      << std::to_underlying(end));
    int reads = 0;
    auto session = Require(Session::FromReader(
        [&](std::span<char> bytes) -> ReadResult {
          ++reads;
          if (reads == 1) {
            std::ranges::copy(prefix, bytes.begin());
            return prefix.size();
          }
          EXPECT_EQ(reads, 2);
          if (end == End::kThrow) throw std::runtime_error("Reader fault");
          if (end == End::kError) return std::unexpected(io_error);
          return 0;
        },
        kSource));
    int checks = 0;
    CapturedSink sink;
    const auto result = dandelion::frontend::Run(
        session,
        [&](const QueryContext&, SessionOptions) -> Outcome<CheckResult> {
          ++checks;
          return CheckResult{CheckAnswer::kSat, UnknownReason::kUnknown,
                             nullptr};
        },
        sink.Get());
    EXPECT_EQ(result.status, 1);
    ASSERT_TRUE(result.diagnostic);
    EXPECT_EQ(result.diagnostic->code, end == End::kEof
                                           ? DiagnosticCode::kSyntax
                                           : DiagnosticCode::kInputIo);
    if (end != End::kEof) {
      EXPECT_EQ(result.diagnostic->where,
                (SourceSpan{kSource, kBoundary, kBoundary}));
    }
    if (end == End::kError) {
      EXPECT_EQ(result.diagnostic->io_error, io_error);
    }
    EXPECT_EQ(sink.output, DiagnosticResponse(*result.diagnostic));
    EXPECT_EQ(sink.flushes, 1);
    EXPECT_EQ(checks, 0);
    EXPECT_EQ(reads, 2);
    EXPECT_EQ(session.GetPhase(), SessionPhase::kTerminated);
    EXPECT_EQ(session.GetPrefix().GetLogic(), "QF_SAT");
    const auto roots = session.GetPrefix().GetAssertions();
    ASSERT_EQ(roots.size(), 1);
    ASSERT_EQ(roots[0].term.Borrow().GetKind(), Kind::kBoolValue);
    EXPECT_EQ(roots[0].term.Borrow().GetValue()->GetBool(), false);
    EXPECT_FALSE(Require(session.NextSyntax()));
    EXPECT_EQ(reads, 2);
  }
}

}  // namespace
}  // namespace dandelion::frontend::testing

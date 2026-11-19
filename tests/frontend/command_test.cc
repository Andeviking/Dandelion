#include "test_support.h"

namespace dandelion::frontend::testing {
namespace {

TEST(FrontendCommands, PrefixStateMetadataAndOwnedCheckContext) {
  std::shared_ptr<const QueryContext> query;
  {
    const std::string input =
        "(set-option :print-success true)(set-info :status unsat)"
        "(set-info :smt-lib-version 2.6)(set-info :smt-lib-version 2.7)"
        "(set-info :source |source|)(set-info :source \"a\"\"b\")"
        "(set-info :category \"crafted\")(set-info :category \"random\")"
        "(set-info :category \"industrial\")(set-info :license \"test\")"
        "(set-info :optional)(set-info :status sat)(set-info :status unknown)"
        "(set-option :produce-models true)(set-option :global-declarations "
        "true)"
        "(set-logic |ALL|)(declare-sort U 0)(define-sort Alias () U)"
        "(declare-fun f (Alias Int) U)(declare-const unused Bool)"
        "(define-const n Int 1)(assert true)(assert true)(check-sat)"
        "(set-info :status sat)(get-model)(exit)";
    auto session = Require(Session::FromText(input));
    EXPECT_EQ(session.GetPhase(), SessionPhase::kBeforeLogic);
    while (const auto command = Require(Next(session))) {
      if (command->query) {
        EXPECT_EQ(session.GetPhase(), SessionPhase::kAfterCheck);
        query = command->query;
      }
    }
    EXPECT_EQ(session.GetPhase(), SessionPhase::kTerminated);
    EXPECT_EQ(session.GetMetadata().size(), 13);
  }
  ASSERT_TRUE(query);
  EXPECT_EQ(query->GetLogic(), "ALL");
  EXPECT_TRUE(query->GetOptions().produce_models);
  ASSERT_EQ(query->GetMetadata().size(), 12);
  EXPECT_EQ(query->GetMetadata().front().value, "unsat");
  EXPECT_EQ(query->GetMetadata()[4].value, "\"a\"\"b\"");
  EXPECT_FALSE(query->GetMetadata()[9].value);
  EXPECT_EQ(query->GetMetadata().back().value, "unknown");
  ASSERT_EQ(query->GetUserSorts().size(), 1);
  ASSERT_EQ(query->GetFunctions().size(), 2);
  const auto& function = query->GetFunctions()[0];
  EXPECT_EQ(function.source_name, "f");
  ASSERT_EQ(function.source_rank.size(), 3);
  EXPECT_EQ(function.source_rank[0], "Alias");
  EXPECT_EQ(function.source_rank[1], "Int");
  EXPECT_EQ(function.source_rank[2], "U");
  ASSERT_EQ(query->GetAssertions().size(), 2);
  EXPECT_EQ(query->GetAssertions()[0].term, query->GetAssertions()[1].term);
  EXPECT_LT(query->GetAssertions()[0].where.begin.offset,
            query->GetAssertions()[1].where.begin.offset);
  ASSERT_EQ(query->GetDefinitions().size(), 1);
  EXPECT_EQ(query->FindDefinition(query->GetDefinitions()[0].decl.GetId()),
            &query->GetDefinitions()[0]);
  EXPECT_FALSE(query->GetManager().IsNull());
}

TEST(FrontendCommands, StatusLogicOptionAndDeclarationErrorsPreservePrefix) {
  struct Case {
    // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
    std::string_view input;
    DiagnosticCode code;
    // NOLINTEND(readability-identifier-naming)
  };
  const std::array cases{
      Case{"(assert true)", DiagnosticCode::kInvalidState},
      Case{"(check-sat)", DiagnosticCode::kInvalidState},
      Case{"(set-logic QF_NotAPolicy)", DiagnosticCode::kUnsupported},
      Case{"(set-logic QF_S)", DiagnosticCode::kUnsupported},
      Case{"(set-logic QF_SLIA)", DiagnosticCode::kUnsupported},
      Case{"(set-logic QF_SNIA)", DiagnosticCode::kUnsupported},
      Case{"(set-logic ALL)(set-logic ALL)", DiagnosticCode::kInvalidState},
      Case{"(set-logic ALL)(check-sat)(check-sat)",
           DiagnosticCode::kInvalidState},
      Case{"(set-logic ALL)(check-sat)(assert true)",
           DiagnosticCode::kInvalidState},
      Case{"(set-logic ALL)(check-sat)(declare-sort X 0)",
           DiagnosticCode::kInvalidState},
      Case{"(set-logic ALL)(check-sat)(define-const x Bool true)",
           DiagnosticCode::kInvalidState},
      Case{"(set-logic ALL)(set-option :global-declarations false)",
           DiagnosticCode::kInvalidState},
      Case{"(set-logic ALL)(set-option :produce-models true)",
           DiagnosticCode::kInvalidState},
      Case{"(set-logic ALL)(get-model)", DiagnosticCode::kInvalidState},
      Case{"(set-logic ALL)(check-sat)(get-model)",
           DiagnosticCode::kInvalidState},
      Case{"(set-info :category crafted)", DiagnosticCode::kInvalidInput},
      Case{"(set-info :category \"other\")", DiagnosticCode::kInvalidInput},
      Case{"(set-info :source source)", DiagnosticCode::kInvalidInput},
      Case{"(set-info :license CC)", DiagnosticCode::kInvalidInput},
      Case{"(set-info :status arbitrary)", DiagnosticCode::kInvalidInput},
      Case{"(set-info :status |sat|)", DiagnosticCode::kInvalidInput},
      Case{"(set-info :smt-lib-version \"2.7\")",
           DiagnosticCode::kInvalidInput},
      Case{"(set-info :smt-lib-version 2.8)", DiagnosticCode::kUnsupported},
      Case{"(set-option :print-success 0)", DiagnosticCode::kSyntax},
      Case{"(set-option :produce-models |true|)", DiagnosticCode::kSyntax},
      Case{"(set-option :ignored true)", DiagnosticCode::kUnsupported},
      Case{"(set-option :regular-output-channel \"log\")",
           DiagnosticCode::kUnsupported},
  };
  for (const auto& [input, code] : cases) {
    SCOPED_TRACE(input);
    auto session = Require(Session::FromText(input));
    auto command = Next(session);
    while (command && *command) command = Next(session);
    ExpectError(command, code);
  }
}

TEST(FrontendCommands, FixedFalseOptionsGettersAndHarmlessUnsupportedQueries) {
  constexpr std::array fixed{
      ":interactive-mode",          ":produce-assertions",
      ":produce-assignments",       ":produce-proofs",
      ":produce-unsat-assumptions", ":produce-unsat-cores"};
  for (const std::string_view name : fixed) {
    SCOPED_TRACE(name);
    const auto input =
        std::string("(set-option :print-success true)(set-option ") +
        std::string(name) + " false)(get-option " + std::string(name) +
        ")(set-logic ALL)(set-option " + std::string(name) + " false)";
    auto session = Require(Session::FromText(input));
    RequireNext(session, CommandKind::kSetOption);
    EXPECT_TRUE(session.GetOptions().print_success);
    RequireNext(session, CommandKind::kSetOption);
    EXPECT_EQ(RequireNext(session, CommandKind::kGetOption).text, "false");
    RequireNext(session, CommandKind::kSetLogic);
    ExpectError(Next(session), DiagnosticCode::kInvalidState);
    const auto enabling =
        std::string("(set-option ") + std::string(name) + " true)";
    auto failed = Require(Session::FromText(enabling));
    ExpectError(Next(failed), DiagnosticCode::kUnsupported);
  }

  auto session = Require(Session::FromText(
      "(get-option :print-success)(get-option :produce-models)"
      "(get-option :global-declarations)(get-option :random-seed)"
      "(get-option :reproducible-resource-limit)(get-option :verbosity)"
      "(get-option :regular-output-channel)(get-option "
      ":diagnostic-output-channel)"
      "(get-option :custom)(get-info :custom)(get-info :name)(get-info "
      ":version)"
      "(get-info :authors)(get-info :error-behavior)(set-logic "
      "ALL)(check-sat)"));
  for (const auto* expected : {"false", "false", "false", "0", "0", "0",
                               "\"stdout\"", "\"stderr\"", "unsupported"})
    EXPECT_EQ(RequireNext(session, CommandKind::kGetOption).text, expected);
  for (const auto* expected :
       {"unsupported", "(:name \"Dandelion\")", "(:version \"development\")",
        "(:authors \"\")", "(:error-behavior immediate-exit)"})
    EXPECT_EQ(RequireNext(session, CommandKind::kGetInfo).text, expected);
  RequireNext(session, CommandKind::kSetLogic);
  EXPECT_TRUE(RequireCheck(session));
}

TEST(FrontendCommands, SyntacticallyValidExcludedCommandsAreUnsupported) {
  for (const std::string_view input :
       {"(check-sat-assuming (a (not b)))", "(push 0)", "(pop 0)", "(reset)",
        "(reset-assertions)", "(declare-sort-parameter T)",
        "(define-fun-rec f ((x Bool)) Bool (f x))",
        "(define-funs-rec ((f ((x Bool)) Bool)) ((f x)))", "(get-assertions)",
        "(get-proof)", "(get-unsat-core)", "(get-assignment)",
        "(get-unsat-assumptions)", "(unregistered-command a)"}) {
    SCOPED_TRACE(input);
    auto session = Require(Session::FromText(input));
    ASSERT_TRUE(Require(session.NextSyntax()));
    ExpectError(session.Apply(), DiagnosticCode::kUnsupported);
  }
}

TEST(FrontendCommands, SyntaxPendingAndRecoveringAfterFailedApply) {
  auto session = Require(Session::FromText(
      "(set-logic ALL)(set-logic QF_SAT)(declare-const x Bool)(check-sat)"));
  ExpectError(session.Apply(), DiagnosticCode::kInvalidState);
  ASSERT_TRUE(Require(session.NextSyntax()));
  EXPECT_EQ(session.GetPhase(), SessionPhase::kBeforeLogic);
  ExpectError(session.NextSyntax(), DiagnosticCode::kInvalidState);
  Require(session.Apply());
  EXPECT_EQ(session.GetPhase(), SessionPhase::kBuilding);
  ExpectError(Next(session), DiagnosticCode::kInvalidState);
  RequireNext(session, CommandKind::kDeclareConst);
  const auto query = RequireCheck(session);
  ASSERT_TRUE(query);
  EXPECT_EQ(query->GetLogic(), "ALL");
  EXPECT_EQ(query->GetFunctions().size(), 1);
}

TEST(FrontendCommands, AllocationFailuresLeaveEachPublicationBoundaryAtomic) {
  const auto sweep = [](std::string_view prefix, std::string_view command,
                        CommandKind kind, auto inspect) {
    const auto input =
        std::string(prefix) + std::string(command) + std::string(command);
    SCOPED_TRACE(::testing::Message() << "FE-ROLLBACK-02 " << input);
    for (std::size_t checkpoint = 0; checkpoint < 64; ++checkpoint) {
      SCOPED_TRACE(checkpoint);
      auto session = Require(Session::FromText(input));
      const auto pending_target = [&] {
        for (;;) {
          const auto header = Require(session.NextSyntax());
          if (!header) throw std::runtime_error("Missing rollback fixture");
          if (header->where.begin.offset >= prefix.size()) {
            EXPECT_EQ(header->kind, kind);
            break;
          }
          Require(session.Apply());
        }
      };
      pending_target();
      const auto before = inspect(session, nullptr);
      session.FailAllocationAfter(checkpoint);
      const auto first = session.Apply();
      if (first) {
        EXPECT_GT(checkpoint, 0u) << "No publication checkpoint was exercised";
        inspect(session, &*first);
        return;
      }
      ExpectError(first, DiagnosticCode::kResource);
      ASSERT_TRUE(first.error().node_error);
      EXPECT_EQ(first.error().node_error->resource,
                ResourceKind::kCppAllocation);
      EXPECT_EQ(inspect(session, nullptr), before);
      ExpectError(session.Apply(), DiagnosticCode::kInvalidState);
      const auto retry = RequireNext(session, kind);
      inspect(session, &retry);
      EXPECT_FALSE(Require(session.NextSyntax()));
    }
    ADD_FAILURE() << "Allocation checkpoint sweep never reached success";
  };

  sweep("(set-option :produce-models true)", "(set-option :print-success true)",
        CommandKind::kSetOption,
        [](const Session& session, const TypedCommand* committed) {
          const auto& options = session.GetOptions();
          EXPECT_EQ(session.GetPhase(), SessionPhase::kBeforeLogic);
          EXPECT_EQ(options.print_success, committed != nullptr);
          EXPECT_TRUE(options.produce_models);
          EXPECT_FALSE(options.global_declarations);
          if (committed) {
            EXPECT_EQ(committed->text, "success");
          }
          return options.print_success;
        });
  sweep("(set-info :tag first)(set-info :tag second)", "(set-info :tag third)",
        CommandKind::kSetInfo,
        [](const Session& session, const TypedCommand* committed) {
          const auto metadata = session.GetMetadata();
          const std::array expected{"first", "second", "third"};
          EXPECT_EQ(metadata.size(), committed ? 3u : 2u);
          for (std::size_t i = 0;
               i < std::min(metadata.size(), expected.size()); ++i) {
            EXPECT_EQ(metadata[i].key, ":tag");
            EXPECT_EQ(metadata[i].value, expected[i]);
          }
          return metadata.size();
        });
  sweep("(set-info :tag logic)", "(set-logic ALL)", CommandKind::kSetLogic,
        [](const Session& session, const TypedCommand* committed) {
          EXPECT_EQ(session.GetPhase(), committed ? SessionPhase::kBuilding
                                                  : SessionPhase::kBeforeLogic);
          EXPECT_EQ(session.GetPrefix().GetLogic(), committed ? "ALL" : "");
          EXPECT_EQ(session.GetMetadata().size(), 1);
          return session.GetPhase();
        });
  sweep(
      "(set-option :produce-models true)(set-info :tag first)"
      "(set-logic ALL)(assert false)(set-info :tag second)",
      "(check-sat)", CommandKind::kCheckSat,
      [](const Session& session, const TypedCommand* committed) {
        const auto& prefix = session.GetPrefix();
        EXPECT_EQ(session.GetPhase(), committed ? SessionPhase::kAfterCheck
                                                : SessionPhase::kBuilding);
        EXPECT_EQ(prefix.GetLogic(), "ALL");
        EXPECT_EQ(prefix.GetAssertions().size(), 1);
        const auto check_metadata = [](std::span<const Attribute> metadata,
                                       std::size_t expected) {
          ASSERT_EQ(metadata.size(), expected);
          if (metadata.empty()) return;
          EXPECT_EQ(metadata[0].value, "first");
          EXPECT_EQ(metadata[1].value, "second");
        };
        check_metadata(session.GetMetadata(), 2);
        check_metadata(prefix.GetMetadata(), committed ? 2u : 0u);
        if (committed) {
          EXPECT_EQ(committed->query.get(), &prefix);
          EXPECT_TRUE(prefix.GetOptions().produce_models);
        }
        if (prefix.GetAssertions().empty()) return NodeView{};
        const auto root = prefix.GetAssertions()[0].term.Borrow();
        EXPECT_EQ(root.GetKind(), Kind::kBoolValue);
        if (const auto value = root.GetValue()) {
          EXPECT_EQ(value->GetBool(), false);
        }
        return root;
      });

  sweep("(set-logic ALL)(declare-sort Prior 1)", "(declare-sort Fresh 0)",
        CommandKind::kDeclareSort,
        [](const Session& session, const TypedCommand* committed) {
          const auto sorts = session.GetPrefix().GetUserSorts();
          EXPECT_EQ(sorts.size(), committed ? 2u : 1u);
          if (sorts.empty()) return SortConstructorView{};
          EXPECT_EQ(sorts[0].source_name, "Prior");
          EXPECT_EQ(sorts[0].constructor.Borrow().GetArity(), std::uint64_t{1});
          if (committed && sorts.size() >= 2) {
            EXPECT_EQ(sorts[1].source_name, "Fresh");
            EXPECT_EQ(sorts[1].constructor.Borrow().GetArity(),
                      std::uint64_t{0});
          }
          return sorts[0].constructor.Borrow();
        });
  sweep("(set-logic ALL)(declare-const prior Bool)",
        "(declare-fun fresh (Bool Int) Bool)", CommandKind::kDeclareFun,
        [](const Session& session, const TypedCommand* committed) {
          const auto& prefix = session.GetPrefix();
          const auto functions = prefix.GetFunctions();
          EXPECT_EQ(functions.size(), committed ? 2u : 1u);
          if (functions.empty()) return DeclView{};
          EXPECT_EQ(functions[0].source_name, "prior");
          EXPECT_TRUE(prefix.HasFunction(functions[0].decl.Borrow()));
          if (committed && functions.size() >= 2) {
            EXPECT_EQ(functions[1].source_name, "fresh");
            EXPECT_TRUE(prefix.HasFunction(functions[1].decl.Borrow()));
            EXPECT_EQ(functions[1].source_rank,
                      (std::vector<std::string>{"Bool", "Int", "Bool"}));
          }
          return functions[0].decl.Borrow();
        });
}

TEST(FrontendCommands, InvalidDefinitionResultRollsBackInnerAnnotation) {
  constexpr std::string_view input =
      "(set-logic ALL)(define-const prior Bool true)"
      "(define-fun f ((x Int)) Int (and (! prior :named staged) true))"
      "(assert staged)"
      "(define-fun f ((x Int)) Bool (and (! prior :named staged) true))"
      "(assert (and staged (f 7)))(check-sat)";
  SCOPED_TRACE(::testing::Message() << "FE-ROLLBACK-02 " << input);
  auto session = Require(Session::FromText(input));
  RequireNext(session, CommandKind::kSetLogic);
  RequireNext(session, CommandKind::kDefineConst);
  const auto prior = session.GetPrefix().GetDefinitions()[0].decl;
  for (int i = 0; i < 2; ++i) {
    ExpectError(Next(session), DiagnosticCode::kInvalidInput);
    const auto& prefix = session.GetPrefix();
    ASSERT_EQ(prefix.GetDefinitions().size(), 1);
    EXPECT_EQ(prefix.GetDefinitions()[0].decl, prior);
    EXPECT_TRUE(prefix.GetAnnotations().empty());
    EXPECT_TRUE(prefix.GetNamedTerms().empty());
    EXPECT_TRUE(prefix.GetAssertions().empty());
  }
  RequireNext(session, CommandKind::kDefineFun);
  RequireNext(session, CommandKind::kAssert);
  const auto query = RequireCheck(session);
  ASSERT_EQ(query->GetDefinitions().size(), 2);
  ASSERT_EQ(query->GetAnnotations().size(), 1);
  ASSERT_EQ(query->GetNamedTerms().size(), 1);
  ASSERT_EQ(query->GetAssertions().size(), 1);
  EXPECT_EQ(query->GetDefinitions()[0].decl, prior);
  EXPECT_EQ(query->GetDefinitions()[1].source_name, "f");
  EXPECT_EQ(query->GetNamedTerms()[0].name, "staged");
  EXPECT_EQ(query->GetAnnotations()[0].term, query->GetNamedTerms()[0].term);
}

}  // namespace
}  // namespace dandelion::frontend::testing

#include "test_support.h"

namespace dandelion::frontend::testing {
namespace {

struct ProfileCase {
  // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
  std::string_view name;
  std::string_view components;
  char declarations;
  // NOLINTEND(readability-identifier-naming)
};

class LogicProfile : public ::testing::TestWithParam<ProfileCase> {};

TEST_P(LogicProfile, SignatureAndDeclarationPermissions) {
  const auto [name, components, declarations] = GetParam();
  const auto prelude = "(set-logic " + std::string(name) + ")";
  const auto parse = [&](std::string_view body) {
    return Parse(prelude + std::string(body) + "(check-sat)");
  };
  std::string combined =
      "(declare-const c Bool)(declare-fun d () Bool)"
      "(define-fun neg ((v Bool)) Bool (not v))(assert (= (neg c) d))";
  const auto assert_control = [&](char gate, std::string_view fragment) {
    if (components.contains(gate))
      combined.append(fragment);
    else
      ExpectError(parse(fragment), DiagnosticCode::kUnsupported);
  };
  assert_control('B', "(assert (= (bvnot #x00) #xff))");
  assert_control('I',
                 "(declare-const i Int)(assert (= (** (* i i) 2) (+ i i 1)))");
  assert_control('A',
                 "(declare-const a (Array Bool (Array Bool Bool)))"
                 "(assert (select (select a true) false))");
  assert_control('D',
                 "(declare-datatype D ((enum)(box (p Bool))))"
                 "(assert ((_ is box) (box true)))");
  assert_control('F',
                 "(declare-const fp0 Float16)(declare-const rm RoundingMode)"
                 "(assert (fp.eq fp0 ((_ to_fp 5 11) rm (/ 1 2))))");
  if (components.contains('J')) {
    combined += "(declare-const j Int)(assert (<= (+ j j) (- j 4)))";
    ExpectError(parse("(assert (= (- 1) 0))"), DiagnosticCode::kUnsupported);
    ExpectError(parse("(assert (< 0 1 2))"), DiagnosticCode::kUnsupported);
  } else if (!components.contains('I')) {
    ExpectError(parse("(declare-const i Int)"), DiagnosticCode::kUnsupported);
  }
  if (components.contains('R') || components.contains('F'))
    ExpectError(parse("(declare-const r Real)"), DiagnosticCode::kUnsupported);
  if (components.contains('X'))
    ExpectError(parse("(assert (= (to_real 2) 2))"),
                DiagnosticCode::kUnsupported);
  if (declarations == 'C') {
    ExpectError(parse("(declare-sort U 1)"), DiagnosticCode::kUnsupported);
  } else {
    combined += "(declare-sort U 1)(declare-const uc (U Bool))";
  }
  if (declarations != 'U') {
    ExpectError(parse("(declare-fun f (Bool) Bool)"),
                DiagnosticCode::kUnsupported);
  } else {
    combined += "(declare-fun f (Bool (U Bool)) Bool)(assert (f c uc))";
  }

  SCOPED_TRACE(::testing::Message() << "FE-PROFILE-02 " << prelude << combined);
  const auto query = RequireQuery(parse(combined));
  EXPECT_EQ(query->GetLogic(), name);
  ASSERT_EQ(query->GetDefinitionCount(), 1);
  const auto roots = query->GetAssertions();
  const std::array expected{
      std::pair{'\0', Kind::kEqual}, std::pair{'B', Kind::kEqual},
      std::pair{'I', Kind::kEqual},  std::pair{'A', Kind::kArraySelect},
      std::pair{'D', Kind::kApply},  std::pair{'F', Kind::kFpEq},
      std::pair{'J', Kind::kIntLe},  std::pair{'U', Kind::kApply}};
  std::size_t index = 0;
  for (const auto& [gate, kind] : expected) {
    if (gate == 'U' ? declarations != 'U'
                    : gate != '\0' && !components.contains(gate))
      continue;
    ASSERT_LT(index, roots.size());
    const auto root = roots[index++].Borrow();
    ASSERT_EQ(root.GetKind(), kind);
    EXPECT_EQ(root.GetSort().GetKind(), SortKind::kBool);
    const auto children = root.GetChildren();
    ASSERT_FALSE(children.empty());
    switch (gate) {
      case '\0':
        EXPECT_EQ(Applied(children[0]), query->GetDefinition(0).decl.Borrow());
        break;
      case 'B':
        EXPECT_EQ(children[0].GetKind(), Kind::kBvNot);
        break;
      case 'I':
        ASSERT_EQ(children.size(), 2);
        ASSERT_EQ(children[0].GetKind(), Kind::kIntPow);
        ASSERT_EQ(children[0].GetChildren().size(), 2);
        EXPECT_EQ(children[0].GetChildren()[0].GetKind(), Kind::kIntMul);
        EXPECT_EQ(children[1].GetKind(), Kind::kIntAdd);
        break;
      case 'A':
        EXPECT_EQ(children[0].GetKind(), Kind::kArraySelect);
        break;
      case 'D':
        ASSERT_EQ(children[0].GetKind(), Kind::kApply);
        EXPECT_EQ(Applied(root).GetKind(), DeclKind::kDatatypeTester);
        EXPECT_EQ(Applied(root).GetConstructor(), Applied(children[0]));
        EXPECT_EQ(Applied(children[0]).GetNameHint(), "box");
        EXPECT_FALSE(query->HasFunction(Applied(root)));
        break;
      case 'U':
        ASSERT_EQ(children.size(), 2);
        EXPECT_EQ(Applied(root).GetKind(), DeclKind::kFunction);
        ASSERT_FALSE(query->GetFunctions().empty());
        EXPECT_EQ(Applied(root), query->GetFunctions().back().Borrow());
        EXPECT_TRUE(query->HasFunction(Applied(root)));
        break;
      default:
        break;
    }
  }
  EXPECT_EQ(roots.size(), index);
}

INSTANTIATE_TEST_SUITE_P(
    Registered, LogicProfile,
    ::testing::ValuesIn(
        std::to_array<ProfileCase>({ProfileCase{"QF_SAT", "", 'C'},
                                    {"QF_BV", "B", 'C'},
                                    {"QF_UFBV", "B", 'U'},
                                    {"QF_ABV", "AB", 'C'},
                                    {"QF_AUFBV", "AB", 'U'},
                                    {"QF_AX", "A", 'S'},
                                    {"QF_UF", "", 'U'},
                                    {"QF_IDL", "I", 'C'},
                                    {"QF_UFIDL", "J", 'U'},
                                    {"QF_LIA", "I", 'C'},
                                    {"QF_UFLIA", "I", 'U'},
                                    {"QF_AUFLIA", "AI", 'U'},
                                    {"QF_NIA", "I", 'C'},
                                    {"QF_EIA", "I", 'C'},
                                    {"QF_DT", "D", 'C'},
                                    {"QF_UFDT", "D", 'U'},
                                    {"QF_UFBVDT", "BD", 'U'},
                                    {"QF_FP", "F", 'C'},
                                    {"QF_UFFP", "F", 'U'},
                                    {"QF_FPLRA", "FR", 'C'},
                                    {"QF_BVFP", "BF", 'C'},
                                    {"QF_BVFPLRA", "BFR", 'C'},
                                    {"QF_ABVFP", "ABF", 'C'},
                                    {"QF_AUFBVFP", "ABF", 'U'},
                                    {"QF_ABVFPLRA", "ABFR", 'C'},
                                    {"QF_ALIA", "AI", 'C'},
                                    {"QF_ANIA", "AI", 'C'},
                                    {"QF_AUFNIA", "AI", 'U'},
                                    {"QF_UFNIA", "I", 'U'},
                                    {"QF_UFDTLIA", "DI", 'U'},
                                    {"QF_UFDTNIA", "DI", 'U'},
                                    {"QF_UFDTLIRA", "DIRX", 'U'},
                                    {"QF_UFBVLIA", "BI", 'U'},
                                    {"QF_AUFBVLIA", "ABI", 'U'},
                                    {"QF_AUFBVNIA", "ABI", 'U'},
                                    {"QF_LIRA", "IRX", 'C'},
                                    {"QF_NIRA", "IRX", 'C'},
                                    {"QF_UFFPDTNIRA", "DFIRX", 'U'},
                                    {"QF_BVLRA", "BR", 'C'},
                                    {"QF_UFLRA", "R", 'U'},
                                    {"QF_UFNRA", "R", 'U'},
                                    {"QF_LRA", "R", 'C'},
                                    {"QF_NRA", "R", 'C'},
                                    {"QF_RDL", "R", 'C'},
                                    {"ALL", "ABDFI", 'U'}})),
    [](const ::testing::TestParamInfo<ProfileCase>& parameter) {
      return std::string(parameter.param.name);
    });

TEST(FrontendProfiles, InactiveSpellingCanHaveUserIdentity) {
  const auto query = RequireQuery(
      Parse("(set-logic QF_UF)(declare-sort Int 0)(declare-sort Array 2)"
            "(declare-const x (Array Int Bool))(declare-fun select ((Array Int "
            "Bool)) Bool)"
            "(assert (select x))(check-sat)"));
  ASSERT_EQ(query->GetFunctions().size(), 2);
  const auto assertion = query->GetAssertions()[0].Borrow();
  EXPECT_EQ(Applied(assertion), query->GetFunctions()[1].Borrow());
  const auto arg_sort = assertion.GetChildren()[0].GetSort();
  EXPECT_EQ(arg_sort.GetKind(), SortKind::kUninterpreted);
  EXPECT_EQ(arg_sort.GetSortConstructor(), query->GetUserSorts()[1].Borrow());
  EXPECT_EQ(arg_sort.GetArguments()[0].GetSortConstructor(),
            query->GetUserSorts()[0].Borrow());
  EXPECT_EQ(arg_sort.GetArguments()[1].GetKind(), SortKind::kBool);

  const auto div = Require(Values("(div 4 0)",
                                  "(set-logic QF_UFIDL)"
                                  "(declare-fun |div| (Int Int) Int)"));
  EXPECT_EQ(Applied(div.values[0].term.Borrow()).GetKind(),
            DeclKind::kFunction);
  ExpectError(
      Values("(div 4)", "(set-logic QF_UFIDL)(declare-fun div (Int Int) Int)"),
      DiagnosticCode::kInvalidInput);
  ExpectError(Values("(div 4 0)", "(set-logic QF_UFIDL)"),
              DiagnosticCode::kUnsupported);
  const auto ints = Require(Values("(div 4 0)", "(set-logic QF_UFLIA)"));
  EXPECT_EQ(ints.values[0].term.Borrow().GetKind(), Kind::kIntDiv);
  ExpectError(Parse("(set-logic QF_UFLIA)(declare-fun |div| (Int Int) Int)"),
              DiagnosticCode::kInvalidInput);

  EXPECT_EQ(Applied(Require(Values("(bvadd #b0 #b1)",
                                   "(set-logic QF_UFFP)"
                                   "(declare-fun bvadd ((_ BitVec 1) (_ BitVec "
                                   "1)) (_ BitVec 1))"))
                        .values[0]
                        .term.Borrow())
                .GetKind(),
            DeclKind::kFunction);
  ExpectError(Parse("(set-logic QF_UFBV)(declare-fun bvadd (Bool) Bool)"),
              DiagnosticCode::kInvalidInput);
  ExpectError(Parse("(set-logic QF_UFFP)(declare-sort Real 0)"),
              DiagnosticCode::kInvalidInput);
  ExpectError(Parse("(set-logic QF_UFFP)(declare-const fp.to_real Bool)"),
              DiagnosticCode::kInvalidInput);
  ExpectError(Parse("(set-logic QF_UFLRA)(declare-fun |/| (Bool) Bool)"),
              DiagnosticCode::kInvalidInput);
  ExpectError(Values("(to_real 1)"), DiagnosticCode::kUnsupported);
  EXPECT_EQ(Applied(Require(Values("(to_real 1)",
                                   "(set-logic ALL)"
                                   "(declare-fun to_real (Int) Int)"))
                        .values[0]
                        .term.Borrow())
                .GetKind(),
            DeclKind::kFunction);
  ExpectError(Values("(f 1)"), DiagnosticCode::kInvalidInput);
  ExpectError(Values("(str.len \"a\")"), DiagnosticCode::kUnsupported);
}

TEST(FrontendProfiles, ArrayCombinationPreservesInnerSortAndMixedBridges) {
  const auto a = Require(Values("(select (store a #b0 (select a #b0)) #b0)",
                                "(set-logic QF_AUFBV)"
                                "(declare-const a (Array (_ BitVec 1) (Array "
                                "(_ BitVec 1) (_ BitVec 1))))"));
  const auto out = a.values[0].term.Borrow();
  EXPECT_EQ(out.GetKind(), Kind::kArraySelect);
  const auto sort = out.GetSort();
  EXPECT_EQ(sort.GetKind(), SortKind::kArray);
  EXPECT_EQ(sort.GetArrayIndex()->GetWidth(), 1u);
  EXPECT_EQ(sort.GetArrayElement()->GetWidth(), 1u);
  ExpectError(Values("(select a #b0)",
                     "(set-logic QF_UFBV)"
                     "(declare-const a (Array (_ BitVec 1) (_ BitVec 1)))"),
              DiagnosticCode::kUnsupported);
  for (auto label : {"QF_UFBVLIA", "QF_AUFBVLIA", "QF_AUFBVNIA", "ALL"}) {
    const auto command =
        Require(Values("(sbv_to_int ((_ int_to_bv 4) 17))",
                       "(set-logic " + std::string(label) + ")"));
    EXPECT_EQ(command.values[0].term.Borrow().GetKind(), Kind::kSbvToInt);
    EXPECT_EQ(Argument(command.values[0].term, 0).GetSort().GetWidth(), 4u);
  }
  for (auto label : {"QF_BV", "QF_BVFP", "QF_LIA", "QF_UFIDL"}) {
    ExpectError(
        Values("(ubv_to_int #b1)", "(set-logic " + std::string(label) + ")"),
        DiagnosticCode::kUnsupported);
  }
}

TEST(FrontendProfiles, DivRankInStandaloneAssertionAndValueRequest) {
  constexpr std::string_view setup =
      "(set-logic QF_UFLIA)(declare-const x Int)";
  constexpr std::string_view assertion = "(assert (= (div x 2) 3))";
  SCOPED_TRACE(::testing::Message() << "FE-PROFILE-02 " << setup << assertion);
  const auto assertion_query = RequireQuery(
      Parse(std::string(setup) + std::string(assertion) + "(check-sat)"));
  std::shared_ptr<const QueryContext> value_query;
  const auto request = Require(Values("(div x 2)", setup, &value_query));
  const auto check = [](NodeView term, const QueryContext& query) {
    ASSERT_EQ(query.GetFunctions().size(), 1);
    ASSERT_EQ(term.GetKind(), Kind::kIntDiv);
    ASSERT_EQ(term.GetChildren().size(), 2);
    const auto lhs = term.GetChildren()[0];
    const auto rhs = term.GetChildren()[1];
    EXPECT_EQ(Applied(lhs), query.GetFunctions()[0].Borrow());
    EXPECT_EQ(term.GetSort().GetKind(), SortKind::kInt);
    EXPECT_EQ(lhs.GetSort(), term.GetSort());
    EXPECT_EQ(rhs.GetSort(), term.GetSort());
    EXPECT_EQ(IntegerValue(rhs), "2");
  };
  ASSERT_EQ(assertion_query->GetAssertions().size(), 1);
  const auto& root = assertion_query->GetAssertions()[0];
  ASSERT_EQ(root.Borrow().GetKind(), Kind::kEqual);
  ASSERT_EQ(root.Borrow().GetChildren().size(), 2);
  check(Argument(root, 0), *assertion_query);
  EXPECT_EQ(IntegerValue(Argument(root, 1)), "3");
  ASSERT_EQ(request.values.size(), 1);
  check(request.values[0].term.Borrow(), *value_query);
  ExpectError(Parse("(set-logic QF_UFLIA)(declare-fun div (Int Int) Int)"),
              DiagnosticCode::kInvalidInput);

  const auto uf = Require(Values("(div x 2)",
                                 "(set-logic QF_UFIDL)(declare-const x Int)"
                                 "(declare-fun |div| (Int Int) Int)",
                                 &value_query));
  ASSERT_EQ(value_query->GetFunctions().size(), 2);
  const auto call = uf.values.at(0).term.Borrow();
  ASSERT_EQ(call.GetKind(), Kind::kApply);
  EXPECT_EQ(Applied(call), value_query->GetFunctions()[1].Borrow());
  ASSERT_EQ(call.GetChildren().size(), 2);
  EXPECT_EQ(Applied(call.GetChildren()[0]),
            value_query->GetFunctions()[0].Borrow());
  EXPECT_EQ(IntegerValue(call.GetChildren()[1]), "2");
}

TEST(FrontendProfiles, RealIntNamesFollowProfileAndOrdinaryUserRanks) {
  constexpr std::array calls{"(to_int true)", "(is_int 2)"};
  constexpr std::array declarations{"(declare-fun to_int (Bool) Int)",
                                    "(declare-fun is_int (Int) Bool)"};
  for (const auto logic : {"QF_LIRA", "QF_NIRA", "QF_UFDTLIRA", "ALL"}) {
    const std::string setup = "(set-logic " + std::string(logic) + ")";
    for (const auto input : calls) {
      SCOPED_TRACE(::testing::Message()
                   << "FE-PROFILE-RI-02 " << setup << input);
      ExpectError(Values(input, setup), DiagnosticCode::kUnsupported);
    }
  }
  for (const auto input : declarations) {
    SCOPED_TRACE(::testing::Message() << "FE-PROFILE-RI-02 " << input);
    ExpectError(Parse(std::string("(set-logic QF_UFDTLIRA)") + input),
                DiagnosticCode::kInvalidInput);
  }
  const auto setup =
      std::string("(set-logic ALL)") + declarations[0] + declarations[1];
  SCOPED_TRACE(::testing::Message() << "FE-PROFILE-RI-02 " << setup);
  std::shared_ptr<const QueryContext> query;
  const auto typed =
      Require(Values("(to_int true) (is_int 2) true 2", setup, &query));
  ASSERT_EQ(typed.values.size(), 4);
  ASSERT_EQ(query->GetFunctions().size(), 2);
  for (std::size_t index = 0; index < calls.size(); ++index) {
    const auto term = typed.values[index].term.Borrow();
    ASSERT_EQ(term.GetKind(), Kind::kApply);
    EXPECT_EQ(Applied(term), query->GetFunctions()[index].Borrow());
    ExpectChildren(term, {typed.values[index + 2].term.Borrow()});
    EXPECT_EQ(term.GetSort().GetKind(),
              index == 0 ? SortKind::kInt : SortKind::kBool);
  }
  ExpectError(Values("(to_int 2)", setup), DiagnosticCode::kInvalidInput);
  ExpectError(Values("(is_int true)", setup), DiagnosticCode::kInvalidInput);
}

TEST(FrontendProfiles, InactiveBvOperatorWithAdmittedFpProfileOperands) {
  for (const auto& [input, diagnostic] :
       {std::pair{"((_ extract 0 0) #b1)", DiagnosticCode::kUnsupported},
        {"((_ extract invalid 0) #b1)", DiagnosticCode::kInvalidInput}}) {
    SCOPED_TRACE(::testing::Message() << "FE-PROFILE-02 " << input);
    ExpectError(Values(input, "(set-logic QF_FP)"), diagnostic);
  }
  const auto control = Require(Values("#b1", "(set-logic QF_FP)"));
  EXPECT_EQ(control.values.at(0).term.Borrow().GetSort().GetWidth(), 1u);
}

}  // namespace
}  // namespace dandelion::frontend::testing

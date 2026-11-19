#include "test_support.h"

namespace dandelion::frontend::testing {
namespace {

TEST(FrontendSorts, ArrayShapePrecedesProfileGatesOnArguments) {
  for (auto declaration : {"(declare-const x (Array Real))",
                           "(declare-const x (Array Real Bool Bool))"}) {
    const auto script = "(set-logic QF_SAT)" + std::string(declaration);
    SCOPED_TRACE(::testing::Message() << "FE-PROFILE-SHAPE-02 " << script);
    ExpectError(Parse(script), DiagnosticCode::kInvalidInput);
  }
  ExpectError(Parse("(set-logic QF_SAT)(declare-const x (Array Bool Bool))"),
              DiagnosticCode::kUnsupported);
  const auto query =
      RequireQuery(Parse("(set-logic QF_SAT)(define-sort Array (T) T)"
                         "(declare-const x (Array Bool))(check-sat)"));
  EXPECT_EQ(query->GetFunctions()[0].decl.Borrow().GetRange()->GetKind(),
            SortKind::kBool);
}

TEST(FrontendDatatypes, HeaderAndBodyCountsPrecedeProfileGating) {
  for (auto declaration : {"(declare-datatypes ((A 0) (B 0)) (((a))))",
                           "(declare-datatypes ((A 0)) (((a)) ((b))))"}) {
    const auto script = "(set-logic QF_SAT)" + std::string(declaration);
    SCOPED_TRACE(::testing::Message() << "FE-PROFILE-SHAPE-02 " << script);
    ExpectError(Parse(script), DiagnosticCode::kInvalidInput);
  }
  ExpectError(Parse("(set-logic QF_SAT)(declare-datatypes ((A 0)) (((a))))"),
              DiagnosticCode::kUnsupported);
}

TEST(FrontendSorts, AliasParametersOrderedRanksAndFpAliases) {
  const auto query = RequireQuery(Parse(
      "(set-logic ALL)(declare-sort U 2)(declare-sort S 0)"
      "(define-sort Swap (A B) (U B A))(define-sort R () RoundingMode)"
      "(declare-const same (Swap Int Bool))(declare-const direct (U Bool Int))"
      "(declare-const other (U Int Bool))(declare-const S S)"
      "(declare-const a Float16)(declare-const b Float32)"
      "(declare-const c Float64)(declare-const d Float128)"
      "(declare-const t (_ FloatingPoint 8 24))(declare-const m R)"
      "(declare-const ar (Array S (U Float16 R)))"
      "(assert (= same direct))(assert (= b t))(check-sat)"));
  const auto fs = query->GetFunctions();
  const auto range = [&](std::size_t i) {
    return *fs[i].decl.Borrow().GetRange();
  };
  EXPECT_EQ(range(0), range(1));
  EXPECT_NE(range(0), range(2));
  EXPECT_EQ(range(0).GetArguments()[0].GetKind(), SortKind::kBool);
  EXPECT_EQ(range(0).GetArguments()[1].GetKind(), SortKind::kInt);
  EXPECT_EQ(range(3).GetSortConstructor(),
            query->GetUserSorts()[1].constructor.Borrow());
  constexpr std::array<std::pair<unsigned, unsigned>, 4> formats{
      {{5, 11}, {8, 24}, {11, 53}, {15, 113}}};
  for (std::size_t i = 0; i < formats.size(); ++i) {
    EXPECT_EQ(range(i + 4).GetFpExponentWidth(), formats[i].first);
    EXPECT_EQ(range(i + 4).GetFpSignificandWidth(), formats[i].second);
  }
  EXPECT_EQ(range(5), range(8));
  EXPECT_EQ(range(9).GetKind(), SortKind::kRoundingMode);
  const auto args = range(10).GetArrayElement()->GetArguments();
  EXPECT_EQ(args[0], range(4));
  EXPECT_EQ(args[1], range(9));

  for (auto invalid :
       {"(define-sort S () S)", "(define-sort S (A |A|) A)",
        "(define-sort S (Int) Int)", "(define-sort S (T) (T Int))",
        "(declare-sort U 1)(declare-const x U)",
        "(declare-sort U 0)(declare-const x (U Int))",
        "(define-sort S (A B) A)(declare-const x (S Int))",
        "(declare-const x (_ BitVec w))", "(declare-const x (_ BitVec 0))",
        "(declare-const x (_ FloatingPoint 1 3))",
        "(declare-const x (_ FloatingPoint 3 1))",
        "(declare-const x (Array Int))",
        "(declare-const x Int)(declare-const |x| Int)",
        "(declare-sort |Bool| 0)", "(declare-sort @S 0)",
        "(declare-const |.hidden| Bool)", "(assert #b1)"}) {
    SCOPED_TRACE(invalid);
    ExpectError(Parse("(set-logic ALL)" + std::string(invalid)),
                DiagnosticCode::kInvalidInput);
  }
  for (auto excluded :
       {"(declare-const x String)", "(declare-const x Real)",
        "(declare-const x (-> Int Bool))", "(declare-sort-parameter T)",
        "(define-sort Drop (T) Bool)(declare-const x (Drop Real))"}) {
    SCOPED_TRACE(excluded);
    ExpectError(Parse("(set-logic ALL)" + std::string(excluded)),
                DiagnosticCode::kUnsupported);
  }
}

TEST(FrontendSorts, RepresentationLimitsAndExactlySizedSorts) {
  static_assert(std::same_as<NodeId, std::uint64_t>);
  static_assert(std::same_as<SortId, std::uint64_t>);
  static_assert(std::same_as<DeclId, std::uint64_t>);
  static_assert(std::same_as<DeclInstanceId, std::uint64_t>);
  static_assert(std::same_as<DatatypeGroupId, std::uint64_t>);
  static_assert(std::same_as<SortConstructorId, std::uint64_t>);
  static_assert(
      std::same_as<decltype(NodeError::operand_index), std::uint64_t>);
  static_assert(
      std::same_as<decltype(NodeError::parameter_index), std::uint64_t>);
  static_assert(
      std::same_as<decltype(DatatypeHeader::parameter_count), std::uint64_t>);
  static_assert(
      std::same_as<decltype(DeclView{}.GetParameterCount()), std::uint64_t>);
  const auto query = RequireQuery(
      Parse("(set-logic ALL)(declare-const b (_ BitVec 4294967295))"
            "(declare-const f (_ FloatingPoint 4294967293 2))"
            "(declare-sort Word 4294967295)(declare-sort Wider 4294967296)"
            "(declare-sort Big 18446744073709551615)(check-sat)"));
  const auto decls = query->GetFunctions();
  constexpr auto max = std::numeric_limits<std::uint32_t>::max();
  EXPECT_EQ(decls[0].decl.Borrow().GetRange()->GetWidth(), max);
  EXPECT_EQ(decls[1].decl.Borrow().GetRange()->GetFpExponentWidth(), max - 2);
  EXPECT_EQ(decls[1].decl.Borrow().GetRange()->GetFpSignificandWidth(), 2u);
  const auto sorts = query->GetUserSorts();
  ASSERT_EQ(sorts.size(), 3);
  EXPECT_EQ(sorts[0].constructor.Borrow().GetArity(),
            std::uint64_t{4294967295});
  EXPECT_EQ(sorts[1].constructor.Borrow().GetArity(),
            std::uint64_t{4294967296});
  const auto big = sorts[2].constructor.Borrow();
  static_assert(std::same_as<decltype(big.GetArity()), std::uint64_t>);
  EXPECT_EQ(big.GetArity(), std::numeric_limits<std::uint64_t>::max());
  for (auto bigger : {"(declare-const b (_ BitVec 4294967296))",
                      "(declare-const f (_ FloatingPoint 4294967294 2))",
                      "(declare-const f (_ FloatingPoint 2 4294967296))",
                      "(declare-sort U 18446744073709551616)"}) {
    SCOPED_TRACE(bigger);
    const auto found = Parse("(set-logic ALL)" + std::string(bigger));
    ExpectError(found, DiagnosticCode::kResource);
    ASSERT_FALSE(found);
    ASSERT_TRUE(found.error().node_error);
    EXPECT_EQ(found.error().node_error->resource, ResourceKind::kSize);
  }
}

TEST(FrontendDatatypes, ParameterizedMutualConstructorsSelectorsAndTesters) {
  const auto ascribed = Require(Values(
      "(as nil (List Int)) (cons 4 (as nil (List Int)))"
      " (cons 5 (as nil (List Int))) (cons #b1 (as nil (List (_ BitVec 1))))"
      " (head (cons 4 (as nil (List Int))))"
      " ((_ is |cons|) (cons 4 (as nil (List Int))))"
      " (branch (children (as tip (Tree Bool))))",
      "(set-logic ALL)"
      "(declare-datatype List (par (T) ((nil)(cons (head T)(tail (List T))))))"
      "(declare-datatypes ((Tree 1)(Forest 1))"
      " ((par (T) ((tip)(branch (kids (Forest T)))))"
      "  (par (T) ((empty)(children (first (Tree T)))))))"));
  const auto& v = ascribed.values;
  const auto list_int = v[0].term.Borrow().GetSort();
  EXPECT_EQ(list_int.GetKind(), SortKind::kDatatype);
  EXPECT_EQ(list_int.GetArguments()[0].GetKind(), SortKind::kInt);
  EXPECT_EQ(v[1].term.Borrow().GetSort(), list_int);
  EXPECT_EQ(v[1].term.Borrow().GetDeclaration(),
            v[2].term.Borrow().GetDeclaration());
  EXPECT_NE(v[1].term.Borrow().GetDeclaration(),
            v[3].term.Borrow().GetDeclaration());
  EXPECT_EQ(v[4].term.Borrow().GetSort().GetKind(), SortKind::kInt);
  EXPECT_EQ(Applied(v[4].term.Borrow()).GetKind(), DeclKind::kDatatypeSelector);
  EXPECT_EQ(Applied(v[5].term.Borrow()).GetKind(), DeclKind::kDatatypeTester);
  EXPECT_EQ(Applied(v[5].term.Borrow()).GetConstructor(),
            Applied(v[1].term.Borrow()));
  EXPECT_TRUE(v[5].term.Borrow().GetIndices().empty());
  const auto tree = v[6].term.Borrow().GetSort();
  EXPECT_EQ(tree.GetSortConstructor()->GetNameHint(), "Tree");
  EXPECT_EQ(tree.GetArguments()[0].GetKind(), SortKind::kBool);
}

TEST(FrontendDatatypes,
     ConstructorRecipeMatchingSharesPairsAndRejectsConflicts) {
  std::string prefix =
      "(set-logic ALL)(define-sort Dup (T) (Array T T))"
      "(define-sort A0 (T) T)";
  for (int i = 1; i <= 12; ++i) {
    prefix += "(define-sort A" + std::to_string(i) + " (T) (Dup (A" +
              std::to_string(i - 1) + " T)))";
  }
  prefix +=
      "(declare-datatype Box (par (T) ((box (inner (A12 T))))))"
      "(declare-const a (A12 Int))";
  const auto script = prefix + "(assert ((_ is box) (box a)))(check-sat)";
  auto session = Require(Session::FromText(script));
  while (Require(Next(session))) {
  }
  EXPECT_EQ(session.GetStats().recipe_pairs, 13u);
  const auto node =
      session.GetPrefix().GetAssertions()[0].term.Borrow().GetChildren()[0];
  EXPECT_EQ(node.GetSort().GetArguments()[0].GetKind(), SortKind::kInt);

  constexpr std::string_view setup =
      "(set-logic ALL)(declare-datatype Pair (par (T S)"
      " ((pair (x (Array T S)) (y T) (z T)) (empty))))"
      "(declare-const a (Array Int Bool))";
  for (auto error :
       {"(pair a 1 false)", "(pair a false 1)",
        "((as pair (Pair Bool Int)) a 1 2)", "empty",
        "(= (as empty (Pair Int Bool)) empty)", "((_ is pair) 0)",
        "(as empty Bool)", "((_ is 1) (as empty (Pair Int Bool)))"}) {
    SCOPED_TRACE(error);
    ExpectError(Values(error, setup), DiagnosticCode::kInvalidInput);
  }
  const auto accepted = Require(Values("(pair a 1 2)", setup));
  const auto args = accepted.values[0].term.Borrow().GetSort().GetArguments();
  EXPECT_EQ(args[0].GetKind(), SortKind::kInt);
  EXPECT_EQ(args[1].GetKind(), SortKind::kBool);
}

TEST(FrontendDatatypes, InvalidGroupsAndUnsupportedContainersAreAtomic) {
  struct Case {
    // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
    std::string_view input;
    DiagnosticCode code;
    DatatypeReason reason;
    // NOLINTEND(readability-identifier-naming)
  };
  for (const auto& [input, code, reason] : {
           Case{"(declare-datatypes () ())", DiagnosticCode::kInvalidInput,
                DatatypeReason::kEmptyGroup},
           Case{"(declare-datatype T ())", DiagnosticCode::kInvalidInput,
                DatatypeReason::kNoConstructor},
           Case{"(declare-datatype T ((bad (p T))))",
                DiagnosticCode::kInvalidInput, DatatypeReason::kNonproductive},
           Case{"(declare-datatype T ((bad (p (Wrap T)))(base)))",
                DiagnosticCode::kUnsupported,
                DatatypeReason::kNestedRecursiveSort},
       }) {
    SCOPED_TRACE(input);
    const auto script = "(set-logic ALL)(define-sort Wrap (X) (Array Int X))" +
                        std::string(input) +
                        "(declare-datatype T ((bad (p Int))(base)))(check-sat)";
    auto session = Require(Session::FromText(script));
    Require(Next(session));
    Require(Next(session));
    const auto failure = Next(session);
    ExpectError(failure, code);
    ASSERT_FALSE(failure);
    ASSERT_TRUE(failure.error().node_error);
    EXPECT_EQ(failure.error().node_error->code, ErrorCode::kInvalidDatatype);
    EXPECT_EQ(failure.error().node_error->datatype_reason, reason);
    EXPECT_TRUE(session.GetPrefix().GetDatatypeGroups().empty());
    Require(Next(session));
    EXPECT_EQ(RequireCheck(session)->GetDatatypeGroups().size(), 1);
  }

  for (auto input : {
           "(declare-datatypes ((T 1)) (((base))))",
           "(declare-datatypes ((T 0)) ())",
           "(declare-datatypes ((T 0) (|T| 0)) (((base)) ((alt))))",
           "(declare-datatype T (par (X |X|) ((base))))",
           "(declare-datatype T ((base)(|base|)))",
           "(declare-datatype T ((c (x Int))(d (|x| Bool))))",
           "(declare-const c Int)(declare-datatype T ((c)))",
           "(declare-datatype T ((c (ite Int))))",
           "(declare-datatype T ((c (x Missing))))",
       }) {
    SCOPED_TRACE(input);
    ExpectError(Parse("(set-logic ALL)" + std::string(input)),
                DiagnosticCode::kInvalidInput);
  }
  const auto valid = RequireQuery(
      Parse("(set-logic ALL)(define-sort Drop (X) Int)"
            "(declare-datatype D (par (A) ((base (val (Drop (D A))))"
            " (step (next (D (Array Int A)))))))"
            "(declare-const d (D Bool))(assert ((_ is step) d))(check-sat)"));
  EXPECT_EQ(valid->GetDatatypeGroups().size(), 1);

  std::size_t errors = 0;
  for (std::size_t i = 0; i < 64; ++i) {
    auto session = Require(Session::FromText(
        "(set-logic ALL)(declare-datatype D ((ctor (p Bool))(base)))"
        "(declare-datatype D ((ctor (p Bool))(base)))"
        "(assert ((_ is ctor) (ctor true)))(check-sat)"));
    Require(Next(session));
    session.FailAllocationAfter(i);
    const auto first = Next(session);
    if (first) break;
    ++errors;
    ExpectError(first, DiagnosticCode::kResource);
    EXPECT_TRUE(session.GetPrefix().GetDatatypeGroups().empty());
    Require(Next(session));
    Require(Next(session));
    const auto query = RequireCheck(session);
    ASSERT_EQ(query->GetDatatypeGroups().size(), 1);
  }
  EXPECT_GE(errors, 3);
}

TEST(FrontendMatch, FirstMatchChecksEveryBranchAndLowersGuardedSelectors) {
  constexpr std::string_view setup =
      "(set-logic ALL)(declare-datatype Choice ((box (item Int)(flag "
      "Bool))(none)))"
      "(declare-datatype Other ((off)(on (code Int))))(declare-const ch "
      "Choice)";
  const auto typed =
      Require(Values("(match ch (((box on _) on) ((box x y) 9) (none 3)))"
                     " (ite ((_ is box) ch) (item ch) 3)"
                     " (match ch ((v v) (none ch))) ch"
                     " (match ch (((box _ _) 1) (_ 2)))"
                     " (match ch (((box |_| _) |_|) (none 0)))",
                     setup));
  const auto& values = typed.values;
  EXPECT_EQ(values[0].term, values[1].term);
  EXPECT_EQ(values[2].term, values[3].term);
  EXPECT_EQ(values[4].term.Borrow().GetKind(), Kind::kIte);
  EXPECT_EQ(values[5].term.Borrow().GetKind(), Kind::kIte);
  EXPECT_EQ(Applied(Argument(values[5].term, 1)).GetNameHint(), "item");
  EXPECT_EQ(IntegerValue(Argument(values[0].term, 2)), "3");

  for (auto input :
       {"(match false ((_ 1)))", "(match ch ((box 0)(none 1)))",
        "(match ch (((box x) 1)(none 0)))",
        "(match ch (((box x |x|) 1)(none 0)))", "(match ch (((box x y) x)))",
        "(match ch (((on x) x) (_ 0)))", "(match ch ((_ 0)(none true)))",
        "(match ch (((box x y) x)((box a b) (+ b 1))(none 0)))",
        "((_ is off) ch)", "(code ch)"}) {
    SCOPED_TRACE(input);
    ExpectError(Values(input, setup), DiagnosticCode::kInvalidInput);
  }
  const auto query = RequireQuery(
      Parse(std::string(setup) +
            "(assert (= (match ch (((box a b) (* 2 a))"
            " ((box c d) (! (* 3 4) :named unreachable)) (none 0)))"
            " (ite ((_ is box) ch) (* 2 (item ch)) 0)))"
            "(assert (= unreachable (* 3 4)))(check-sat)"));
  EXPECT_EQ(Argument(query->GetAssertions()[0].term, 0),
            Argument(query->GetAssertions()[0].term, 1));
  EXPECT_EQ(Argument(query->GetAssertions()[1].term, 0),
            Argument(query->GetAssertions()[1].term, 1));
}

TEST(FrontendCombinations, DatatypeArrayFpAndUfRanksAndBoundaries) {
  constexpr std::string_view setup =
      "(set-logic ALL)(declare-sort U 2)(declare-datatype Bit ((bit (raw (_ "
      "BitVec 8)))))"
      "(declare-datatype T ((packet (contents (Array RoundingMode Bit)) (mark "
      "Float32))))"
      "(declare-fun f (Bool T (U Float32 Int)) (Array (_ BitVec 1) T))"
      "(declare-const a (Array RoundingMode Bit))(declare-const token (U "
      "Float32 Int))"
      "(declare-const x Float32)(declare-const rm RoundingMode)";
  const auto typed = Require(Values(
      "(raw (select (contents (select (f true (packet a x) token) #b0)) rm))",
      setup));
  const auto root = typed.values[0].term.Borrow();
  EXPECT_EQ(root.GetSort().GetWidth(), 8u);
  EXPECT_EQ(Applied(root).GetKind(), DeclKind::kDatatypeSelector);
  EXPECT_EQ(root.GetChildren()[0].GetSort().GetSortConstructor()->GetNameHint(),
            "Bit");
  ExpectError(Values("(select (f true (packet a x) token) true)", setup),
              DiagnosticCode::kInvalidInput);
  ExpectError(Values("(packet a RNE)", setup), DiagnosticCode::kInvalidInput);
  ExpectError(Values("(f true (packet a x) x)", setup),
              DiagnosticCode::kInvalidInput);
}

TEST(FrontendScale, DeepShorthandSortsAndNestedMatches) {
  std::string script =
      "(set-logic ALL)(define-sort S (T) (Array Bool T))"
      "(declare-datatype Color ((red)(blue)))"
      "(declare-const c Color)(declare-const a ";
  constexpr std::size_t kSortDepth = 8500;
  for (std::size_t i = 0; i < kSortDepth; ++i) script += "(S ";
  script += "Int" + std::string(kSortDepth + 1, ')');
  script += "(assert (= ";
  constexpr std::size_t kMatchDepth = 3000;
  for (std::size_t i = 0; i < kMatchDepth; ++i) script += "(match c ((red ";
  script += "1";
  for (std::size_t i = 0; i < kMatchDepth; ++i) script += ") (blue 2)))";
  script += "1))(check-sat)";
  auto query = RequireQuery(Parse(script));
  auto sort = *query->GetFunctions()[1].decl.Borrow().GetRange();
  for (std::size_t i = 0; i < kSortDepth; ++i) {
    ASSERT_EQ(sort.GetKind(), SortKind::kArray);
    EXPECT_EQ(sort.GetArrayIndex()->GetKind(), SortKind::kBool);
    sort = *sort.GetArrayElement();
  }
  EXPECT_EQ(sort.GetKind(), SortKind::kInt);
  auto node = Argument(query->GetAssertions()[0].term, 0);
  for (std::size_t i = 0; i < kMatchDepth; ++i) {
    ASSERT_EQ(node.GetKind(), Kind::kIte);
    const auto children = node.GetChildren();
    EXPECT_EQ(Applied(children[0]).GetConstructor()->GetNameHint(), "red");
    EXPECT_EQ(IntegerValue(children[2]), "2");
    node = children[1];
  }
  EXPECT_EQ(IntegerValue(node), "1");
}

TEST(FrontendMatch, ConstructorPatternsResolveIndependentlyOfLetNames) {
  constexpr std::string_view setup =
      "(set-logic ALL)(declare-datatype Choice ((a) (b (field Int)) (c)))"
      "(declare-const choice Choice)";
  constexpr std::string_view match =
      "(let ((a 99) (b false)) (match choice ((c 3) ((b a) a) (a 1))))";
  SCOPED_TRACE(::testing::Message() << "FE-MATCH-ORDER-02 " << setup << match);
  const auto typed = Require(Values(std::string(match) +
                                        " choice a (b 7) c"
                                        " (let ((a 99) (b false)) (+ 0 (match "
                                        "choice ((c a) ((b a) a) (a 1))) a))",
                                    setup));
  ASSERT_EQ(typed.values.size(), 6);
  const auto root = typed.values[0].term.Borrow();
  const auto choice = typed.values[1].term.Borrow();
  const auto constructor = [&](std::size_t i) {
    return Applied(typed.values[i].term.Borrow());
  };
  ASSERT_EQ(root.GetKind(), Kind::kIte);
  ASSERT_EQ(root.GetChildren().size(), 3);
  const auto outer = root.GetChildren();
  const auto next = outer[2];
  ASSERT_EQ(next.GetKind(), Kind::kIte);
  ASSERT_EQ(next.GetChildren().size(), 3);
  const auto inner = next.GetChildren();
  for (const auto& [condition, expected] :
       {std::pair{outer[0], constructor(4)}, {inner[0], constructor(3)}}) {
    ASSERT_EQ(condition.GetKind(), Kind::kApply);
    EXPECT_EQ(Applied(condition).GetKind(), DeclKind::kDatatypeTester);
    EXPECT_EQ(Applied(condition).GetConstructor(), expected);
    ExpectChildren(condition, {choice});
  }
  ASSERT_EQ(inner[1].GetKind(), Kind::kApply);
  EXPECT_EQ(Applied(inner[1]).GetKind(), DeclKind::kDatatypeSelector);
  EXPECT_EQ(Applied(inner[1]).GetConstructor(), constructor(3));
  EXPECT_EQ(Applied(inner[1]).GetNameHint(), "field");
  ExpectChildren(inner[1], {choice});
  EXPECT_EQ(IntegerValue(outer[1]), "3");
  EXPECT_EQ(IntegerValue(inner[2]), "1");
  // The guarded selector extracts 7 only in the branch for constructor b.
  constexpr std::array expected_values{"1", "7", "3"};
  for (std::size_t i = 0; i < expected_values.size(); ++i) {
    const auto input = typed.values[i + 2].term.Borrow();
    const auto decl = Applied(input);
    const auto result = Applied(outer[0]).GetConstructor() == decl ? outer[1]
                        : Applied(inner[0]).GetConstructor() == decl
                            ? input.GetChildren()[0]
                            : inner[2];
    EXPECT_EQ(IntegerValue(result), expected_values[i]);
  }
  const auto scoped = typed.values[5].term.Borrow();
  ASSERT_EQ(scoped.GetKind(), Kind::kIntAdd);
  ASSERT_EQ(scoped.GetChildren().size(), 3);
  const auto scoped_match = scoped.GetChildren()[1];
  ASSERT_EQ(scoped_match.GetKind(), Kind::kIte);
  ASSERT_EQ(scoped_match.GetChildren().size(), 3);
  EXPECT_EQ(IntegerValue(scoped_match.GetChildren()[1]), "99");
  EXPECT_EQ(scoped_match.GetChildren()[2], next);
  EXPECT_EQ(IntegerValue(scoped.GetChildren()[2]), "99");
  ExpectError(Values("(match choice ((_ 0) ((b arg) false)))", setup),
              DiagnosticCode::kInvalidInput);
}

TEST(FrontendDatatypes, ParParametersShadowSortsAndSelectorsUseDatatypeRank) {
  constexpr std::string_view setup =
      "(set-logic ALL)(declare-sort T 0)"
      "(declare-datatype Box (par (T) ((pack (payload T)) (empty))))"
      "(declare-datatype Wrap (par (T) ((wrap (contents (Box T))))))"
      "(declare-datatype Wrong ((wrong (bad Int))))"
      "(declare-const global T)";
  const auto typed =
      Require(Values("(wrap (pack 7)) (pack 7) (payload (as empty (Box Int)))"
                     " (payload (pack global)) global",
                     setup));
  ASSERT_EQ(typed.values.size(), 5);
  const auto wrap = typed.values[0].term.Borrow();
  const auto pack = typed.values[1].term.Borrow();
  const auto selected_empty = typed.values[2].term.Borrow();
  const auto selected_global = typed.values[3].term.Borrow();
  ASSERT_EQ(wrap.GetSort().GetArguments().size(), 1);
  EXPECT_EQ(wrap.GetSort().GetArguments()[0].GetKind(), SortKind::kInt);
  EXPECT_EQ(pack.GetSort().GetArguments()[0], wrap.GetSort().GetArguments()[0]);
  ExpectChildren(wrap, {pack});
  EXPECT_EQ(Applied(wrap).GetNameHint(), "wrap");
  EXPECT_EQ(selected_empty.GetSort().GetKind(), SortKind::kInt);
  ASSERT_EQ(selected_empty.GetChildren().size(), 1);
  EXPECT_EQ(selected_empty.GetChildren()[0].GetSort(), pack.GetSort());
  EXPECT_EQ(Applied(selected_empty.GetChildren()[0]).GetNameHint(), "empty");
  EXPECT_EQ(Applied(selected_empty).GetConstructor(), Applied(pack));
  EXPECT_EQ(selected_global.GetSort(), typed.values[4].term.Borrow().GetSort());
  EXPECT_EQ(selected_global.GetSort().GetSortConstructor()->GetNameHint(), "T");
  ExpectError(Values("(wrap (wrong 7))", setup), DiagnosticCode::kInvalidInput);
}

TEST(FrontendSorts, FailedAliasRecipeLeavesPriorAliasAndAllowsSameNameRetry) {
  constexpr std::string_view script =
      "(set-logic ALL)(define-sort Prior (T) (Array T Int))"
      "(declare-const first (Prior Bool))"
      "(define-sort Later (T) (Array (Prior T) Missing))"
      "(declare-const leaked (Later Bool))"
      "(define-sort Later (T) (Array (Prior T) (Prior Int)))"
      "(declare-const next (Later Bool))"
      "(declare-const same (Array (Array Bool Int) (Array Int Int)))"
      "(assert (= next same))(check-sat)";
  SCOPED_TRACE(script);
  auto session = Require(Session::FromText(script));
  RequireNext(session, CommandKind::kSetLogic);
  RequireNext(session, CommandKind::kDefineSort);
  RequireNext(session, CommandKind::kDeclareConst);
  const auto prior = session.GetPrefix().GetFunctions()[0].decl;
  ExpectError(Next(session), DiagnosticCode::kInvalidInput);
  ExpectError(Next(session), DiagnosticCode::kInvalidInput);
  ASSERT_EQ(session.GetPrefix().GetFunctions().size(), 1);
  EXPECT_EQ(session.GetPrefix().GetFunctions()[0].decl, prior);
  RequireNext(session, CommandKind::kDefineSort);
  RequireNext(session, CommandKind::kDeclareConst);
  RequireNext(session, CommandKind::kDeclareConst);
  RequireNext(session, CommandKind::kAssert);
  const auto query = RequireCheck(session);
  const auto functions = query->GetFunctions();
  ASSERT_EQ(functions.size(), 3);
  EXPECT_EQ(functions[0].decl, prior);
  const auto later = *functions[1].decl.Borrow().GetRange();
  EXPECT_EQ(later, functions[2].decl.Borrow().GetRange());
  EXPECT_EQ(later.GetArrayIndex(), prior.Borrow().GetRange());
  EXPECT_EQ(later.GetArrayElement()->GetArrayIndex()->GetKind(),
            SortKind::kInt);
  EXPECT_EQ(later.GetArrayElement()->GetArrayElement()->GetKind(),
            SortKind::kInt);
}

}  // namespace
}  // namespace dandelion::frontend::testing

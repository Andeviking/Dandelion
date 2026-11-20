#include "test_support.h"

namespace dandelion::frontend::testing {
namespace {

constexpr std::string_view kOperatorSetup =
    "(set-logic ALL)(declare-const q Bool)(declare-const rm RoundingMode)"
    "(declare-const f (_ FloatingPoint 3 3))(declare-const g (_ FloatingPoint "
    "3 3))"
    "(declare-const h (_ FloatingPoint 3 3))(declare-const h32 Float32)"
    "(declare-const a (Array Bool Int))";

struct OperatorCase {
  // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
  std::string_view label;
  std::string_view head;
  std::string_view operands;
  Kind kind;
  SortKind sort;
  std::uint32_t width;
  std::string_view indices;
  // NOLINTEND(readability-identifier-naming)
};

constexpr auto kOperators = std::to_array<OperatorCase>({
    {"Not", "not", "true", Kind::kNot, SortKind::kBool, 0, ""},
    {"And", "and", "true false q", Kind::kAnd, SortKind::kBool, 0, ""},
    {"Or", "or", "true false q", Kind::kOr, SortKind::kBool, 0, ""},
    {"Xor", "xor", "true false q", Kind::kXor, SortKind::kBool, 0, ""},
    {"Implies", "=>", "true false q", Kind::kImplies, SortKind::kBool, 0, ""},
    {"Equal", "=", "#b01 #b10 #b11", Kind::kEqual, SortKind::kBool, 0, ""},
    {"Distinct", "distinct", "#b01 #b10 #b11", Kind::kDistinct, SortKind::kBool,
     0, ""},
    {"Ite", "ite", "q 8 9", Kind::kIte, SortKind::kInt, 0, ""},
    {"IntNeg", "-", "7", Kind::kIntNeg, SortKind::kInt, 0, ""},
    {"IntAdd", "+", "7 2 3", Kind::kIntAdd, SortKind::kInt, 0, ""},
    {"IntSub", "-", "7 2 3", Kind::kIntSub, SortKind::kInt, 0, ""},
    {"IntMul", "*", "7 2 3", Kind::kIntMul, SortKind::kInt, 0, ""},
    {"IntDiv", "div", "7 2 3", Kind::kIntDiv, SortKind::kInt, 0, ""},
    {"IntMod", "mod", "7 2", Kind::kIntMod, SortKind::kInt, 0, ""},
    {"IntPow", "**", "7 2", Kind::kIntPow, SortKind::kInt, 0, ""},
    {"IntAbs", "abs", "7", Kind::kIntAbs, SortKind::kInt, 0, ""},
    {"IntLt", "<", "7 2 3", Kind::kIntLt, SortKind::kBool, 0, ""},
    {"IntLe", "<=", "7 2 3", Kind::kIntLe, SortKind::kBool, 0, ""},
    {"IntGt", ">", "7 2 3", Kind::kIntGt, SortKind::kBool, 0, ""},
    {"IntGe", ">=", "7 2 3", Kind::kIntGe, SortKind::kBool, 0, ""},
    {"IntDivisible", "(_ divisible 18446744073709551617)", "7",
     Kind::kIntDivisible, SortKind::kBool, 0, "18446744073709551617"},
    {"BvNot", "bvnot", "#x27", Kind::kBvNot, SortKind::kBitVector, 8, ""},
    {"BvNeg", "bvneg", "#x27", Kind::kBvNeg, SortKind::kBitVector, 8, ""},
    {"BvAnd", "bvand", "#x27 #xa1 #xf0", Kind::kBvAnd, SortKind::kBitVector, 8,
     ""},
    {"BvOr", "bvor", "#x27 #xa1 #xf0", Kind::kBvOr, SortKind::kBitVector, 8,
     ""},
    {"BvXor", "bvxor", "#x27 #xa1 #xf0", Kind::kBvXor, SortKind::kBitVector, 8,
     ""},
    {"BvAdd", "bvadd", "#x27 #xa1 #xf0", Kind::kBvAdd, SortKind::kBitVector, 8,
     ""},
    {"BvMul", "bvmul", "#x27 #xa1 #xf0", Kind::kBvMul, SortKind::kBitVector, 8,
     ""},
    {"BvNand", "bvnand", "#x27 #xa1", Kind::kBvNand, SortKind::kBitVector, 8,
     ""},
    {"BvNor", "bvnor", "#x27 #xa1", Kind::kBvNor, SortKind::kBitVector, 8, ""},
    {"BvXnor", "bvxnor", "#x27 #xa1", Kind::kBvXnor, SortKind::kBitVector, 8,
     ""},
    {"BvSub", "bvsub", "#x27 #xa1", Kind::kBvSub, SortKind::kBitVector, 8, ""},
    {"BvUdiv", "bvudiv", "#x27 #xa1", Kind::kBvUdiv, SortKind::kBitVector, 8,
     ""},
    {"BvUrem", "bvurem", "#x27 #xa1", Kind::kBvUrem, SortKind::kBitVector, 8,
     ""},
    {"BvSdiv", "bvsdiv", "#x27 #xa1", Kind::kBvSdiv, SortKind::kBitVector, 8,
     ""},
    {"BvSrem", "bvsrem", "#x27 #xa1", Kind::kBvSrem, SortKind::kBitVector, 8,
     ""},
    {"BvSmod", "bvsmod", "#x27 #xa1", Kind::kBvSmod, SortKind::kBitVector, 8,
     ""},
    {"BvShl", "bvshl", "#x27 #xa1", Kind::kBvShl, SortKind::kBitVector, 8, ""},
    {"BvLshr", "bvlshr", "#x27 #xa1", Kind::kBvLshr, SortKind::kBitVector, 8,
     ""},
    {"BvAshr", "bvashr", "#x27 #xa1", Kind::kBvAshr, SortKind::kBitVector, 8,
     ""},
    {"BvComp", "bvcomp", "#x27 #xa1", Kind::kBvComp, SortKind::kBitVector, 1,
     ""},
    {"BvUlt", "bvult", "#x27 #xa1", Kind::kBvUlt, SortKind::kBool, 0, ""},
    {"BvUle", "bvule", "#x27 #xa1", Kind::kBvUle, SortKind::kBool, 0, ""},
    {"BvUgt", "bvugt", "#x27 #xa1", Kind::kBvUgt, SortKind::kBool, 0, ""},
    {"BvUge", "bvuge", "#x27 #xa1", Kind::kBvUge, SortKind::kBool, 0, ""},
    {"BvSlt", "bvslt", "#x27 #xa1", Kind::kBvSlt, SortKind::kBool, 0, ""},
    {"BvSle", "bvsle", "#x27 #xa1", Kind::kBvSle, SortKind::kBool, 0, ""},
    {"BvSgt", "bvsgt", "#x27 #xa1", Kind::kBvSgt, SortKind::kBool, 0, ""},
    {"BvSge", "bvsge", "#x27 #xa1", Kind::kBvSge, SortKind::kBool, 0, ""},
    {"BvUaddo", "bvuaddo", "#x27 #xa1", Kind::kBvUaddo, SortKind::kBool, 0, ""},
    {"BvSaddo", "bvsaddo", "#x27 #xa1", Kind::kBvSaddo, SortKind::kBool, 0, ""},
    {"BvUmulo", "bvumulo", "#x27 #xa1", Kind::kBvUmulo, SortKind::kBool, 0, ""},
    {"BvSmulo", "bvsmulo", "#x27 #xa1", Kind::kBvSmulo, SortKind::kBool, 0, ""},
    {"BvUsubo", "bvusubo", "#x27 #xa1", Kind::kBvUsubo, SortKind::kBool, 0, ""},
    {"BvSsubo", "bvssubo", "#x27 #xa1", Kind::kBvSsubo, SortKind::kBool, 0, ""},
    {"BvSdivo", "bvsdivo", "#x27 #xa1", Kind::kBvSdivo, SortKind::kBool, 0, ""},
    {"BvNego", "bvnego", "#x27", Kind::kBvNego, SortKind::kBool, 0, ""},
    {"BvConcat", "concat", "#x27 #b11", Kind::kBvConcat, SortKind::kBitVector,
     10, ""},
    {"BvExtract", "(_ extract 6 3)", "#x27", Kind::kBvExtract,
     SortKind::kBitVector, 4, "6 3"},
    {"BvRepeat", "(_ repeat 3)", "#x27", Kind::kBvRepeat, SortKind::kBitVector,
     24, "3"},
    {"BvZeroExtend", "(_ zero_extend 4)", "#x27", Kind::kBvZeroExtend,
     SortKind::kBitVector, 12, "4"},
    {"BvSignExtend", "(_ sign_extend 7)", "#x27", Kind::kBvSignExtend,
     SortKind::kBitVector, 15, "7"},
    {"BvRotateLeft", "(_ rotate_left 18446744073709551617)", "#x27",
     Kind::kBvRotateLeft, SortKind::kBitVector, 8, "18446744073709551617"},
    {"BvRotateRight", "(_ rotate_right 2)", "#x27", Kind::kBvRotateRight,
     SortKind::kBitVector, 8, "2"},
    {"UbvToInt", "ubv_to_int", "#x27", Kind::kUbvToInt, SortKind::kInt, 0, ""},
    {"SbvToInt", "sbv_to_int", "#x27", Kind::kSbvToInt, SortKind::kInt, 0, ""},
    {"LegacyBv2nat", "bv2nat", "#x27", Kind::kUbvToInt, SortKind::kInt, 0, ""},
    {"IntToBv", "(_ int_to_bv 7)", "18446744073709551616", Kind::kIntToBv,
     SortKind::kBitVector, 7, "7"},
    {"LegacyInt2bv", "(_ int2bv 5)", "0", Kind::kIntToBv, SortKind::kBitVector,
     5, "5"},
    {"FpConstruct", "fp", "#b1 #b101 #b11", Kind::kFpConstruct,
     SortKind::kFloatingPoint, 3, ""},
    {"FpAbs", "fp.abs", "f", Kind::kFpAbs, SortKind::kFloatingPoint, 3, ""},
    {"FpNeg", "fp.neg", "f", Kind::kFpNeg, SortKind::kFloatingPoint, 3, ""},
    {"FpAdd", "fp.add", "rm f g", Kind::kFpAdd, SortKind::kFloatingPoint, 3,
     ""},
    {"FpSub", "fp.sub", "rm f g", Kind::kFpSub, SortKind::kFloatingPoint, 3,
     ""},
    {"FpMul", "fp.mul", "rm f g", Kind::kFpMul, SortKind::kFloatingPoint, 3,
     ""},
    {"FpDiv", "fp.div", "rm f g", Kind::kFpDiv, SortKind::kFloatingPoint, 3,
     ""},
    {"FpFma", "fp.fma", "rm f g h", Kind::kFpFma, SortKind::kFloatingPoint, 3,
     ""},
    {"FpSqrt", "fp.sqrt", "rm g", Kind::kFpSqrt, SortKind::kFloatingPoint, 3,
     ""},
    {"FpRoundToIntegral", "fp.roundToIntegral", "rm g",
     Kind::kFpRoundToIntegral, SortKind::kFloatingPoint, 3, ""},
    {"FpRem", "fp.rem", "f g", Kind::kFpRem, SortKind::kFloatingPoint, 3, ""},
    {"FpMin", "fp.min", "f g", Kind::kFpMin, SortKind::kFloatingPoint, 3, ""},
    {"FpMax", "fp.max", "f g", Kind::kFpMax, SortKind::kFloatingPoint, 3, ""},
    {"FpEq", "fp.eq", "f g h", Kind::kFpEq, SortKind::kBool, 0, ""},
    {"FpLt", "fp.lt", "f g h", Kind::kFpLt, SortKind::kBool, 0, ""},
    {"FpLeq", "fp.leq", "f g h", Kind::kFpLeq, SortKind::kBool, 0, ""},
    {"FpGt", "fp.gt", "f g h", Kind::kFpGt, SortKind::kBool, 0, ""},
    {"FpGeq", "fp.geq", "f g h", Kind::kFpGeq, SortKind::kBool, 0, ""},
    {"FpIsNormal", "fp.isNormal", "f", Kind::kFpIsNormal, SortKind::kBool, 0,
     ""},
    {"FpIsSubnormal", "fp.isSubnormal", "f", Kind::kFpIsSubnormal,
     SortKind::kBool, 0, ""},
    {"FpIsZero", "fp.isZero", "f", Kind::kFpIsZero, SortKind::kBool, 0, ""},
    {"FpIsInfinite", "fp.isInfinite", "f", Kind::kFpIsInfinite, SortKind::kBool,
     0, ""},
    {"FpIsNaN", "fp.isNaN", "f", Kind::kFpIsNaN, SortKind::kBool, 0, ""},
    {"FpIsNegative", "fp.isNegative", "f", Kind::kFpIsNegative, SortKind::kBool,
     0, ""},
    {"FpIsPositive", "fp.isPositive", "f", Kind::kFpIsPositive, SortKind::kBool,
     0, ""},
    {"FpFromBits", "(_ to_fp 3 3)", "#b000001", Kind::kFpFromBits,
     SortKind::kFloatingPoint, 3, "3 3"},
    {"FpConvert", "(_ to_fp 3 3)", "rm h32", Kind::kFpConvert,
     SortKind::kFloatingPoint, 3, "3 3"},
    {"FpFromSbv", "(_ to_fp 3 3)", "rm #x27", Kind::kFpFromSbv,
     SortKind::kFloatingPoint, 3, "3 3"},
    {"FpFromUbv", "(_ to_fp_unsigned 3 3)", "rm #x27", Kind::kFpFromUbv,
     SortKind::kFloatingPoint, 3, "3 3"},
    {"FpToSbv", "(_ fp.to_sbv 7)", "rm h32", Kind::kFpToSbv,
     SortKind::kBitVector, 7, "7"},
    {"FpToUbv", "(_ fp.to_ubv 9)", "rm h32", Kind::kFpToUbv,
     SortKind::kBitVector, 9, "9"},
    {"ArraySelect", "select", "a true", Kind::kArraySelect, SortKind::kInt, 0,
     ""},
    {"ArrayStore", "store", "a true 8", Kind::kArrayStore, SortKind::kArray, 0,
     ""},
    {"ArrayConst", "(as const (Array Bool Int))", "8", Kind::kArrayConst,
     SortKind::kArray, 0, ""},
});

class OperatorSpelling : public ::testing::TestWithParam<OperatorCase> {};
TEST_P(OperatorSpelling, TypedKindOrderedOperandsAndBadRank) {
  const auto [label, head, operands, kind, sort, width, indices] = GetParam();
  const auto start = "(" + std::string(head);
  const auto expression = start + " " + std::string(operands) + ")";
  SCOPED_TRACE(::testing::Message() << "FE-RANK-02 " << expression);
  const auto typed =
      Require(Values(expression + " " + std::string(operands), kOperatorSetup));
  ASSERT_FALSE(typed.values.empty());
  const auto root = typed.values[0].term.Borrow();
  ASSERT_EQ(root.GetKind(), kind) << label;
  EXPECT_EQ(root.GetSort().GetKind(), sort);
  if (sort == SortKind::kBitVector) {
    EXPECT_EQ(root.GetSort().GetWidth(), width);
  }
  if (sort == SortKind::kFloatingPoint) {
    EXPECT_EQ(root.GetSort().GetFpFormat().value().ExponentWidth(), width);
    EXPECT_EQ(root.GetSort().GetFpFormat().value().SignificandWidth(), width);
  }
  const auto children = root.GetChildren();
  ASSERT_EQ(children.size(), typed.values.size() - 1);
  std::size_t child = 0;
  for (auto word : operands | std::views::split(' ')) {
    EXPECT_EQ(typed.values[child + 1].input,
              std::string_view(word.begin(), word.end()));
    EXPECT_EQ(children[child], typed.values[child + 1].term.Borrow());
    ++child;
  }
  std::size_t index = 0;
  if (!indices.empty()) {
    for (auto word : indices | std::views::split(' ')) {
      ASSERT_LT(index, root.GetIndices().size());
      EXPECT_EQ(root.GetIndices()[index++].ToDecimal(),
                std::string_view(word.begin(), word.end()));
    }
  }
  EXPECT_EQ(index, root.GetIndices().size());

  const auto reject = [&](std::string_view input) {
    SCOPED_TRACE(input);
    ExpectError(Values(input, kOperatorSetup), DiagnosticCode::kInvalidInput);
  };
  const auto last_space = operands.rfind(' ');
  constexpr std::array kNaryKinds{
      Kind::kAnd,    Kind::kOr,       Kind::kXor,    Kind::kImplies,
      Kind::kEqual,  Kind::kDistinct, Kind::kIntAdd, Kind::kIntSub,
      Kind::kIntMul, Kind::kIntDiv,   Kind::kIntLt,  Kind::kIntLe,
      Kind::kIntGt,  Kind::kIntGe,    Kind::kBvAnd,  Kind::kBvOr,
      Kind::kBvXor,  Kind::kBvAdd,    Kind::kBvMul,  Kind::kFpEq,
      Kind::kFpLt,   Kind::kFpLeq,    Kind::kFpGt,   Kind::kFpGeq};
  if (std::ranges::contains(kNaryKinds, kind)) {
    EXPECT_GT(children.size(), 2u);
    if (kind != Kind::kAnd && kind != Kind::kOr && kind != Kind::kIntSub)
      reject(start + " " + std::string(operands.substr(0, operands.find(' '))) +
             ")");
  } else {
    if (last_space == std::string_view::npos) {
      reject(start + ")");
    } else if (kind != Kind::kFpConvert && kind != Kind::kFpFromSbv) {
      reject(start + " " + std::string(operands.substr(0, last_space)) + ")");
    }
    auto extra = operands.substr(
        last_space == std::string_view::npos ? 0 : last_space + 1);
    if (kind == Kind::kFpFromBits) extra = "rm #x27";
    if (kind != Kind::kIntNeg)
      reject(start + " " + std::string(operands) + " " + std::string(extra) +
             ")");
  }

  // Change one slot at a time, retaining the original rank and other slots.
  std::size_t offset = 0;
  for (const auto operand : children) {
    const auto end = operands.find(' ', offset);
    const auto wrong =
        operand.GetSort().GetKind() == SortKind::kBool ? "11" : "false";
    std::string mutated(operands);
    mutated.replace(offset, end == std::string_view::npos ? end : end - offset,
                    wrong);
    reject(start + " " + mutated + ")");
    if (end != std::string_view::npos) offset = end + 1;
  }

  if (head.starts_with("(_ ")) {
    const auto last_index = head.rfind(' ');
    reject("(" + std::string(head.substr(0, head.size() - 1)) + " 0) " +
           std::string(operands) + ")");
    reject("(" + std::string(head.substr(0, last_index)) + " index) " +
           std::string(operands) + ")");
  }
}

INSTANTIATE_TEST_SUITE_P(
    CheckedRanks, OperatorSpelling, ::testing::ValuesIn(kOperators),
    [](const ::testing::TestParamInfo<OperatorCase>& parameter) {
      return std::string(parameter.param.label);
    });

TEST(FrontendOperators, CompatibilityFormsAndResultQualifiers) {
  const auto typed =
      Require(Values("(and) (or) (and q) (or q) ((as bvnot (_ BitVec 8)) #xff) "
                     "((as and Bool) q)",
                     kOperatorSetup));
  EXPECT_EQ(typed.values[0].term.Borrow().GetValue()->GetBool(), true);
  EXPECT_EQ(typed.values[1].term.Borrow().GetValue()->GetBool(), false);
  EXPECT_EQ(typed.values[2].term, typed.values[3].term);
  EXPECT_EQ(typed.values[2].term, typed.values[5].term);
  EXPECT_EQ(typed.values[4].term.Borrow().GetKind(), Kind::kBvNot);
  for (auto input :
       {"(and 1)", "(or 1)", "((as and Int))", "(true)", "(q)",
        "((as bvnot Bool) #xff)", "(const 0)", "((as const Int) 0)",
        "(as q Int)", "((as const (Array Bool Int)) q)"}) {
    SCOPED_TRACE(input);
    ExpectError(Values(input, kOperatorSetup), DiagnosticCode::kInvalidInput);
  }
  for (auto input :
       {"(bv2int #xff)", "(fp.to_real f)", "(fp.to_ieee_bv f)",
        "(forall ((x Bool)) x)", "(exists ((x Bool)) x)",
        "(lambda ((x Bool)) x)", "(@ q q)", "\"literal\"", "1.5"}) {
    SCOPED_TRACE(input);
    ExpectError(Values(input, kOperatorSetup), DiagnosticCode::kUnsupported);
  }
}

TEST(FrontendOperators, IndicesWidthsAndFormatsKeepDistinctFailureCategories) {
  for (auto input : {"((_ extract 2 3) #xff)",
                     "((_ extract 8 0) #xff)",
                     "((_ extract -1 0) #xff)",
                     "((_ extract 2) #xff)",
                     "((_ repeat 0) #xff)",
                     "((_ repeat 2 0) #xff)",
                     "((_ zero_extend -1) #xff)",
                     "((_ sign_extend -1) #xff)",
                     "((_ rotate_left -1) #xff)",
                     "((_ rotate_right -1) #xff)",
                     "((_ int_to_bv 0) 1)",
                     "((_ divisible 0) 1)",
                     "((_ divisible -1) 1)",
                     "((_ to_fp 1 3) rm f)",
                     "((_ to_fp 3 1) rm #x01)",
                     "((_ to_fp 3 3) #b0)",
                     "((_ fp.to_sbv 0) rm f)",
                     "((_ fp.to_ubv -1) rm f)",
                     "((_ to_fp_unsigned 3 3) rm f)",
                     "(fp #b00 #b11 #b11)",
                     "(fp.eq f h32)",
                     "(= f h32)",
                     "(bvand #x00 #b0)",
                     "((_ + 1) 0 1)",
                     "((_ bv256 8))",
                     "((_ bv256 8) #x00)",
                     "(_ bv1 0)",
                     "(_ bv1 -1)",
                     "(_ bv1 2 3)"}) {
    SCOPED_TRACE(input);
    ExpectError(Values(input, kOperatorSetup), DiagnosticCode::kInvalidInput);
  }
  for (auto input :
       {"(_ bv1 4294967296)", "((_ repeat 4294967296) #b1)",
        "((_ zero_extend 4294967295) #b1)", "((_ sign_extend 4294967295) #b1)",
        "((_ int_to_bv 4294967296) 0)", "((_ to_fp 4294967295 2) rm f)",
        "((_ fp.to_ubv 4294967296) rm f)"}) {
    SCOPED_TRACE(input);
    const auto value = Values(input, kOperatorSetup);
    ExpectError(value, DiagnosticCode::kResource);
    ASSERT_FALSE(value);
    ASSERT_TRUE(value.error().node_error);
    EXPECT_EQ(value.error().node_error->resource, ResourceKind::kSize);
  }
  auto terms =
      Require(Values("((_ extract 7 7) #xff) ((_ extract 0 0) #xff)"
                     " ((_ zero_extend 0) #xff) ((_ sign_extend 0) #xff)"
                     " ((_ repeat 4294967295) #b1) (_ bv1 4294967295)",
                     kOperatorSetup));
  EXPECT_EQ(terms.values[0].term.Borrow().GetSort().GetWidth(), 1u);
  EXPECT_EQ(terms.values[1].term.Borrow().GetSort().GetWidth(), 1u);
  EXPECT_EQ(terms.values[2].term.Borrow().GetSort().GetWidth(), 8u);
  EXPECT_EQ(terms.values[3].term.Borrow().GetSort().GetWidth(), 8u);
  constexpr auto limit = std::numeric_limits<std::uint32_t>::max();
  EXPECT_EQ(terms.values[4].term.Borrow().GetSort().GetWidth(), limit);
  EXPECT_EQ(terms.values[5].term.Borrow().GetSort().GetWidth(), limit);
}

TEST(FrontendLiterals, ExactBvAndIntValuesAndFpCategories) {
  const std::string wide = "#b1" + std::string(64, '0');
  const std::string long_hex = "#x10000000000000000";
  const auto typed = Require(Values(
      "#b01 #b1 #xFF (_ bv255 8) #b11111111 #x00000000 #x0000000000000000 " +
          wide + " " + long_hex +
          " 0 9223372036854775807 9223372036854775808"
          " (- 9223372036854775808) 18446744073709551616"
          " (_ NaN 3 3) (_ +oo 3 3) (_ -oo 3 3) (_ +zero 3 3) (_ -zero 3 3)"
          " (fp #b1 #b000 #b00)",
      kOperatorSetup));
  constexpr std::array widths{2u, 1u, 8u, 8u, 8u, 32u, 64u, 65u, 68u};
  for (std::size_t i = 0; i < widths.size(); ++i) {
    EXPECT_EQ(typed.values[i].term.Borrow().GetKind(), Kind::kBvValue);
    EXPECT_EQ(typed.values[i].term.Borrow().GetSort().GetWidth(), widths[i]);
  }
  EXPECT_EQ(BvValue(typed.values[0].term.Borrow()), "1");
  EXPECT_EQ(typed.values[2].term, typed.values[3].term);
  EXPECT_EQ(typed.values[3].term, typed.values[4].term);
  for (std::size_t i : {7u, 8u})
    EXPECT_EQ(BvValue(typed.values[i].term.Borrow()), "18446744073709551616");
  EXPECT_EQ(IntegerValue(typed.values[13].term.Borrow()),
            "18446744073709551616");
  EXPECT_NE(typed.values[7].term, typed.values[8].term);
  EXPECT_EQ(IntegerValue(typed.values[9].term.Borrow()), "0");
  EXPECT_EQ(IntegerValue(typed.values[10].term.Borrow()),
            "9223372036854775807");
  EXPECT_EQ(IntegerValue(typed.values[11].term.Borrow()),
            "9223372036854775808");
  EXPECT_EQ(typed.values[12].term.Borrow().GetKind(), Kind::kIntNeg);
  EXPECT_EQ(Argument(typed.values[12].term, 0), typed.values[11].term.Borrow());
  for (const auto category :
       {numeric::FpCategory::kNaN, numeric::FpCategory::kPositiveInfinity,
        numeric::FpCategory::kNegativeInfinity,
        numeric::FpCategory::kPositiveZero,
        numeric::FpCategory::kNegativeZero}) {
    const auto index = 14u + std::to_underlying(category);
    EXPECT_EQ(typed.values[index]
                  .term.Borrow()
                  .GetValue()
                  ->GetFloatingPoint()
                  .value()
                  .Category(),
              category);
  }
  EXPECT_NE(typed.values[17].term, typed.values[18].term);
  EXPECT_EQ(typed.values[19].term.Borrow().GetKind(), Kind::kFpConstruct);
  const auto reduced =
      Require(Values("(_ bv256 8) #x00"
                     " (_ bv18446744073709551616 64) (_ bv0 64)"));
  ASSERT_EQ(reduced.values.size(), 4u);
  EXPECT_EQ(reduced.values[0].term, reduced.values[1].term);
  EXPECT_EQ(reduced.values[2].term, reduced.values[3].term);
  EXPECT_NE(reduced.values[0].term, reduced.values[2].term);
}

TEST(FrontendLiterals, WideDecimalBvReducesAndCachesByWidth) {
  // 2^66 + 2^64 + 1 retains the second limb at width 65 only.
  const auto typed = Require(
      Values("(_ bv92233720368547758081 65) (_ bv92233720368547758081 64)"
             " (_ bv92233720368547758081 65) (_ bv92233720368547758081 64)"));
  ASSERT_EQ(typed.values.size(), 4u);
  for (std::size_t i = 0; i < typed.values.size(); ++i) {
    const auto bits = typed.values[i].term.Borrow().GetValue()->GetBitVector();
    ASSERT_TRUE(bits);
    EXPECT_EQ(bits->Width(), i % 2 == 0 ? 65u : 64u);
    EXPECT_EQ(bits->Chunk(0), 1u);
    EXPECT_EQ(bits->Chunk(1), i % 2 == 0 ? 1u : 0u);
  }
  EXPECT_EQ(typed.values[0].term, typed.values[2].term);
  EXPECT_EQ(typed.values[1].term, typed.values[3].term);
}

TEST(FrontendLiterals, RoundingModesFormatsAndRationalIdentityPrecedence) {
  const auto modes =
      Require(Values("RNE roundNearestTiesToEven RNA roundNearestTiesToAway "
                     "RTP roundTowardPositive"
                     " RTN roundTowardNegative RTZ roundTowardZero ((_ to_fp 3 "
                     "3) rm (/ (- 1.5) (/ 3 2)))",
                     kOperatorSetup));
  for (std::size_t i = 0; i < 5; ++i) {
    EXPECT_EQ(modes.values[2 * i].term, modes.values[2 * i + 1].term);
    EXPECT_EQ(modes.values[2 * i].term.Borrow().GetValue()->GetRoundingMode(),
              static_cast<numeric::RoundingMode>(i));
  }
  const auto& convert = modes.values[10].term;
  ASSERT_EQ(convert.Borrow().GetKind(), Kind::kFpFromRational);
  ASSERT_EQ(convert.Borrow().GetChildren().size(), 1);
  EXPECT_EQ(Applied(Argument(convert, 0)).GetNameHint(), "rm");
  EXPECT_EQ(convert.Borrow().GetRationalParameter()->Numerator().ToDecimal(),
            "-1");
  EXPECT_EQ(convert.Borrow().GetRationalParameter()->Denominator().ToDecimal(),
            "1");
  ExpectError(Values("((_ to_fp 3 3) RNE (/ 1 (/ 0 3)))"),
              DiagnosticCode::kUnsupported);
  ExpectError(Values("((_ to_fp 3 3) true 1.2)"),
              DiagnosticCode::kInvalidInput);
  const auto fp_only = Require(Values(
      "#b1 #x0 (_ bv1 1) ((_ to_fp 3 3) RNE (|/| 1 2))", "(set-logic QF_FP)"));
  EXPECT_EQ(fp_only.values[0].term, fp_only.values[2].term);
  EXPECT_EQ(fp_only.values[3].term.Borrow().GetKind(), Kind::kFpFromRational);
  const auto uf =
      Require(Values("((_ to_fp 3 3) RNE (|/| 1 2))",
                     "(set-logic ALL)(declare-fun / (Int Int) (_ BitVec 1))"));
  EXPECT_EQ(uf.values[0].term.Borrow().GetKind(), Kind::kFpFromSbv);
  EXPECT_EQ(Applied(Argument(uf.values[0].term, 1)).GetNameHint(), "/");
  ExpectError(Values("((_ to_fp 3 3) RNE (/ 1 2))",
                     "(set-logic ALL)(declare-fun / (Bool Bool) (_ BitVec 1))"),
              DiagnosticCode::kInvalidInput);
  const auto fp_uf = Require(
      Values("((_ to_fp 3 3) RNE (/ true false))",
             "(set-logic QF_UFFP)(declare-fun |/| (Bool Bool) (_ BitVec 1))"));
  EXPECT_EQ(fp_uf.values[0].term.Borrow().GetKind(), Kind::kFpFromSbv);
  ExpectError(Values("(let ((/ #b1)) ((_ to_fp 3 3) RNE (/ 1 2)))",
                     "(set-logic QF_UFFP)"),
              DiagnosticCode::kInvalidInput);
  ExpectError(
      Values("((_ to_fp 3 3) RNE (/ (- 1) 2))",
             "(set-logic QF_UFFP)(declare-fun - (Bool Bool) (_ BitVec 1))"),
      DiagnosticCode::kInvalidInput);
  ExpectError(Values("(bvadd #b0 #b1)", "(set-logic QF_FP)"),
              DiagnosticCode::kUnsupported);
}

TEST(FrontendOperators, ExcludedIdentifiersStillRequireTheirIndexShape) {
  for (auto term :
       {"((_ fp.to_real 0) true)", "(re.^ true)", "((_ re.^ i) true)",
        "((_ re.loop 2) true)", "((_ re.loop 2 i) true)"}) {
    SCOPED_TRACE(::testing::Message() << "FE-PROFILE-SHAPE-02 " << term);
    ExpectError(Values(term, "(set-logic QF_SAT)"),
                DiagnosticCode::kInvalidInput);
  }
  for (auto term :
       {"(fp.to_real true)", "((_ re.^ 2) true)", "((_ re.loop 2 3) true)"}) {
    SCOPED_TRACE(::testing::Message() << "FE-PROFILE-SHAPE-02 " << term);
    ExpectError(Values(term, "(set-logic QF_SAT)"),
                DiagnosticCode::kUnsupported);
  }
}

TEST(FrontendOperators, InvalidBuiltinParameterMapsToSecondWrittenIndex) {
  constexpr std::string_view script =
      "(set-logic ALL)\n(assert (= ((_ to_fp 5\n"
      "                            1) RNE #x00) (_ +zero 5 11)))";
  constexpr std::uint32_t kSource = 43;
  SCOPED_TRACE(::testing::Message() << "FE-RANK-SPAN-02 " << script);
  auto session = Require(Session::FromText(script, kSource));
  RequireNext(session, CommandKind::kSetLogic);
  const auto result = Next(session);
  ASSERT_FALSE(result);
  EXPECT_EQ(result.error().code, DiagnosticCode::kInvalidInput);
  ASSERT_TRUE(result.error().node_error);
  EXPECT_EQ(result.error().node_error->code, ErrorCode::kInvalidParameter);
  EXPECT_EQ(result.error().node_error->parameter_index, 1u);
  const auto offset = script.find("1)");
  EXPECT_EQ(result.error().where,
            (SourceSpan{kSource, {offset, 3, 29}, {offset + 1, 3, 30}}));
}

TEST(FrontendOperators, RankErrorsIdentifyWrittenOperandsAndIndices) {
  constexpr std::string_view script =
      "(set-logic ALL)\n"
      "(assert (= (bvadd #x00\n"
      "                 #b0) #x00))\n"
      "(assert (= ((_ to_fp 1 11) RNE #x00) (_ +zero 5 11)))\n"
      "(assert (= (mod 4 0 2) 0))\n"
      "(assert (= ((_ to_fp 5 11) RNE (/ 1 (/ 0 3))) (_ +zero 5 11)))";
  constexpr std::uint32_t kSource = 42;
  constexpr auto kNoIndex = std::numeric_limits<std::uint64_t>::max();
  auto session = Require(Session::FromText(script, kSource));
  RequireNext(session, CommandKind::kSetLogic);
  const auto check = [&](DiagnosticCode code, std::string_view text,
                         std::uint64_t line, std::uint64_t column,
                         std::uint64_t operand, std::uint64_t parameter) {
    SCOPED_TRACE(::testing::Message() << "FE-RANK-SPAN-02 " << text);
    const auto result = Next(session);
    ASSERT_FALSE(result);
    const auto& error = result.error();
    EXPECT_EQ(error.code, code);
    const auto offset = line == 4 ? script.find("1 11") : script.find(text);
    EXPECT_EQ(error.where,
              (SourceSpan{kSource,
                          {offset, line, column},
                          {offset + text.size(), line, column + text.size()}}));
    if (code == DiagnosticCode::kUnsupported) return;
    ASSERT_TRUE(error.node_error);
    EXPECT_EQ(error.node_error->operand_index, operand);
    EXPECT_EQ(error.node_error->parameter_index, parameter);
  };
  check(DiagnosticCode::kInvalidInput, "#b0", 3, 18, 1, kNoIndex);
  check(DiagnosticCode::kInvalidInput, "1", 4, 22, kNoIndex, 0);
  check(DiagnosticCode::kInvalidInput, "(mod 4 0 2)", 5, 12, kNoIndex,
        kNoIndex);
  check(DiagnosticCode::kUnsupported, "(/ 0 3)", 6, 37, kNoIndex, kNoIndex);
}

TEST(FrontendOperators, AsymmetricFpConversionAndSignedZeroEquality) {
  constexpr std::string_view input =
      "((_ to_fp 5 11) rm f) ((_ to_fp 5 11) #x0001) rm f #x0001"
      " (= (_ +zero 5 11) (_ -zero 5 11))"
      " (fp.eq (_ +zero 5 11) (_ -zero 5 11))"
      " (_ +zero 5 11) (_ -zero 5 11)";
  SCOPED_TRACE(::testing::Message() << "FE-FP-02 " << input);
  constexpr std::string_view setup =
      "(set-logic ALL)(declare-const rm RoundingMode)(declare-const f Float32)";
  const auto command = Require(Values(input, setup));
  const auto& values = command.values;
  ASSERT_EQ(values.size(), 9);
  const auto convert = values[0].term.Borrow();
  const auto packed = values[1].term.Borrow();
  EXPECT_EQ(convert.GetKind(), Kind::kFpConvert);
  EXPECT_EQ(packed.GetKind(), Kind::kFpFromBits);
  ASSERT_EQ(convert.GetChildren().size(), 2);
  EXPECT_EQ(convert.GetChildren()[0], values[2].term.Borrow());
  EXPECT_EQ(convert.GetChildren()[1], values[3].term.Borrow());
  const auto source = convert.GetChildren()[1].GetSort();
  EXPECT_EQ(source.GetFpFormat().value().ExponentWidth(), 8u);
  EXPECT_EQ(source.GetFpFormat().value().SignificandWidth(), 24u);
  ASSERT_EQ(packed.GetChildren().size(), 1);
  EXPECT_EQ(packed.GetChildren()[0], values[4].term.Borrow());
  EXPECT_EQ(values[4].term.Borrow().GetSort().GetWidth(), 16u);
  for (const auto term : {convert, packed}) {
    ASSERT_EQ(term.GetIndices().size(), 2);
    EXPECT_EQ(term.GetIndices()[0].ToDecimal(), "5");
    EXPECT_EQ(term.GetIndices()[1].ToDecimal(), "11");
    EXPECT_EQ(term.GetSort().GetFpFormat().value().ExponentWidth(), 5u);
    EXPECT_EQ(term.GetSort().GetFpFormat().value().SignificandWidth(), 11u);
  }
  ExpectError(Values("((_ to_fp 5 11) #b000000000000001)", setup),
              DiagnosticCode::kInvalidInput);
  const auto positive = values[7].term.Borrow();
  const auto negative = values[8].term.Borrow();
  EXPECT_NE(positive, negative);
  EXPECT_EQ(positive.GetValue()->GetFloatingPoint().value().Category(),
            numeric::FpCategory::kPositiveZero);
  EXPECT_EQ(negative.GetValue()->GetFloatingPoint().value().Category(),
            numeric::FpCategory::kNegativeZero);
  EXPECT_EQ(values[5].term.Borrow().GetKind(), Kind::kEqual);
  EXPECT_EQ(values[6].term.Borrow().GetKind(), Kind::kFpEq);
  for (const auto& value : std::span(values).subspan(5, 2)) {
    const auto term = value.term.Borrow();
    EXPECT_EQ(term.GetSort().GetKind(), SortKind::kBool);
    ASSERT_EQ(term.GetChildren().size(), 2);
    EXPECT_EQ(term.GetChildren()[0], positive);
    EXPECT_EQ(term.GetChildren()[1], negative);
  }
}

TEST(FrontendOperators, ArraysKeepDistinctIndexAndElementRanks) {
  constexpr std::string_view setup =
      "(set-logic ALL)(declare-const a (Array Bool Int))"
      "(declare-const b (Array Int Bool))";
  for (const bool bool_index : {true, false}) {
    const auto input = bool_index
                           ? "(select a true) (store a true 8)"
                             " ((as const (Array Bool Int)) 8) a true 8"
                           : "(select b 8) (store b 8 true)"
                             " ((as const (Array Int Bool)) true) b 8 true";
    SCOPED_TRACE(::testing::Message() << "FE-ARRAY-RANK-02 " << input);
    const auto command = Require(Values(input, setup));
    ASSERT_EQ(command.values.size(), 6);
    const auto term = [&](std::size_t i) {
      return command.values[i].term.Borrow();
    };
    EXPECT_EQ(term(0).GetKind(), Kind::kArraySelect);
    EXPECT_EQ(term(1).GetKind(), Kind::kArrayStore);
    EXPECT_EQ(term(2).GetKind(), Kind::kArrayConst);
    EXPECT_EQ(term(0).GetSort(), term(5).GetSort());
    for (auto array : {term(1), term(2)}) {
      EXPECT_EQ(array.GetSort(), term(3).GetSort());
      EXPECT_EQ(array.GetSort().GetArrayIndex(), term(4).GetSort());
      EXPECT_EQ(array.GetSort().GetArrayElement(), term(5).GetSort());
    }
    ExpectChildren(term(0), {term(3), term(4)});
    ExpectChildren(term(1), {term(3), term(4), term(5)});
    ExpectChildren(term(2), {term(5)});
  }
  for (const auto input :
       {"(select a 8)", "(store a 8 8)", "(store a true false)",
        "((as const (Array Bool Int)) false)"}) {
    SCOPED_TRACE(::testing::Message() << "FE-ARRAY-RANK-02 " << input);
    ExpectError(Values(input, setup), DiagnosticCode::kInvalidInput);
  }
}

TEST(FrontendOperators, RankTransitionsAndWidthMetadata) {
  for (const auto input :
       {"(= #b0)", "(ite true 1 false)", "(div 1)", "(mod 4 0 2)",
        "(concat #b0 #b1 #b0)", "(fp.add f f g)", "(fp.add rm f h32)",
        "(bvadd #x00 #b0)", "((as (_ extract 7 4) (_ BitVec 5)) #xff)"}) {
    SCOPED_TRACE(::testing::Message() << "FE-RANK-02 " << input);
    ExpectError(Values(input, kOperatorSetup), DiagnosticCode::kInvalidInput);
  }
  const auto terms =
      Require(Values("((as (_ extract 7 4) (_ BitVec 4)) #xff)"));
  ASSERT_EQ(terms.values.size(), 1);
  const auto extract = terms.values[0].term.Borrow();
  EXPECT_EQ(extract.GetKind(), Kind::kBvExtract);
  EXPECT_EQ(extract.GetSort().GetWidth(), 4u);
  ASSERT_EQ(extract.GetIndices().size(), 2);
  EXPECT_EQ(extract.GetIndices()[0].ToDecimal(), "7");
  EXPECT_EQ(extract.GetIndices()[1].ToDecimal(), "4");
  constexpr std::string_view setup =
      "(set-logic ALL)(declare-const max (_ BitVec 4294967295))"
      "(declare-const neighbor (_ BitVec 4294967294))";
  constexpr auto kMaxWidth = std::numeric_limits<std::uint32_t>::max();
  const auto width = Require(Values("(concat neighbor #b1)", setup));
  ASSERT_EQ(width.values.size(), 1);
  EXPECT_EQ(width.values[0].term.Borrow().GetSort().GetWidth(), kMaxWidth);
  const auto overflow = Values("(concat max #b1)", setup);
  ASSERT_FALSE(overflow);
  EXPECT_EQ(overflow.error().code, DiagnosticCode::kResource);
  ASSERT_TRUE(overflow.error().node_error);
  EXPECT_EQ(overflow.error().node_error->resource, ResourceKind::kSize);
}

TEST(FrontendLiterals, DecimalBvIdentifierDigitsDifferFromNumeralTokens) {
  // The leading zero is inside a symbol in the first fixture and inside a
  // standalone numeral token in the second.
  ExpectError(Values("(_ bv1 08)"), DiagnosticCode::kSyntax);
  constexpr std::string_view input = "(_ bv01 8) #x01";
  SCOPED_TRACE(input);
  const auto terms = Require(Values(input));
  ASSERT_EQ(terms.values.size(), 2);
  EXPECT_EQ(terms.values[0].term, terms.values[1].term);
  EXPECT_EQ(terms.values[0].term.Borrow().GetSort().GetWidth(), 8u);
  EXPECT_EQ(BvValue(terms.values[0].term.Borrow()), "1");
}

TEST(FrontendOperators, FreeFunctionPreservesMixedOrderedRankAndCanonicalName) {
  constexpr std::string_view setup =
      "(set-logic ALL)(declare-fun mix (Bool (_ BitVec 4) Int) (Array Int "
      "Bool))";
  std::shared_ptr<const QueryContext> query;
  const auto command = Require(Values("(|mix| true #xf 3)", setup, &query));
  ASSERT_EQ(query->GetFunctions().size(), 1);
  const auto term = command.values.at(0).term.Borrow();
  EXPECT_EQ(Applied(term), query->GetFunctions()[0].Borrow());
  ASSERT_EQ(term.GetChildren().size(), 3);
  EXPECT_EQ(term.GetChildren()[0].GetValue()->GetBool(), true);
  EXPECT_EQ(term.GetChildren()[1].GetSort().GetWidth(), 4u);
  EXPECT_EQ(IntegerValue(term.GetChildren()[2]), "3");
  EXPECT_EQ(term.GetSort().GetArrayIndex()->GetKind(), SortKind::kInt);
  EXPECT_EQ(term.GetSort().GetArrayElement()->GetKind(), SortKind::kBool);
  ExpectError(Values("(mix true #xf false)", setup),
              DiagnosticCode::kInvalidInput);
  ExpectError(Parse(std::string(setup) + "(declare-fun |mix| (Int) Bool)"),
              DiagnosticCode::kInvalidInput);
}

}  // namespace
}  // namespace dandelion::frontend::testing

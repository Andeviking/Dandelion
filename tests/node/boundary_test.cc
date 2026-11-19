#include "test_support.h"

namespace dandelion::testing {
namespace {

class Boundary : public NodeTest {};

TEST_F(Boundary, EveryIndexedKindRejectsNegativeIndicesWithoutTruncation) {
  const auto v = Bv(8);
  const auto f = Fp(8, 24);
  const auto x = Const(v.Borrow());
  const auto y = Const(f.Borrow());
  const auto rm = Take(manager_.MakeRoundingMode(RoundingMode::kRne));
  const auto z = Int(1);
  const auto packed_sort = Bv(32);
  const auto packed = Const(packed_sort.Borrow());
  const Integer one(1), minus(-1), eight(8), twenty_four(24);
  const auto rational = Take(Rational::Make(one.Borrow(), one.Borrow()));
  for (const auto kind :
       {Kind::kIntDivisible, Kind::kBvRepeat, Kind::kBvZeroExtend,
        Kind::kBvSignExtend, Kind::kBvRotateLeft, Kind::kBvRotateRight,
        Kind::kIntToBv, Kind::kFpToSbv, Kind::kFpToUbv}) {
    const std::array idx = {minus};
    std::vector<NodeView> children = {x.Borrow()};
    if (kind == Kind::kIntDivisible || kind == Kind::kIntToBv)
      children = {z.Borrow()};
    if (kind == Kind::kFpToSbv || kind == Kind::kFpToUbv)
      children = {rm.Borrow(), y.Borrow()};
    Error(manager_.MakeBuiltin({kind, idx, {}, {}}, children),
          ErrorCode::kInvalidParameter);
  }
  for (const auto kind : {Kind::kFpFromBits, Kind::kFpConvert, Kind::kFpFromSbv,
                          Kind::kFpFromUbv, Kind::kFpFromRational}) {
    for (int bad = 0; bad < 2; ++bad) {
      const std::array idx = {bad == 0 ? minus : eight,
                              bad == 1 ? minus : twenty_four};
      std::vector<NodeView> children = {packed.Borrow()};
      if (kind == Kind::kFpConvert) children = {rm.Borrow(), y.Borrow()};
      if (kind == Kind::kFpFromSbv || kind == Kind::kFpFromUbv)
        children = {rm.Borrow(), x.Borrow()};
      if (kind == Kind::kFpFromRational) children = {rm.Borrow()};
      Error(manager_.MakeBuiltin({kind,
                                  idx,
                                  {},
                                  kind == Kind::kFpFromRational
                                      ? std::optional(rational.Borrow())
                                      : std::nullopt},
                                 children),
            ErrorCode::kInvalidParameter);
    }
  }
  for (const auto& idx : {std::array{minus, one}, std::array{one, minus}}) {
    const std::array children = {x.Borrow()};
    Error(manager_.MakeBuiltin({Kind::kBvExtract, idx, {}, {}}, children),
          ErrorCode::kInvalidParameter);
  }
}

TEST_F(Boundary, ExtractEndpointsAndExactLargeUnboundedIndices) {
  NodeTestPeer::ForceCollisions(manager_, 15);
  const auto v = Bv(8);
  const auto x = Const(v.Borrow());
  const auto z = Int(0);
  const Integer zero(0), one(1), three(3), four(4), seven(7), eight(8);
  const auto huge = Number("18446744073709551617");
  const std::array low = {three, zero};
  const std::array high = {seven, four};
  const auto first = Op(Kind::kBvExtract, {x.Borrow()}, low);
  const auto second = Op(Kind::kBvExtract, {x.Borrow()}, high);
  EXPECT_EQ(first.GetSort(), Bv(4));
  EXPECT_EQ(first.GetSort(), second.GetSort());
  EXPECT_NE(first, second);
  const std::array endpoint = {zero, zero};
  EXPECT_EQ(Op(Kind::kBvExtract, {x.Borrow()}, endpoint).GetSort(), Bv(1));
  for (const auto& idx : {std::array{eight, zero}, std::array{three, four},
                          std::array{huge, zero}}) {
    const std::array children = {x.Borrow()};
    Error(manager_.MakeBuiltin({Kind::kBvExtract, idx, {}, {}}, children),
          ErrorCode::kInvalidParameter);
  }
  for (const auto kind : {Kind::kBvRotateLeft, Kind::kBvRotateRight}) {
    const std::array small = {one};
    const std::array big = {huge};
    const auto a = Op(kind, {x.Borrow()}, small);
    const auto b = Op(kind, {x.Borrow()}, big);
    EXPECT_NE(a, b);  // 18446744073709551617 mod 8 = 1, but keys differ.
    EXPECT_EQ(b.Borrow().GetIndices()[0].ToDecimal(), "18446744073709551617");
    const std::array identity = {zero};
    EXPECT_NE(Op(kind, {x.Borrow()}, identity), x);
  }
  const std::array divisible = {huge};
  EXPECT_EQ(View(Op(Kind::kIntDivisible, {z.Borrow()}, divisible))
                .GetIndices()[0]
                .ToDecimal(),
            "18446744073709551617");
}

TEST_F(Boundary, ParameterRangesPreserveNarrowAndWideValues) {
  NodeTestPeer::ForceCollisions(manager_, 5);
  const auto x = Const(View(Bv(8)));
  const auto y = Int(12);
  const std::array narrow{Number("4294967295")};
  const std::array wide{Number("4294967296")};
  const auto start = manager_.GetStorageStats()->node_bytes;
  const auto a = Op(Kind::kBvRotateLeft, {x.Borrow()}, narrow);
  const auto after_narrow = manager_.GetStorageStats()->node_bytes;
  const auto b = Op(Kind::kBvRotateLeft, {x.Borrow()}, wide);
  EXPECT_LT(after_narrow - start,
            manager_.GetStorageStats()->node_bytes - after_narrow);
  EXPECT_NE(a, b);
  EXPECT_EQ(b, Op(Kind::kBvRotateLeft, {x.Borrow()}, wide));
  EXPECT_EQ(a.Borrow().GetIndices()[0].ToDecimal(), "4294967295");
  EXPECT_EQ(b.Borrow().GetIndices()[0].ToDecimal(), "4294967296");
  const std::array huge{Number("18446744073709551617")};
  const auto divisible = Op(Kind::kIntDivisible, {y.Borrow()}, huge);
  EXPECT_EQ(divisible.Borrow().GetIndices()[0].ToDecimal(),
            "18446744073709551617");
  const std::array parameters{Integer(6), Integer(3)};
  const auto extract = Op(Kind::kBvExtract, {x.Borrow()}, parameters);
  std::vector<std::string> values;
  for (const auto& index : extract.Borrow().GetIndices())
    values.push_back(index.ToDecimal());
  EXPECT_EQ(values, (std::vector<std::string>{"6", "3"}));
  const auto rm = Take(manager_.MakeRoundingMode(RoundingMode::kRne));
  const std::array format{Integer(8), Integer(24)};
  const auto rational = Take(Rational::Make(IntegerView(-6), IntegerView(4)));
  const auto converted =
      Op(Kind::kFpFromRational, {rm.Borrow()}, format, {}, rational.Borrow());
  EXPECT_EQ(converted.Borrow().GetIndices()[1].ToDecimal(), "24");
  EXPECT_EQ(converted.Borrow().GetRationalParameter()->Numerator().ToDecimal(),
            "-3");
  EXPECT_EQ(
      converted.Borrow().GetRationalParameter()->Denominator().ToDecimal(),
      "2");
}

TEST_F(Boundary, ComputedWidthsAndZeroBoundaries) {
  const auto huge_sort = Bv(UINT32_MAX);
  const auto big = Const(huge_sort.Borrow());
  const auto small_sort = Bv(1);
  const auto small = Const(small_sort.Borrow());
  const auto x_sort = Bv(8);
  const auto x = Const(x_sort.Borrow());
  const auto z = Int(1);
  const auto f = Fp(2, 2);
  const auto fp = Const(f.Borrow());
  const auto rm = Take(manager_.MakeRoundingMode(RoundingMode::kRtz));
  const Integer zero(0), one(1), two(2);
  const auto beyond = Number("4294967296");
  const std::array concat = {big.Borrow(), small.Borrow()};
  Resource(manager_.MakeBuiltin({Kind::kBvConcat, {}, {}, {}}, concat),
           ResourceKind::kSize);
  for (const auto kind :
       {Kind::kBvRepeat, Kind::kBvZeroExtend, Kind::kBvSignExtend}) {
    const std::array idx = {kind == Kind::kBvRepeat ? two : one};
    const std::array child = {big.Borrow()};
    Resource(manager_.MakeBuiltin({kind, idx, {}, {}}, child),
             ResourceKind::kSize);
  }
  for (const auto kind : {Kind::kBvRepeat, Kind::kIntDivisible, Kind::kIntToBv,
                          Kind::kFpToSbv, Kind::kFpToUbv}) {
    const std::array idx = {zero};
    std::vector<NodeView> children = {x.Borrow()};
    if (kind == Kind::kIntDivisible || kind == Kind::kIntToBv)
      children = {z.Borrow()};
    if (kind == Kind::kFpToSbv || kind == Kind::kFpToUbv)
      children = {rm.Borrow(), fp.Borrow()};
    Error(manager_.MakeBuiltin({kind, idx, {}, {}}, children),
          ErrorCode::kInvalidParameter);
  }
  for (const auto kind : {Kind::kBvZeroExtend, Kind::kBvSignExtend}) {
    const std::array idx = {zero};
    EXPECT_EQ(Op(kind, {x.Borrow()}, idx).GetSort(), x_sort);
    EXPECT_NE(Op(kind, {x.Borrow()}, idx), x);
  }
  for (const auto kind :
       {Kind::kIntToBv, Kind::kFpToSbv, Kind::kFpToUbv, Kind::kBvRepeat,
        Kind::kBvZeroExtend, Kind::kBvSignExtend}) {
    const std::array idx = {beyond};
    std::vector<NodeView> children = {x.Borrow()};
    if (kind == Kind::kIntToBv) children = {z.Borrow()};
    if (kind == Kind::kFpToSbv || kind == Kind::kFpToUbv)
      children = {rm.Borrow(), fp.Borrow()};
    Resource(manager_.MakeBuiltin({kind, idx, {}, {}}, children),
             ResourceKind::kSize);
  }
}

TEST_F(Boundary, FpFormatsConstructionConversionAndArrayQualifiers) {
  const auto f = Fp(2, 2);
  const auto g = Fp(2, 3);
  const auto x = Const(f.Borrow());
  const auto y = Const(g.Borrow());
  const auto rm = Take(manager_.MakeRoundingMode(RoundingMode::kRna));
  const Integer two(2);
  const std::array format = {two, two};
  EXPECT_EQ(Op(Kind::kFpConvert, {rm.Borrow(), y.Borrow()}, format).GetSort(),
            f);
  for (const auto kind :
       {Kind::kFpAdd, Kind::kFpSub, Kind::kFpMul, Kind::kFpDiv, Kind::kFpFma,
        Kind::kFpRem, Kind::kFpMin, Kind::kFpMax, Kind::kFpEq, Kind::kFpLt,
        Kind::kFpLeq, Kind::kFpGt, Kind::kFpGeq}) {
    std::vector<NodeView> children = {x.Borrow(), y.Borrow()};
    if (kind >= Kind::kFpAdd && kind <= Kind::kFpDiv)
      children.insert(children.begin(), rm.Borrow());
    if (kind == Kind::kFpFma)
      children = {rm.Borrow(), x.Borrow(), x.Borrow(), y.Borrow()};
    Error(manager_.MakeBuiltin({kind, {}, {}, {}}, children),
          ErrorCode::kSortMismatch);
  }
  const auto b = Take(manager_.BoolSort());
  const auto z = Take(manager_.IntSort());
  const auto array = Take(manager_.MakeArraySort(z.Borrow(), f.Borrow()));
  const std::array child = {y.Borrow()};
  Error(
      manager_.MakeBuiltin({Kind::kArrayConst, {}, array.Borrow(), {}}, child),
      ErrorCode::kSortMismatch);
  const std::array valid_child = {x.Borrow()};
  Error(manager_.MakeBuiltin({Kind::kArrayConst, {}, b.Borrow(), {}},
                             valid_child),
        ErrorCode::kSortMismatch);
  auto other = Take(NodeManager::Create());
  const auto foreign = Take(other.MakeArraySort(View(Take(other.IntSort())),
                                                View(Take(other.BoolSort()))));
  Error(manager_.MakeBuiltin({Kind::kArrayConst, {}, foreign.Borrow(), {}},
                             valid_child),
        ErrorCode::kForeignManager);
  const auto sign_sort = Bv(1);
  const auto exp_sort = Bv(2);
  const auto frac_sort = Bv(1);
  const auto sign = Const(sign_sort.Borrow());
  const auto exp = Const(exp_sort.Borrow());
  const auto frac = Const(frac_sort.Borrow());
  EXPECT_EQ(Op(Kind::kFpConstruct, {sign.Borrow(), exp.Borrow(), frac.Borrow()})
                .GetSort(),
            f);
  const std::array wrong_sign = {exp.Borrow(), exp.Borrow(), frac.Borrow()};
  Error(manager_.MakeBuiltin({Kind::kFpConstruct, {}, {}, {}}, wrong_sign),
        ErrorCode::kSortMismatch);
}

TEST_F(Boundary, DivisionPowerAndUnderspecifiedOperationsAreRetained) {
  const auto zero = Int(0);
  const auto minus_one = Int(-1);
  for (const auto kind : {Kind::kIntDiv, Kind::kIntMod, Kind::kIntPow}) {
    const auto node = Op(kind, {minus_one.Borrow(), zero.Borrow()});
    EXPECT_EQ(node.Borrow().GetKind(), kind);
    EXPECT_EQ(node.Borrow().GetChildren()[1].GetId(), zero.GetId());
  }
  for (const auto width : {1, 2, 8, 32, 64, 65}) {
    const auto sort = Bv(width);
    const Integer value(0);
    const auto bv = Take(manager_.MakeBvValue(sort.Borrow(), value.Borrow()));
    for (const auto kind : {Kind::kBvUdiv, Kind::kBvUrem, Kind::kBvSdiv,
                            Kind::kBvSrem, Kind::kBvSmod}) {
      EXPECT_EQ(View(Op(kind, {bv.Borrow(), bv.Borrow()})).GetKind(), kind);
    }
  }
}

}  // namespace
}  // namespace dandelion::testing

namespace dandelion::testing {

TEST_F(Boundary, HeterogeneousConcatAndFpComponentWidthLimits) {
  const auto v1 = Bv(1), v2 = Bv(2), v65 = Bv(65), huge = Bv(UINT32_MAX);
  const auto one = Const(v1.Borrow()), two = Const(v2.Borrow()),
             wide = Const(v65.Borrow()), big = Const(huge.Borrow());
  EXPECT_EQ(Op(Kind::kBvConcat, {one.Borrow(), wide.Borrow()}).GetSort(),
            Bv(66));
  const std::array invalid_format = {one.Borrow(), one.Borrow(), one.Borrow()};
  Error(manager_.MakeBuiltin({Kind::kFpConstruct, {}, {}, {}}, invalid_format),
        ErrorCode::kInvalidParameter);
  const std::array overflow = {one.Borrow(), two.Borrow(), big.Borrow()};
  Resource(manager_.MakeBuiltin({Kind::kFpConstruct, {}, {}, {}}, overflow),
           ResourceKind::kSize);
  const auto zero = Int(0), negative = Int(-1);
  const auto power = Op(Kind::kIntPow, {zero.Borrow(), negative.Borrow()});
  EXPECT_EQ(power.Borrow().GetKind(), Kind::kIntPow);
  EXPECT_EQ(power.Borrow().GetChildren()[1], negative.Borrow());
}

}  // namespace dandelion::testing

namespace dandelion::testing {

TEST_F(Boundary,
       EveryPositiveWidthIndexAcceptsOneAndTheLargestRepresentableWidth) {
  const auto z = Int(-1);
  const auto fp_sort = Fp(2, 2);
  const auto fp = Const(fp_sort.Borrow());
  const auto rm = Take(manager_.MakeRoundingMode(RoundingMode::kRne));
  for (const auto width : {std::uint32_t{1}, UINT32_MAX}) {
    const std::array indices = {Integer(width)};
    EXPECT_EQ(Op(Kind::kIntToBv, {z.Borrow()}, indices).GetSort(), Bv(width));
    for (const auto kind : {Kind::kFpToSbv, Kind::kFpToUbv}) {
      EXPECT_EQ(Op(kind, {rm.Borrow(), fp.Borrow()}, indices).GetSort(),
                Bv(width));
    }
  }
  const auto bv_sort = Bv(8);
  const auto bv = Const(bv_sort.Borrow());
  const std::array one = {Integer(1)};
  const auto repeat = Op(Kind::kBvRepeat, {bv.Borrow()}, one);
  EXPECT_EQ(repeat.GetSort(), bv_sort);
  EXPECT_NE(repeat, bv);
  EXPECT_EQ(View(Op(Kind::kIntDivisible, {z.Borrow()}, one)).GetKind(),
            Kind::kIntDivisible);
}

TEST_F(Boundary, ExtractEndpointAndWideIndexChangesAreIndependent) {
  NodeTestPeer::ForceCollisions(manager_, 15);
  const auto word = Const(View(Bv(8)));
  const auto extract = [&](std::int64_t high, std::int64_t low) {
    return Op(Kind::kBvExtract, {word.Borrow()},
              std::array{Integer(high), Integer(low)});
  };
  const auto original = extract(6, 1);
  const auto high_only = extract(7, 1);
  const auto low_only = extract(6, 0);
  EXPECT_NE(original, high_only);
  EXPECT_NE(original, low_only);
  EXPECT_NE(high_only, low_only);
  EXPECT_EQ(original, extract(6, 1));
  EXPECT_EQ(high_only, extract(7, 1));
  EXPECT_EQ(low_only, extract(6, 0));
  const std::array first{Number("18446744073709551617")};
  const std::array second{Number("18446744073709551618")};
  const auto left = Op(Kind::kBvRotateLeft, {word.Borrow()}, first);
  const auto right = Op(Kind::kBvRotateLeft, {word.Borrow()}, second);
  EXPECT_NE(left, right);
  EXPECT_EQ(left, Op(Kind::kBvRotateLeft, {word.Borrow()}, first));
  EXPECT_EQ(right, Op(Kind::kBvRotateLeft, {word.Borrow()}, second));
}

TEST_F(Boundary, BuiltinRejectsCompatibleCategoriesWithWrongWidths) {
  const auto bv8 = Bv(8);
  const auto bv16 = Bv(16);
  const auto eight = Const(bv8.Borrow());
  const auto sixteen = Const(bv16.Borrow());
  const auto fp = Const(View(Fp(8, 24)));
  const auto fp_other = Const(View(Fp(7, 25)));
  const auto z = Take(manager_.IntSort());
  const auto index = Int(0);
  const auto array = Take(manager_.MakeArraySort(z.Borrow(), bv8.Borrow()));
  const auto arr = Const(array.Borrow());
  const std::array format{Integer(8), Integer(24)};
  const auto check = [&](BuiltinOpView op, std::span<const NodeView> operands,
                         ErrorCode code) {
    Error(manager_.CheckBuiltin(op, operands), code);
    Error(manager_.MakeBuiltin(op, operands), code);
  };
  check({Kind::kBvAdd, {}, {}, {}},
        std::array{eight.Borrow(), eight.Borrow(), sixteen.Borrow()},
        ErrorCode::kSortMismatch);
  for (const auto kind : {Kind::kEqual, Kind::kFpEq}) {
    check({kind, {}, {}, {}},
          std::array{fp.Borrow(), fp.Borrow(), fp_other.Borrow()},
          ErrorCode::kSortMismatch);
  }
  check({Kind::kFpFromBits, format, {}, {}}, std::array{sixteen.Borrow()},
        ErrorCode::kSortMismatch);
  check({Kind::kArrayConst, {}, SortView{}, {}}, std::array{eight.Borrow()},
        ErrorCode::kNullObject);
  check({Kind::kArrayStore, {}, {}, {}},
        std::array{arr.Borrow(), index.Borrow(), sixteen.Borrow()},
        ErrorCode::kSortMismatch);
}

}  // namespace dandelion::testing

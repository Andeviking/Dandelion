#include "test_support.h"

namespace dandelion::testing {
namespace {

TEST(Numeric, DecimalGrammarAndCanonicalSmallStorage) {
  for (const auto text : {"", "-", "+1", " 1", "1 ", "1a", "1.0", "--1"}) {
    Error(Integer::ParseDecimal(text), ErrorCode::kInvalidLiteral);
  }
  EXPECT_EQ(Number("-000").ToDecimal(), "0");
  EXPECT_EQ(Number("00042").ToDecimal(), "42");
  for (const auto value : {std::numeric_limits<std::int64_t>::min(),
                           std::int64_t{-1}, std::int64_t{0}, std::int64_t{1},
                           std::numeric_limits<std::int64_t>::max()}) {
    const Integer small(value);
    const auto parsed = Number(std::to_string(value));
    EXPECT_TRUE(parsed.Borrow().IsSmall());
    EXPECT_EQ(parsed.Borrow().Compare(small.Borrow()), 0);
    EXPECT_EQ(parsed.Borrow().Hash(), small.Borrow().Hash());
    EXPECT_EQ(parsed.Borrow().TryInt64(), value);
  }
  const auto min = Number("-9223372036854775808");
  EXPECT_EQ(min.Borrow().BitLength(), 64);
  EXPECT_EQ(min.Borrow().MagnitudeChunk(0), std::uint64_t{1} << 63);
}

TEST(Numeric, ExactLargeValuesNarrowingAndReadOnlyViews) {
  const std::array texts = {"-9223372036854775809", "9223372036854775808",
                            "18446744073709551616",
                            "340282366920938463463374607431768211456"};
  const std::array<std::uint64_t, 4> lengths = {64, 64, 65, 129};
  for (std::size_t i = 0; i < texts.size(); ++i) {
    auto value = Number(texts[i]);
    EXPECT_FALSE(value.Borrow().IsSmall());
    EXPECT_EQ(value.ToDecimal(), texts[i]);
    EXPECT_EQ(value.Borrow().BitLength(), lengths[i]);
    EXPECT_FALSE(value.Borrow().TryInt64());
    auto copy = value;
    value = Integer(7);
    EXPECT_EQ(copy.ToDecimal(), texts[i]);
    const auto& alias = copy;
    copy = alias;
    auto& move_alias = copy;
    copy = std::move(move_alias);
    EXPECT_EQ(copy.ToDecimal(), texts[i]);
    const auto moved = std::move(copy);
    EXPECT_EQ(moved.ToDecimal(), texts[i]);
  }
  EXPECT_EQ(View(Number("4294967295")).TryUint32(), UINT32_MAX);
  EXPECT_FALSE(View(Number("4294967296")).TryUint32());
  EXPECT_EQ(View(Number("18446744073709551615")).TryUint64(), UINT64_MAX);
  EXPECT_FALSE(View(Number("18446744073709551616")).TryUint64());
  EXPECT_FALSE(View(Number("-1")).TryUint64());
  EXPECT_EQ(
      View(Number("340282366920938463463374607431768211456")).MagnitudeChunk(2),
      1);
  EXPECT_EQ(View(Number("0")).BitLength(), 0);
}

TEST(Numeric, RationalNormalizationOwnsItsArguments) {
  auto numerator = Number("-200000000000000000000000000000000000000");
  auto denominator = Number("-300000000000000000000000000000000000000");
  auto rational =
      Take(Rational::Make(numerator.Borrow(), denominator.Borrow()));
  numerator = Integer(9);
  denominator = Integer(0);
  EXPECT_EQ(rational.Borrow().Numerator().ToDecimal(), "2");
  EXPECT_EQ(rational.Borrow().Denominator().ToDecimal(), "3");
  auto zero = Take(Rational::Make(denominator.Borrow(), numerator.Borrow()));
  EXPECT_EQ(zero.Borrow().Numerator().ToDecimal(), "0");
  EXPECT_EQ(zero.Borrow().Denominator().ToDecimal(), "1");
  Error(Rational::Make(numerator.Borrow(), denominator.Borrow()),
        ErrorCode::kInvalidParameter);
  auto copy = rational;
  rational = std::move(zero);
  EXPECT_EQ(copy.Borrow().Numerator().ToDecimal(), "2");
}

}  // namespace
}  // namespace dandelion::testing

namespace dandelion::testing {

TEST(NumericFailure, ControlledCppAllocationDoesNotSimulateGmpLimbFailure) {
  const auto big = Number("340282366920938463463374607431768211456");
  const auto denominator = Number("340282366920938463463374607431768211457");
  bool parsed = false;
  std::size_t failures = 0;
  for (std::size_t checkpoint = 0; checkpoint < 16; ++checkpoint) {
    NumericTestPeer::FailAllocationAfter(checkpoint);
    auto result =
        Integer::ParseDecimal("340282366920938463463374607431768211456");
    NumericTestPeer::ClearFailures();
    if (result) {
      parsed = true;
      break;
    }
    Resource(result, ResourceKind::kCppAllocation);
    ++failures;
  }
  EXPECT_TRUE(parsed);
  EXPECT_GT(failures, 0);
  bool normalized = false;
  failures = 0;
  for (std::size_t checkpoint = 0; checkpoint < 16; ++checkpoint) {
    NumericTestPeer::FailAllocationAfter(checkpoint);
    auto result = Rational::Make(big.Borrow(), denominator.Borrow());
    NumericTestPeer::ClearFailures();
    if (result) {
      normalized = true;
      break;
    }
    Resource(result, ResourceKind::kCppAllocation);
    ++failures;
  }
  EXPECT_TRUE(normalized);
  EXPECT_GE(failures, 2);
  NumericTestPeer::FailAllocationAfter(0);
  EXPECT_THROW((void)big.ToDecimal(), std::bad_alloc);
  NumericTestPeer::ClearFailures();
  NumericTestPeer::FailAllocationAfter(0);
  EXPECT_THROW((void)big.Borrow().Copy(), std::bad_alloc);
  NumericTestPeer::ClearFailures();
  const auto source = Take(Rational::Make(big.Borrow(), denominator.Borrow()));
  const Integer one(1), two(2);
  auto target = Take(Rational::Make(one.Borrow(), two.Borrow()));
  NumericTestPeer::FailAllocationAfter(1);
  EXPECT_THROW(target = source, std::bad_alloc);
  NumericTestPeer::ClearFailures();
  EXPECT_EQ(target.Borrow().Numerator().ToDecimal(), "1");
  EXPECT_EQ(target.Borrow().Denominator().ToDecimal(), "2");
  NumericTestPeer::FailAllocationAfter(0);
  EXPECT_THROW((void)Integer(big), std::bad_alloc);
  NumericTestPeer::ClearFailures();
  EXPECT_EQ(big.ToDecimal(), "340282366920938463463374607431768211456");
  target = source;
  const auto& alias = target;
  target = alias;
  auto& move_alias = target;
  target = std::move(move_alias);
  EXPECT_EQ(target.Borrow().Numerator().Compare(big.Borrow()), 0);
  EXPECT_EQ(target.Borrow().Denominator().Compare(denominator.Borrow()), 0);
}

}  // namespace dandelion::testing

namespace dandelion::testing {

TEST_F(NodeTest,
       MovedFromRationalsStayCanonicalThroughMovesAndNodePublication) {
  const Integer one(1), two(2), three(3);
  auto original = Take(Rational::Make(one.Borrow(), two.Borrow()));
  NumericTestPeer::FailAllocationAfter(0);
  auto constructed = std::move(original);
  NumericTestPeer::ClearFailures();
  EXPECT_EQ(original.Borrow().Numerator().ToDecimal(), "0");
  EXPECT_EQ(original.Borrow().Denominator().ToDecimal(), "1");
  EXPECT_EQ(constructed.Borrow().Numerator().ToDecimal(), "1");
  EXPECT_EQ(constructed.Borrow().Denominator().ToDecimal(), "2");
  auto assigned = Take(Rational::Make(two.Borrow(), three.Borrow()));
  NumericTestPeer::FailAllocationAfter(0);
  assigned = std::move(constructed);
  NumericTestPeer::ClearFailures();
  EXPECT_EQ(constructed.Borrow().Numerator().ToDecimal(), "0");
  EXPECT_EQ(constructed.Borrow().Denominator().ToDecimal(), "1");
  auto& alias = assigned;
  assigned = std::move(alias);
  EXPECT_EQ(assigned.Borrow().Numerator().ToDecimal(), "1");
  EXPECT_EQ(assigned.Borrow().Denominator().ToDecimal(), "2");
  const auto rounding = Take(manager_.MakeRoundingMode(RoundingMode::kRne));
  const std::array indices = {two, two};
  const auto conversion = Op(Kind::kFpFromRational, {rounding.Borrow()},
                             indices, {}, constructed.Borrow());
  EXPECT_EQ(conversion.Borrow().GetRationalParameter()->Numerator().ToDecimal(),
            "0");
  EXPECT_EQ(
      conversion.Borrow().GetRationalParameter()->Denominator().ToDecimal(),
      "1");
  const auto big = Number("340282366920938463463374607431768211456");
  const auto denominator = Number("340282366920938463463374607431768211457");
  auto big_original = Take(Rational::Make(big.Borrow(), denominator.Borrow()));
  NumericTestPeer::FailAllocationAfter(0);
  auto big_moved = std::move(big_original);
  NumericTestPeer::ClearFailures();
  EXPECT_EQ(big_moved.Borrow().Numerator().Compare(big.Borrow()), 0);
  EXPECT_EQ(big_original.Borrow().Denominator().ToDecimal(), "1");
  NumericTestPeer::FailAllocationAfter(0);
  assigned = std::move(big_moved);
  NumericTestPeer::ClearFailures();
  EXPECT_EQ(assigned.Borrow().Denominator().Compare(denominator.Borrow()), 0);
  EXPECT_EQ(big_moved.Borrow().Numerator().ToDecimal(), "0");
  EXPECT_EQ(big_moved.Borrow().Denominator().ToDecimal(), "1");
}

TEST(NumericInput, RadixImportsPreserveMagnitudeAcrossLimbs) {
  const auto binary = Take(Integer::ParseRadix("1" + std::string(64, '0'), 2));
  const auto hex = Take(Integer::ParseRadix("10000000000000000", 16));
  EXPECT_EQ(binary.ToDecimal(), "18446744073709551616");
  EXPECT_EQ(binary.Borrow().Compare(hex.Borrow()), 0);
  EXPECT_EQ(Take(Integer::ParseRadix("000aB", 16)).ToDecimal(), "171");
  EXPECT_EQ(Take(Integer::ParseRadix("0000", 2)).ToDecimal(), "0");
  for (auto invalid : {"", "-1", " 1", "+1", "#b1", "1 ", "2"})
    Error(Integer::ParseRadix(invalid, 2), ErrorCode::kInvalidLiteral);
  Error(Integer::ParseRadix("g", 16), ErrorCode::kInvalidLiteral);
  Error(Integer::ParseRadix("1", 10), ErrorCode::kInvalidParameter);
}

TEST(NumericInput, DecimalFractionsNormalizeExactly) {
  const auto reduced = Take(Rational::ParseDecimalFraction("0.0125"));
  EXPECT_EQ(reduced.Borrow().Numerator().ToDecimal(), "1");
  EXPECT_EQ(reduced.Borrow().Denominator().ToDecimal(), "80");
  const auto zero = Take(Rational::ParseDecimalFraction("00.000"));
  EXPECT_EQ(zero.Borrow().Numerator().ToDecimal(), "0");
  EXPECT_EQ(zero.Borrow().Denominator().ToDecimal(), "1");
  const auto wide =
      Take(Rational::ParseDecimalFraction("0.000000000000000000001"));
  EXPECT_EQ(wide.Borrow().Numerator().ToDecimal(), "1");
  EXPECT_EQ(wide.Borrow().Denominator().ToDecimal(), "1000000000000000000000");
  for (auto invalid : {"", "3", "-0.5", "+0.5", ".3", "3.", " 3.0", "1.2.3"})
    Error(Rational::ParseDecimalFraction(invalid), ErrorCode::kInvalidLiteral);
}

TEST(NumericInput, SignAndDivisionPreserveCanonicalForm) {
  const Integer minimum(std::numeric_limits<std::int64_t>::min()), zero;
  const auto positive = Take(Integer::Negate(minimum.Borrow()));
  EXPECT_EQ(positive.ToDecimal(), "9223372036854775808");
  EXPECT_EQ(Take(Integer::Negate(positive.Borrow())).ToDecimal(),
            "-9223372036854775808");
  EXPECT_EQ(Take(Integer::Negate(zero.Borrow())).ToDecimal(), "0");
  const auto five_quarters = Take(Rational::ParseDecimalFraction("1.25"));
  const auto five_halves = Take(Rational::ParseDecimalFraction("2.5"));
  const auto negative = Take(Rational::Negate(five_quarters.Borrow()));
  const auto half =
      Take(Rational::Divide(negative.Borrow(), five_halves.Borrow()));
  EXPECT_EQ(half.Borrow().Numerator().ToDecimal(), "-1");
  EXPECT_EQ(half.Borrow().Denominator().ToDecimal(), "2");
  const auto reverse =
      Take(Rational::Divide(five_halves.Borrow(), negative.Borrow()));
  EXPECT_EQ(reverse.Borrow().Numerator().ToDecimal(), "-2");
  EXPECT_EQ(reverse.Borrow().Denominator().ToDecimal(), "1");
  const auto zero_fraction =
      Take(Rational::Make(zero.Borrow(), IntegerView(1)));
  const auto negative_zero = Take(Rational::Negate(zero_fraction.Borrow()));
  EXPECT_EQ(negative_zero.Borrow().Numerator().ToDecimal(), "0");
  EXPECT_EQ(negative_zero.Borrow().Denominator().ToDecimal(), "1");
  Error(Rational::Divide(five_halves.Borrow(), zero_fraction.Borrow()),
        ErrorCode::kInvalidParameter);
  const auto huge =
      Take(Rational::ParseDecimalFraction("18446744073709551616.0"));
  const auto negative_huge = Take(Rational::Negate(huge.Borrow()));
  EXPECT_EQ(negative_huge.Borrow().Numerator().ToDecimal(),
            "-18446744073709551616");
  EXPECT_EQ(huge.Borrow().Numerator().ToDecimal(), "18446744073709551616");
  EXPECT_EQ(five_quarters.Borrow().Numerator().ToDecimal(), "5");
}

TEST(NumericInput, ReportsCppFailureAndPreservesInputs) {
  const auto wide = Number("18446744073709551616");
  const auto fraction = Take(Rational::Make(wide.Borrow(), IntegerView(3)));
  const auto one = Take(Rational::Make(IntegerView(1), IntegerView(1)));
  NumericTestPeer::FailAllocationAfter(0);
  Resource(Integer::ParseRadix("10000000000000000", 16),
           ResourceKind::kCppAllocation);
  NumericTestPeer::FailAllocationAfter(0);
  Resource(Rational::ParseDecimalFraction("0.000000000000000000001"),
           ResourceKind::kCppAllocation);
  NumericTestPeer::FailAllocationAfter(0);
  Resource(Integer::Negate(wide.Borrow()), ResourceKind::kCppAllocation);
  NumericTestPeer::FailAllocationAfter(0);
  Resource(Rational::Negate(fraction.Borrow()), ResourceKind::kCppAllocation);
  NumericTestPeer::FailAllocationAfter(0);
  Resource(Rational::Divide(fraction.Borrow(), one.Borrow()),
           ResourceKind::kCppAllocation);
  NumericTestPeer::ClearFailures();
  EXPECT_EQ(fraction.Borrow().Numerator().ToDecimal(), "18446744073709551616");
  EXPECT_EQ(fraction.Borrow().Denominator().ToDecimal(), "3");
}

TEST(NumericRounding, TinyFormatAgainstIndependentNeighborTable) {
  // Finite magnitudes in units of 1/8, indexed by exponent and fraction.
  // The trailing 32 is the next exact binade, encoded as overflow.
  constexpr std::array grid{0, 2, 4, 6, 8, 10, 12, 14, 16, 20, 24, 28, 32};
  constexpr std::array modes{RoundingMode::kRne, RoundingMode::kRna,
                             RoundingMode::kRtp, RoundingMode::kRtn,
                             RoundingMode::kRtz};
  for (int value = -36; value <= 36; ++value) {
    SCOPED_TRACE(value);
    const auto number =
        Take(Rational::Make(IntegerView(value), IntegerView(8)));
    const auto magnitude = std::abs(value);
    const auto upper = std::lower_bound(grid.begin(), grid.end(), magnitude);
    const auto high = upper == grid.end()
                          ? grid.size() - 1
                          : static_cast<std::size_t>(upper - grid.begin());
    const auto low =
        high == grid.size() - 1 || grid[high] != magnitude ? high - 1 : high;
    for (auto mode : modes) {
      SCOPED_TRACE(std::to_underlying(mode));
      const auto rounded = Take(RoundRationalToFp(2, 3, mode, number.Borrow()));
      auto expected = low;
      if ((mode == RoundingMode::kRtp && value > 0) ||
          (mode == RoundingMode::kRtn && value < 0)) {
        expected = high;
      } else if (mode == RoundingMode::kRna || mode == RoundingMode::kRne) {
        const auto lower_distance = magnitude - grid[low];
        const auto upper_distance = grid[high] - magnitude;
        if (upper_distance < lower_distance ||
            (upper_distance == lower_distance &&
             (mode == RoundingMode::kRna || (high % 2) == 0))) {
          expected = high;
        }
      }
      EXPECT_EQ(rounded.negative, value < 0);
      EXPECT_EQ(rounded.infinite, expected == grid.size() - 1);
      if (!rounded.infinite) {
        EXPECT_EQ(rounded.exponent.Borrow().TryUint64(), expected / 4);
        EXPECT_EQ(rounded.fraction.Borrow().TryUint64(), expected % 4);
      }
    }
  }
}

TEST(NumericRounding, FormatValidationWideBiasAndAllocationFailure) {
  const auto one = Take(Rational::Make(IntegerView(1), IntegerView(1)));
  Error(RoundRationalToFp(1, 3, RoundingMode::kRne, one.Borrow()),
        ErrorCode::kInvalidParameter);
  Error(RoundRationalToFp(5, 1, RoundingMode::kRne, one.Borrow()),
        ErrorCode::kInvalidParameter);
  Error(RoundRationalToFp(5, 3, static_cast<RoundingMode>(255), one.Borrow()),
        ErrorCode::kInvalidParameter);
  Resource(RoundRationalToFp(std::numeric_limits<std::uint32_t>::max(), 3,
                             RoundingMode::kRne, one.Borrow()),
           ResourceKind::kSize);
  NumericTestPeer::FailAllocationAfter(0);
  Resource(RoundRationalToFp(65, 3, RoundingMode::kRne, one.Borrow()),
           ResourceKind::kCppAllocation);
  NumericTestPeer::ClearFailures();
  const auto rounded =
      Take(RoundRationalToFp(65, 3, RoundingMode::kRne, one.Borrow()));
  EXPECT_FALSE(rounded.negative);
  EXPECT_FALSE(rounded.infinite);
  EXPECT_EQ(rounded.exponent.ToDecimal(), "18446744073709551615");
  EXPECT_EQ(rounded.fraction.ToDecimal(), "0");
}

TEST(Numeric, OrdersValuesWithEqualUpperChunksAndZeroBeyondMagnitude) {
  const auto smaller = Number("340282366920938463481821351505477763073");
  const auto larger = Number("340282366920938463481821351505477763074");
  const auto a = smaller.Borrow();
  const auto b = larger.Borrow();
  EXPECT_EQ(a.BitLength(), 129);
  EXPECT_EQ(a.MagnitudeChunk(0), 1);
  EXPECT_EQ(b.MagnitudeChunk(0), 2);
  EXPECT_EQ(a.MagnitudeChunk(1), 1);
  EXPECT_EQ(b.MagnitudeChunk(1), 1);
  EXPECT_EQ(a.MagnitudeChunk(2), 1);
  EXPECT_EQ(b.MagnitudeChunk(2), 1);
  EXPECT_EQ(b.MagnitudeChunk(3), 0);
  EXPECT_EQ(b.MagnitudeChunk(UINT64_MAX), 0);
  EXPECT_LT(a.Compare(b), 0);
  EXPECT_GT(b.Compare(a), 0);
}

TEST(NumericRounding, EarlyUnderflowAndTieInWideBinade) {
  const std::array modes{RoundingMode::kRne, RoundingMode::kRna,
                         RoundingMode::kRtp, RoundingMode::kRtn,
                         RoundingMode::kRtz};
  for (const auto sign : {-1, 1}) {
    const auto fraction =
        Take(Rational::Make(IntegerView(sign), IntegerView(32)));
    for (const auto mode : modes) {
      const auto result =
          Take(RoundRationalToFp(2, 3, mode, fraction.Borrow()));
      EXPECT_EQ(result.negative, sign < 0);
      EXPECT_FALSE(result.infinite);
      EXPECT_EQ(result.exponent.ToDecimal(), "0");
      const bool away =
          mode == (sign < 0 ? RoundingMode::kRtn : RoundingMode::kRtp);
      EXPECT_EQ(result.fraction.ToDecimal(), away ? "1" : "0");
    }
  }
  // The FP(8,3) neighbors of 9 are 8 and 10, with fraction fields 00 and 01.
  const auto nine = Take(Rational::Make(IntegerView(9), IntegerView(1)));
  for (const auto mode : {RoundingMode::kRne, RoundingMode::kRna}) {
    const auto result = Take(RoundRationalToFp(8, 3, mode, nine.Borrow()));
    EXPECT_FALSE(result.negative);
    EXPECT_FALSE(result.infinite);
    EXPECT_EQ(result.exponent.ToDecimal(), "130");
    EXPECT_EQ(result.fraction.ToDecimal(),
              mode == RoundingMode::kRne ? "0" : "1");
  }
}

TEST(NumericRounding, ExactWideFractionSurvivesAllocationFailure) {
  const auto numerator = Number("55340232221128654849");
  const auto denominator = Number("36893488147419103232");
  const auto value =
      Take(Rational::Make(numerator.Borrow(), denominator.Borrow()));
  for (std::size_t checkpoint = 0; checkpoint < 2; ++checkpoint) {
    NumericTestPeer::FailAllocationAfter(checkpoint);
    const auto failed =
        RoundRationalToFp(65, 66, RoundingMode::kRne, value.Borrow());
    NumericTestPeer::ClearFailures();
    Resource(failed, ResourceKind::kCppAllocation);
    EXPECT_EQ(value.Borrow().Numerator().Compare(numerator.Borrow()), 0);
    EXPECT_EQ(value.Borrow().Denominator().Compare(denominator.Borrow()), 0);
  }
  const auto result =
      Take(RoundRationalToFp(65, 66, RoundingMode::kRne, value.Borrow()));
  EXPECT_FALSE(result.negative);
  EXPECT_FALSE(result.infinite);
  EXPECT_EQ(result.exponent.ToDecimal(), "18446744073709551615");
  EXPECT_EQ(result.fraction.ToDecimal(), "18446744073709551617");
}

}  // namespace dandelion::testing

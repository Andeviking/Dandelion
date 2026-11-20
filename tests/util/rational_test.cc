#include <array>
#include <limits>
#include <string>

#include "test_support.h"

namespace dandelion::testing::util {
namespace {

static_assert(sizeof(Rational) == 16);
static_assert(kNothrowOwnership<Rational>);
static_assert(!RvalueBorrowable<Rational>);
static_assert(!RvalueBorrowable<const Rational>);

constexpr auto kMin = std::numeric_limits<std::int64_t>::min();
constexpr auto kMax = std::numeric_limits<std::int64_t>::max();

Rational Fraction(std::string_view numerator, std::string_view denominator) {
  const auto n = Int(numerator);
  const auto d = Int(denominator);
  return Take(Rational::Make(n.Borrow(), d.Borrow()));
}

void ExpectRatio(const Rational& value, std::string_view numerator,
                 std::string_view denominator, bool inlined) {
  const auto view = value.Borrow();
  EXPECT_EQ(view.Numerator().ToDecimal(), numerator);
  EXPECT_EQ(view.Denominator().ToDecimal(), denominator);
  const std::string text = denominator == "1" ? std::string(numerator)
                                              : std::string(numerator) + "/" +
                                                    std::string(denominator);
  EXPECT_EQ(value.ToString(), text);
  EXPECT_EQ(view.BackingBytes() == 0, inlined);
}

TEST(UtilRational, CanonicalFormAndStorageBoundaries) {
  const auto half = Ratio(-2, -4);
  const auto equivalent = Ratio(3, 6);
  ExpectRatio(half, "1", "2", true);
  EXPECT_EQ(half.Borrow(), equivalent.Borrow());
  EXPECT_EQ(half.Borrow().Hash(), equivalent.Borrow().Hash());
  ExpectRatio(Ratio(2, -4), "-1", "2", true);
  ExpectRatio(Ratio(0, -9), "0", "1", true);
  ExpectRatio(Ratio(kMin), "-9223372036854775808", "1", true);
  ExpectRatio(Ratio(kMax), "9223372036854775807", "1", true);
  ExpectRatio(Ratio(kMin, kMin), "1", "1", true);
  ExpectRatio(Ratio(1, kMin), "-1", "9223372036854775808", true);
  ExpectRatio(Ratio(kMin, -1), "9223372036854775808", "1", false);
  ExpectRatio(Fraction("-9223372036854775809", "1"), "-9223372036854775809",
              "1", false);
  const auto word_denominator =
      Take(Rational::Make(IntegerView(1), Unsigned(UINT64_MAX)));
  ExpectRatio(word_denominator, "1", "18446744073709551615", true);
  const auto word_view = word_denominator.Borrow().Denominator();
  EXPECT_EQ(word_view.TryUint64(), UINT64_MAX);
  EXPECT_FALSE(word_view.TryInt64());
  EXPECT_FALSE(word_view.IsSmall());
  EXPECT_EQ(word_view.BackingBytes(), 0u);
  const auto wide_denominator = Fraction("1", "18446744073709551616");
  ExpectRatio(wide_denominator, "1", "18446744073709551616", false);
  EXPECT_EQ(wide_denominator.Borrow().Numerator(), IntegerView(1));
  EXPECT_EQ(wide_denominator.Borrow().Denominator().MagnitudeChunk(1), 1u);
  ExpectRatio(Fraction("-200000000000000000000000000000000000000",
                       "-300000000000000000000000000000000000000"),
              "2", "3", true);
  ExpectRatio(Fraction("0", "-340282366920938463463374607431768211456"), "0",
              "1", true);
  ExpectError(Rational::Make(IntegerView(1), IntegerView(0)),
              NumericError::kDivisionByZero);
  ExpectError(Rational::Make(IntegerView(0), IntegerView(0)),
              NumericError::kDivisionByZero);
  EXPECT_GT(half.Borrow().Sign(), 0);
  const auto negative = Ratio(-1, 2);
  const auto zero = Ratio(0);
  EXPECT_LT(negative.Borrow().Sign(), 0);
  EXPECT_EQ(zero.Borrow().Sign(), 0);
  EXPECT_NE(half.Borrow(), negative.Borrow());

  ExpectRatio(Take(Rational::FromInteger(IntegerView(kMin))),
              "-9223372036854775808", "1", true);
  const auto wide_integer = Int("340282366920938463463374607431768211456");
  ExpectRatio(Take(Rational::FromInteger(wide_integer.Borrow())),
              "340282366920938463463374607431768211456", "1", false);
}

TEST(UtilRational, DecimalFractionsAreExactAndUnsigned) {
  ExpectRatio(Take(Rational::ParseDecimalFraction("001.500")), "3", "2", true);
  ExpectRatio(Take(Rational::ParseDecimalFraction("00.000")), "0", "1", true);
  ExpectRatio(Take(Rational::ParseDecimalFraction("0125.0")), "125", "1", true);
  ExpectRatio(Take(Rational::ParseDecimalFraction("0.0125")), "1", "80", true);
  ExpectRatio(Take(Rational::ParseDecimalFraction("0.000000000000000000001")),
              "1", "1000000000000000000000", false);
  ExpectRatio(Take(Rational::ParseDecimalFraction("18446744073709551616.50")),
              "36893488147419103233", "2", false);
  constexpr std::array<std::string_view, 16> invalid = {
      "",      "3",     ".",     ".3",
      "3.",    "3.0.1", "3..0",  "-0.5",
      "+0.5",  " 3.0",  "3.0 ",  "3. 0",
      "0x1.2", "#b1.1", "1.5e1", std::string_view("1.0\0", 4)};
  for (auto text : invalid) {
    SCOPED_TRACE(::testing::PrintToString(text));
    ExpectError(Rational::ParseDecimalFraction(text),
                NumericError::kInvalidText);
  }
}

TEST(UtilRational, ExactArithmeticAndOrderingAcrossStorageForms) {
  const auto sixth = Ratio(1, 6);
  const auto third = Ratio(1, 3);
  const auto negative = Ratio(-2, 3);
  const auto two_thirds = Ratio(2, 3);
  const auto nine_quarters = Ratio(9, 4);
  const auto negative_four_fifths = Ratio(-4, 5);
  ExpectRatio(Take(sixth.Borrow() + third.Borrow()), "1", "2", true);
  ExpectRatio(Take(sixth.Borrow() - third.Borrow()), "-1", "6", true);
  ExpectRatio(Take(negative.Borrow() * nine_quarters.Borrow()), "-3", "2",
              true);
  ExpectRatio(Take(two_thirds.Borrow() / negative_four_fifths.Borrow()), "-5",
              "6", true);

  const auto wide = Fraction("18446744073709551617", "3");
  const auto other = Fraction("-18446744073709551619", "5");
  const auto check = [](const Rational& a, const Rational& b,
                        const std::array<std::string_view, 4>& expected) {
    SCOPED_TRACE(a.ToString() + " and " + b.ToString());
    EXPECT_EQ(Take(a.Borrow() + b.Borrow()).ToString(), expected[0]) << "add";
    EXPECT_EQ(Take(a.Borrow() - b.Borrow()).ToString(), expected[1]) << "sub";
    EXPECT_EQ(Take(a.Borrow() * b.Borrow()).ToString(), expected[2]) << "mul";
    EXPECT_EQ(Take(a.Borrow() / b.Borrow()).ToString(), expected[3]) << "div";
  };
  check(negative, wide,
        {"6148914691236517205", "-18446744073709551619/3",
         "-36893488147419103234/9", "-2/18446744073709551617"});
  check(wide, negative,
        {"6148914691236517205", "18446744073709551619/3",
         "-36893488147419103234/9", "-18446744073709551617/2"});
  check(wide, other,
        {"36893488147419103228/15", "147573952589676412942/15",
         "-340282366920938463537161583726606417923/15",
         "-92233720368547758085/55340232221128654857"});
  check(other, wide,
        {"36893488147419103228/15", "-147573952589676412942/15",
         "-340282366920938463537161583726606417923/15",
         "-55340232221128654857/92233720368547758085"});

  const auto reciprocal =
      Take(Rational::Make(IntegerView(1), Unsigned(UINT64_MAX)));
  const auto square = Take(reciprocal.Borrow() * reciprocal.Borrow());
  ExpectRatio(square, "1", "340282366920938463426481119284349108225", false);
  ExpectRatio(Take(square.Borrow() / reciprocal.Borrow()), "1",
              "18446744073709551615", true);
  const auto wide_reciprocal = Fraction("3", "18446744073709551617");
  ExpectRatio(Take(wide.Borrow() * wide_reciprocal.Borrow()), "1", "1", true);
  ExpectRatio(Take(wide.Borrow() - wide.Borrow()), "0", "1", true);
  const auto min =
      Take(Rational::Make(IntegerView(kMin), Unsigned(UINT64_MAX)));
  ExpectRatio(Take(min.Borrow() + min.Borrow()), "-18446744073709551616",
              "18446744073709551615", false);
  const auto zero = Ratio(0);
  ExpectRatio(Take(wide.Borrow() * zero.Borrow()), "0", "1", true);
  ExpectRatio(Take(zero.Borrow() / wide.Borrow()), "0", "1", true);
  for (const auto* value : {&third, &wide, &zero}) {
    ExpectError(value->Borrow() / zero.Borrow(), NumericError::kDivisionByZero);
  }
  EXPECT_LT(sixth.Borrow().Compare(third.Borrow()), 0);
  EXPECT_GT(negative.Borrow().Compare(other.Borrow()), 0);
  EXPECT_LT(other.Borrow().Compare(negative.Borrow()), 0);
  EXPECT_LT(third.Borrow().Compare(wide.Borrow()), 0);
  EXPECT_GT(wide.Borrow().Compare(third.Borrow()), 0);
  EXPECT_LT(square.Borrow().Compare(reciprocal.Borrow()), 0);
  const auto equivalent = Fraction("36893488147419103234", "6");
  EXPECT_EQ(wide.Borrow().Compare(equivalent.Borrow()), 0);
  EXPECT_EQ(wide.Borrow().Hash(), equivalent.Borrow().Hash());
  const auto close = Fraction("18446744073709551618", "3");
  EXPECT_LT(wide.Borrow().Compare(close.Borrow()), 0);
  EXPECT_GT(close.Borrow().Compare(wide.Borrow()), 0);
}

TEST(UtilRational, IntegerAddAndSubtractAtSignedWordLimits) {
  const auto min = Ratio(kMin);
  const auto max = Ratio(kMax);
  const auto one = Ratio(1);
  const auto minus_one = Ratio(-1);
  const auto zero = Ratio(0);
  ExpectRatio(Take(min.Borrow() + max.Borrow()), "-1", "1", true);
  ExpectRatio(Take(max.Borrow() - max.Borrow()), "0", "1", true);
  ExpectRatio(Take(min.Borrow() + zero.Borrow()), "-9223372036854775808", "1",
              true);
  ExpectRatio(Take(max.Borrow() + one.Borrow()), "9223372036854775808", "1",
              false);
  ExpectRatio(Take(min.Borrow() + minus_one.Borrow()), "-9223372036854775809",
              "1", false);
  ExpectRatio(Take(min.Borrow() - one.Borrow()), "-9223372036854775809", "1",
              false);
  ExpectRatio(Take(zero.Borrow() - min.Borrow()), "9223372036854775808", "1",
              false);
}

TEST(UtilRational, PowerOfTwoDenominatorAddAndSubtractAlignAndReduce) {
  const auto a = Ratio(-3, 8);
  const auto b = Ratio(5, 32);
  ExpectRatio(Take(a.Borrow() + b.Borrow()), "-7", "32", true);
  ExpectRatio(Take(a.Borrow() - b.Borrow()), "-17", "32", true);
  ExpectRatio(Take(b.Borrow() - a.Borrow()), "17", "32", true);

  const auto half_max = Ratio(kMax, 2);
  const auto three_halves = Ratio(3, 2);
  ExpectRatio(Take(half_max.Borrow() + three_halves.Borrow()),
              "4611686018427387905", "1", true);
  const auto max_den = Unsigned(std::uint64_t{1} << 63);
  const auto tiny = Take(Rational::Make(IntegerView(1), max_den));
  const auto large = Take(Rational::Make(IntegerView(-kMax), max_den));
  ExpectRatio(Take(large.Borrow() - tiny.Borrow()), "-1", "1", true);
  ExpectRatio(Take(tiny.Borrow() - tiny.Borrow()), "0", "1", true);

  const auto min = Ratio(kMin);
  constexpr auto kDenominator = "9223372036854775808";
  ExpectRatio(Take(min.Borrow() + tiny.Borrow()),
              "-85070591730234615865843651857942052863", kDenominator, false);
  ExpectRatio(Take(tiny.Borrow() - min.Borrow()),
              "85070591730234615865843651857942052865", kDenominator, false);
}

TEST(UtilRational, EqualNonPowerOfTwoDenominatorAddAndSubtractReduce) {
  const auto a = Ratio(5, 18);
  const auto b = Ratio(-7, 18);
  ExpectRatio(Take(a.Borrow() + b.Borrow()), "-1", "9", true);
  ExpectRatio(Take(a.Borrow() - b.Borrow()), "2", "3", true);
  ExpectRatio(Take(b.Borrow() - a.Borrow()), "-2", "3", true);
  ExpectRatio(Take(b.Borrow() - b.Borrow()), "0", "1", true);
  const auto c = Ratio(kMax, 3);
  const auto d = Ratio(kMax - 3, 3);
  ExpectRatio(Take(c.Borrow() + d.Borrow()), "18446744073709551611", "3",
              false);
  ExpectRatio(Take(c.Borrow() - d.Borrow()), "1", "1", true);
}

TEST(UtilRational, Signed128BitAddAndSubtractOverflowPromoteExactly) {
  const auto left =
      Take(Rational::Make(IntegerView(kMin), Unsigned(UINT64_MAX)));
  const auto negative =
      Take(Rational::Make(IntegerView(kMin), Unsigned(UINT64_MAX - 2)));
  const auto positive =
      Take(Rational::Make(IntegerView(kMax - 1), Unsigned(UINT64_MAX - 2)));
  ASSERT_EQ(left.Borrow().BackingBytes(), 0u);
  ASSERT_EQ(negative.Borrow().BackingBytes(), 0u);
  ASSERT_EQ(positive.Borrow().BackingBytes(), 0u);

  constexpr auto kDenominator = "340282366920938463389587631136930004995";
  ExpectRatio(Take(left.Borrow() + negative.Borrow()),
              "-340282366920938463426481119284349108224", kDenominator, false);
  ExpectRatio(Take(left.Borrow() - positive.Borrow()),
              "-340282366920938463389587631136930004994", kDenominator, false);
}

TEST(UtilRational, NegationAndAbsoluteValuePreserveCanonicalForm) {
  for (const auto n : {-3, 0, 3}) {
    const auto value = Ratio(n, 7);
    const auto opposite = Ratio(-n, 7);
    const auto magnitude = Ratio(n < 0 ? -n : n, 7);
    EXPECT_EQ(Take(Rational::Negate(value.Borrow())).ToString(),
              opposite.ToString());
    EXPECT_EQ(Take(Rational::Abs(value.Borrow())).ToString(),
              magnitude.ToString());
  }
  const auto min = Ratio(kMin);
  const auto positive = Take(Rational::Negate(min.Borrow()));
  ExpectRatio(positive, "9223372036854775808", "1", false);
  ExpectRatio(Take(Rational::Abs(min.Borrow())), "9223372036854775808", "1",
              false);
  ExpectRatio(Take(Rational::Negate(positive.Borrow())), "-9223372036854775808",
              "1", true);
  const auto wide = Fraction("-18446744073709551617", "3");
  ExpectRatio(Take(Rational::Abs(wide.Borrow())), "18446744073709551617", "3",
              false);
}

void ExpectIntegralRounding(const Rational& value, std::string_view floor,
                            std::string_view ceil, std::string_view trunc) {
  SCOPED_TRACE(value.ToString());
  const auto view = value.Borrow();
  EXPECT_EQ(Take(Rational::Floor(view)).ToDecimal(), floor);
  EXPECT_EQ(Take(Rational::Ceil(view)).ToDecimal(), ceil);
  EXPECT_EQ(Take(Rational::Trunc(view)).ToDecimal(), trunc);
  const bool is_integer = floor == ceil;
  EXPECT_EQ(view.IsInteger(), is_integer);
  const auto exact = Rational::ToIntegerExact(view);
  if (is_integer) {
    ASSERT_TRUE(exact.has_value());
    EXPECT_EQ(exact->ToDecimal(), trunc);
  } else {
    ExpectError(exact, NumericError::kInvalidArgument);
  }
}

TEST(UtilRational, IntegralRoundingAndConversion) {
  ExpectIntegralRounding(Ratio(-7, 3), "-3", "-2", "-2");
  ExpectIntegralRounding(Ratio(7, 3), "2", "3", "2");
  ExpectIntegralRounding(Ratio(0), "0", "0", "0");
  ExpectIntegralRounding(Ratio(1, 7), "0", "1", "0");
  ExpectIntegralRounding(Ratio(-1, 7), "-1", "0", "0");
  ExpectIntegralRounding(Ratio(-8, 4), "-2", "-2", "-2");
  ExpectIntegralRounding(Ratio(kMin), "-9223372036854775808",
                         "-9223372036854775808", "-9223372036854775808");
  ExpectIntegralRounding(Fraction("18446744073709551616", "1"),
                         "18446744073709551616", "18446744073709551616",
                         "18446744073709551616");
  ExpectIntegralRounding(Fraction("-36893488147419103233", "2"),
                         "-18446744073709551617", "-18446744073709551616",
                         "-18446744073709551616");
  ExpectIntegralRounding(Fraction("36893488147419103233", "2"),
                         "18446744073709551616", "18446744073709551617",
                         "18446744073709551616");
}

TEST(UtilRational, BorrowedComponentsAndIndependentCopies) {
  for (const auto denominator :
       {"18446744073709551615", "18446744073709551617"}) {
    SCOPED_TRACE(denominator);
    auto detached = Ratio(0);
    std::uint64_t hash = 0;
    {
      const auto owner = Fraction("1", denominator);
      const auto view = owner.Borrow();
      const auto component = view.Denominator();
      detached = view.Copy();
      hash = view.Hash();
      EXPECT_EQ(component.ToDecimal(), denominator);
      EXPECT_EQ(detached.Borrow(), view);
    }
    auto copied = detached;
    CheckSelfAssignment(detached);
    auto moved = std::move(copied);
    ExpectRatio(copied, "0", "1", true);
    auto assigned = Fraction("18446744073709551616", "7");
    assigned = std::move(moved);
    ExpectRatio(moved, "0", "1", true);
    EXPECT_EQ(assigned.Borrow(), detached.Borrow());
    EXPECT_EQ(assigned.Borrow().Hash(), hash);
    EXPECT_EQ(assigned.Borrow().Denominator().ToDecimal(), denominator);
  }
}

}  // namespace
}  // namespace dandelion::testing::util

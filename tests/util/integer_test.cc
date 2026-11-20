#include <array>
#include <bit>
#include <cstdlib>
#include <limits>
#include <numeric>
#include <string>

#include "test_support.h"

namespace dandelion::testing::util {
namespace {

static_assert(sizeof(Integer) == 16);
static_assert(kNothrowOwnership<Integer>);
static_assert(!RvalueBorrowable<Integer>);
static_assert(!RvalueBorrowable<const Integer>);

constexpr auto kMin = std::numeric_limits<std::int64_t>::min();
constexpr auto kMax = std::numeric_limits<std::int64_t>::max();

void ExpectInteger(const NumericResult<Integer>& actual,
                   std::string_view expected, bool small) {
  ASSERT_TRUE(actual.has_value());
  EXPECT_EQ(actual->ToDecimal(), expected);
  EXPECT_EQ(actual->Borrow().IsSmall(), small);
  EXPECT_EQ(actual->Borrow().BackingBytes() == 0, small);
}

TEST(UtilInteger, DecimalAndRadixGrammar) {
  EXPECT_EQ(Int("-00000").ToDecimal(), "0");
  EXPECT_EQ(Int("00042").ToDecimal(), "42");
  EXPECT_EQ(Int("-00042").ToDecimal(), "-42");
  const auto binary =
      Take(Integer::ParseRadix("0001" + std::string(64, '0'), 2));
  const auto hex = Take(Integer::ParseRadix("00010000000000000000", 16));
  EXPECT_EQ(binary.ToDecimal(), "18446744073709551616");
  EXPECT_EQ(binary.Borrow(), hex.Borrow());
  EXPECT_EQ(binary.Borrow().Hash(), hex.Borrow().Hash());
  EXPECT_EQ(Take(Integer::ParseRadix("00aBcDeF", 16)).ToDecimal(), "11259375");
  EXPECT_EQ(Take(Integer::ParseRadix("0000", 2)).ToDecimal(), "0");
  EXPECT_EQ(Take(Integer::ParseRadix("0000", 16)).ToDecimal(), "0");

  constexpr std::array<std::string_view, 14> invalid_decimal = {
      "",    "-",   "+1",  "--1",  "-+1", " 1", "1 ",
      "1\n", "1_0", "1.0", "0x10", "#b1", "a",  std::string_view("1\0", 2)};
  for (auto value : invalid_decimal) {
    SCOPED_TRACE(::testing::PrintToString(value));
    ExpectError(Integer::ParseDecimal(value), NumericError::kInvalidText);
  }
  for (auto radix : {2, 16}) {
    constexpr std::array<std::string_view, 10> invalid_radix = {
        "",    "-1",  "+1",  " 1", "1 ",
        "#b1", "#x1", "0x1", "g",  std::string_view("1\0", 2)};
    for (auto value : invalid_radix) {
      SCOPED_TRACE(::testing::PrintToString(value));
      ExpectError(Integer::ParseRadix(value, radix),
                  NumericError::kInvalidText);
    }
  }
  ExpectError(Integer::ParseRadix("2", 2), NumericError::kInvalidText);
  ExpectError(Integer::ParseRadix("1", 10), NumericError::kInvalidArgument);
}

TEST(UtilInteger, SignedUnsignedNarrowingAndMagnitude) {
  const Integer zero;
  EXPECT_EQ(zero.Borrow(), IntegerView(0));
  EXPECT_EQ(zero.ToDecimal(), "0");
  for (const auto value : {kMin, kMin + 1, std::int64_t{-1}, std::int64_t{0},
                           std::int64_t{1}, kMax - 1, kMax}) {
    SCOPED_TRACE(value);
    const Integer number(value);
    const auto parsed = Int(std::to_string(value));
    const auto view = number.Borrow();
    const auto magnitude =
        value < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(value)
                  : static_cast<std::uint64_t>(value);
    EXPECT_EQ(view, parsed.Borrow());
    EXPECT_EQ(view, IntegerView(value));
    EXPECT_EQ(view.Hash(), parsed.Borrow().Hash());
    EXPECT_EQ(view.TryInt64(), value);
    EXPECT_EQ((view.Sign() > 0) - (view.Sign() < 0), (value > 0) - (value < 0));
    EXPECT_EQ(view.IsOdd(), magnitude % 2 != 0);
    EXPECT_EQ(view.BitLength(), std::bit_width(magnitude));
    EXPECT_EQ(view.MagnitudeChunk(0), magnitude);
    EXPECT_EQ(view.MagnitudeChunk(1), 0u);
    EXPECT_EQ(view.BackingBytes(), 0u);
    if (value < 0) {
      EXPECT_FALSE(view.TryUint32());
      EXPECT_FALSE(view.TryUint64());
    } else {
      EXPECT_EQ(view.TryUint64(), magnitude);
      EXPECT_EQ(view.Hash(), Unsigned(magnitude).Hash());
      EXPECT_EQ(view, Unsigned(magnitude));
    }
  }

  const auto unsigned_max = Take(Integer::FromUnsigned(UINT64_MAX));
  const auto unsigned_high =
      Take(Integer::FromUnsigned(std::uint64_t{1} << 63));
  const auto unsigned_word =
      Take(Integer::FromUnsigned(static_cast<std::uint64_t>(kMax)));
  EXPECT_TRUE(unsigned_word.Borrow().IsSmall());
  EXPECT_EQ(unsigned_word.Borrow().TryInt64(), kMax);
  for (const auto* number : {&unsigned_high, &unsigned_max}) {
    const auto view = number->Borrow();
    const auto magnitude = view.MagnitudeChunk(0);
    EXPECT_EQ(view, Unsigned(magnitude));
    EXPECT_EQ(view.Hash(), Unsigned(magnitude).Hash());
    EXPECT_EQ(view.BitLength(), 64u);
    EXPECT_FALSE(view.IsSmall());
    EXPECT_FALSE(view.TryInt64());
    EXPECT_FALSE(view.TryUint32());
    EXPECT_GT(view.BackingBytes(), 0u);
  }
  EXPECT_EQ(unsigned_high.Borrow().TryUint64(), std::uint64_t{1} << 63);
  EXPECT_EQ(unsigned_max.Borrow().TryUint64(), UINT64_MAX);

  const auto two64 = Int("18446744073709551616");
  EXPECT_EQ(two64.Borrow().BitLength(), 65u);
  EXPECT_FALSE(two64.Borrow().TryUint64());
  EXPECT_FALSE(two64.Borrow().IsOdd());
  EXPECT_EQ(two64.Borrow().MagnitudeChunk(0), 0u);
  EXPECT_EQ(two64.Borrow().MagnitudeChunk(1), 1u);

  const auto below_min = Int("-9223372036854775809");
  EXPECT_FALSE(below_min.Borrow().TryInt64());
  EXPECT_FALSE(below_min.Borrow().IsSmall());
  EXPECT_LT(below_min.Borrow().Sign(), 0);
  EXPECT_EQ(below_min.Borrow().BitLength(), 64u);
  EXPECT_TRUE(below_min.Borrow().IsOdd());
  EXPECT_EQ(below_min.Borrow().MagnitudeChunk(0), (std::uint64_t{1} << 63) + 1);

  const auto positive = Int("340282366920938463481821351505477763073");
  const auto larger = Int("340282366920938463481821351505477763074");
  const auto negative = Int("-340282366920938463481821351505477763074");
  for (const auto* number : {&positive, &larger, &negative}) {
    const auto view = number->Borrow();
    EXPECT_EQ(view.ToDecimal(), number->ToDecimal());
    EXPECT_EQ(view.BitLength(), 129u);
    EXPECT_EQ(view.MagnitudeChunk(0), number == &positive ? 1u : 2u);
    EXPECT_EQ(view.MagnitudeChunk(1), 1u);
    EXPECT_EQ(view.MagnitudeChunk(2), 1u);
    EXPECT_EQ(view.MagnitudeChunk(3), 0u);
    EXPECT_EQ(view.MagnitudeChunk(UINT64_MAX), 0u);
    EXPECT_FALSE(view.TryInt64());
    EXPECT_FALSE(view.TryUint64());
    EXPECT_FALSE(view.TryUint32());
  }
  EXPECT_LT(positive.Borrow().Compare(larger.Borrow()), 0);
  EXPECT_GT(larger.Borrow().Compare(positive.Borrow()), 0);
  EXPECT_LT(negative.Borrow().Compare(below_min.Borrow()), 0);
  EXPECT_GT(positive.Borrow().Compare(unsigned_max.Borrow()), 0);
  EXPECT_EQ(Unsigned(0).TryUint32(), 0u);
  EXPECT_EQ(Unsigned(UINT32_MAX).TryUint32(), UINT32_MAX);
  EXPECT_FALSE(Unsigned(std::uint64_t{1} + UINT32_MAX).TryUint32());
}

TEST(UtilInteger, BoundedArithmeticAndOrdering) {
  for (int a = -40; a <= 40; ++a) {
    SCOPED_TRACE(a);
    const IntegerView left(a);
    EXPECT_EQ(Int64(-left), -a);
    EXPECT_EQ(Int64(Integer::Negate(left)), -a);
    EXPECT_EQ(Int64(Integer::Abs(left)), std::abs(a));
    for (int b = -40; b <= 40; ++b) {
      SCOPED_TRACE(b);
      const IntegerView right(b);
      EXPECT_EQ(Int64(left + right), a + b);
      EXPECT_EQ(Int64(left - right), a - b);
      EXPECT_EQ(Int64(left * right), a * b);
      const int comparison = left.Compare(right);
      EXPECT_EQ((comparison > 0) - (comparison < 0), (a > b) - (a < b));
      EXPECT_EQ(left == right, a == b);
      EXPECT_EQ(Int64(Integer::Gcd(left, right)), std::gcd(a, b));
      EXPECT_EQ(Int64(Integer::Lcm(left, right)), std::lcm(a, b));
    }
  }
}

TEST(UtilInteger, PromotionCrossLimbArithmeticAndCancellation) {
  const IntegerView min(kMin), max(kMax), one(1), minus_one(-1);
  const auto two64 = Int("18446744073709551616");
  const auto two64_plus_one = Int("18446744073709551617");
  const auto negative = Int("-18446744073709551616");
  const auto positive_min = Take(-min);
  ExpectInteger(max + one, "9223372036854775808", false);
  ExpectInteger(min - one, "-9223372036854775809", false);
  ExpectInteger(min * minus_one, "9223372036854775808", false);
  ExpectInteger(Integer::Abs(min), "9223372036854775808", false);
  ExpectInteger(-positive_min.Borrow(), "-9223372036854775808", true);
  ExpectInteger(positive_min.Borrow() * minus_one, "-9223372036854775808",
                true);
  ExpectInteger(two64.Borrow() + negative.Borrow(), "0", true);
  ExpectInteger(two64_plus_one.Borrow() - two64.Borrow(), "1", true);
  ExpectInteger(two64.Borrow() + one, "18446744073709551617", false);
  ExpectInteger(negative.Borrow() - one, "-18446744073709551617", false);
  ExpectInteger(two64.Borrow() + two64.Borrow(), "36893488147419103232", false);
  ExpectInteger(two64.Borrow() * negative.Borrow(),
                "-340282366920938463463374607431768211456", false);
  ExpectInteger(Integer::Gcd(min, min), "9223372036854775808", false);
  ExpectInteger(Integer::Gcd(two64.Borrow(), negative.Borrow()),
                "18446744073709551616", false);
  ExpectInteger(Integer::Gcd(two64.Borrow(), two64_plus_one.Borrow()), "1",
                true);
  ExpectInteger(Integer::Lcm(two64.Borrow(), two64_plus_one.Borrow()),
                "340282366920938463481821351505477763072", false);
}

TEST(UtilInteger, EuclideanDivisionAndExactness) {
  for (int a = -40; a <= 40; ++a) {
    for (int b = -9; b <= 9; ++b) {
      SCOPED_TRACE(a);
      SCOPED_TRACE(b);
      const IntegerView left(a), right(b);
      if (b == 0) {
        ExpectError(Integer::DivMod(left, right),
                    NumericError::kDivisionByZero);
        ExpectError(Integer::ExactDiv(left, right),
                    NumericError::kDivisionByZero);
        ExpectError(Integer::IsDivisible(left, right),
                    NumericError::kDivisionByZero);
        continue;
      }
      int r = 0;
      while ((a - r) % b != 0) ++r;
      const int q = (a - r) / b;
      const auto [quotient, remainder] = Take(Integer::DivMod(left, right));
      EXPECT_EQ(quotient.Borrow().TryInt64(), q);
      EXPECT_EQ(remainder.Borrow().TryInt64(), r);
      ASSERT_TRUE(quotient.Borrow().TryInt64());
      ASSERT_TRUE(remainder.Borrow().TryInt64());
      EXPECT_EQ(
          b * *quotient.Borrow().TryInt64() + *remainder.Borrow().TryInt64(),
          a);
      EXPECT_GE(remainder.Borrow().Sign(), 0);
      EXPECT_LT(*remainder.Borrow().TryInt64(), std::abs(b));
      EXPECT_EQ(Take(Integer::IsDivisible(left, right)), r == 0);
      const auto exact = Integer::ExactDiv(left, right);
      if (r == 0) {
        ASSERT_TRUE(exact.has_value());
        EXPECT_EQ(exact->Borrow().TryInt64(), q);
      } else {
        ExpectError(exact, NumericError::kInvalidArgument);
      }
    }
  }

  const auto [quotient, remainder] =
      Take(Integer::DivMod(IntegerView(kMin), IntegerView(-1)));
  EXPECT_EQ(quotient.ToDecimal(), "9223372036854775808");
  EXPECT_EQ(remainder.ToDecimal(), "0");
  EXPECT_TRUE(remainder.Borrow().IsSmall());
  ExpectInteger(Integer::ExactDiv(IntegerView(kMin), IntegerView(-1)),
                "9223372036854775808", false);

  const auto divisor = Int("18446744073709551616");
  const auto exact = Int("-55340232221128654848");
  const auto inexact = Int("-55340232221128654847");
  const auto negative_divisor = Int("-18446744073709551616");
  ExpectInteger(Integer::ExactDiv(exact.Borrow(), divisor.Borrow()), "-3",
                true);
  EXPECT_TRUE(Take(Integer::IsDivisible(exact.Borrow(), divisor.Borrow())));
  EXPECT_TRUE(Take(Integer::IsDivisible(IntegerView(0), divisor.Borrow())));
  EXPECT_FALSE(Take(Integer::IsDivisible(inexact.Borrow(), divisor.Borrow())));
  ExpectError(Integer::ExactDiv(inexact.Borrow(), negative_divisor.Borrow()),
              NumericError::kInvalidArgument);
  const auto [wide_q, wide_r] =
      Take(Integer::DivMod(inexact.Borrow(), negative_divisor.Borrow()));
  EXPECT_EQ(wide_q.Borrow().TryInt64(), 3);
  EXPECT_EQ(wide_r.Borrow().TryInt64(), 1);
  const auto [short_q, large_r] =
      Take(Integer::DivMod(IntegerView(-1), divisor.Borrow()));
  EXPECT_EQ(short_q.Borrow().TryInt64(), -1);
  EXPECT_EQ(large_r.ToDecimal(), "18446744073709551615");
  const auto [long_q, short_r] =
      Take(Integer::DivMod(divisor.Borrow(), IntegerView(1)));
  EXPECT_EQ(long_q.Borrow(), divisor.Borrow());
  EXPECT_EQ(short_r.Borrow().TryInt64(), 0);
}

TEST(UtilInteger, PowerUsesExactExponentsAndSmtNegativeExponentSemantics) {
  for (const auto base : {-2, -1, 0, 1, 2}) {
    for (const auto exponent : {-4, -3, 0, 3, 4}) {
      SCOPED_TRACE(base);
      SCOPED_TRACE(exponent);
      int expected = 1;
      if (exponent < 0) {
        expected = base == 1    ? 1
                   : base == -1 ? (exponent % 2 == 0 ? 1 : -1)
                                : 0;
      } else {
        for (int i = 0; i < exponent; ++i) expected *= base;
      }
      EXPECT_EQ(Int64(Integer::Power(IntegerView(base), IntegerView(exponent))),
                expected);
    }
  }
  const auto power = Take(Integer::Power(IntegerView(2), IntegerView(1000)));
  EXPECT_EQ(power.Borrow().BitLength(), 1001u);
  for (std::uint64_t i = 0; i <= 16; ++i) {
    EXPECT_EQ(power.Borrow().MagnitudeChunk(i),
              i == 15 ? std::uint64_t{1} << 40 : 0);
  }
  const auto wide_base = Int("18446744073709551616");
  ExpectInteger(Integer::Power(wide_base.Borrow(), IntegerView(2)),
                "340282366920938463463374607431768211456", false);
  for (const auto text : {"18446744073709551616", "18446744073709551617",
                          "-18446744073709551616", "-18446744073709551617"}) {
    SCOPED_TRACE(text);
    const auto owner = Int(text);
    const auto exponent = owner.Borrow();
    const bool odd = std::string_view(text).back() == '7';
    EXPECT_EQ(Take(Integer::Power(IntegerView(0), exponent)).ToDecimal(), "0");
    EXPECT_EQ(Take(Integer::Power(IntegerView(1), exponent)).ToDecimal(), "1");
    EXPECT_EQ(Take(Integer::Power(IntegerView(-1), exponent)).ToDecimal(),
              odd ? "-1" : "1");
    const auto nontrivial = Integer::Power(IntegerView(-2), exponent);
    if (text[0] == '-') {
      ExpectInteger(nontrivial, "0", true);
    } else {
      ExpectError(nontrivial, NumericError::kSizeLimit);
    }
  }
}

TEST(UtilInteger, BorrowCopyMoveAndSelfAssignmentPreserveIdentity) {
  for (const auto text :
       {"-9223372036854775808", "340282366920938463463374607431768211456"}) {
    SCOPED_TRACE(text);
    Integer detached;
    std::uint64_t hash = 0;
    {
      const auto owner = Int(text);
      const auto view = owner.Borrow();
      hash = view.Hash();
      detached = view.Copy();
      EXPECT_EQ(detached.Borrow(), view);
    }
    EXPECT_EQ(detached.ToDecimal(), text);
    auto copied = detached;
    CheckSelfAssignment(detached);
    EXPECT_EQ(detached.ToDecimal(), text);
    auto moved = std::move(copied);
    EXPECT_EQ(copied.Borrow().TryInt64(), 0);
    auto assigned = Int("18446744073709551616");
    assigned = std::move(moved);
    EXPECT_EQ(moved.Borrow().TryInt64(), 0);
    EXPECT_EQ(assigned.Borrow(), detached.Borrow());
    EXPECT_EQ(assigned.Borrow().Hash(), hash);
  }
}

}  // namespace
}  // namespace dandelion::testing::util

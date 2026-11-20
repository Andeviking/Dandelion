#include <new>
#include <string>

#include "test_support.h"

namespace dandelion::testing::util {
namespace {

class FailAllocation {
 public:
  explicit FailAllocation(std::size_t checkpoint) {
    NumericTestPeer::FailAllocationAfter(checkpoint);
  }
  ~FailAllocation() { NumericTestPeer::ClearFailures(); }

  FailAllocation(const FailAllocation&) = delete;
  FailAllocation& operator=(const FailAllocation&) = delete;
};

class CapacityGuard {
 public:
  ~CapacityGuard() { NumericTestPeer::ClearFailures(); }
};

template <class Call, class CheckValue, class CheckInputs>
void SweepChecked(const char* name, Call call, CheckValue check_value,
                  CheckInputs check_inputs) {
  SCOPED_TRACE(name);
  for (std::size_t checkpoint = 0; checkpoint < 256; ++checkpoint) {
    SCOPED_TRACE(checkpoint);
    const auto result = [&] {
      const FailAllocation fault(checkpoint);
      return call();
    }();
    check_inputs();
    if (result) {
      EXPECT_GT(checkpoint, 0u) << "The result requires a C++ allocation";
      check_value(*result);
      return;
    }
    EXPECT_EQ(result.error(), NumericError::kCppAllocation);
  }
  FAIL() << "The operation did not succeed within the fault sweep";
}

template <class T, class Check>
void SweepCopy(const T& source, const T& old_destination, Check check) {
  const auto source_snapshot = source;
  for (const bool assignment : {false, true}) {
    SCOPED_TRACE(assignment ? "copy assignment" : "copy construction");
    auto destination = old_destination;
    const void* const source_backing = NumericTestPeer::BackingAddress(source);
    const void* const old_backing =
        NumericTestPeer::BackingAddress(destination);
    ASSERT_NE(source_backing, nullptr);
    ASSERT_NE(old_backing, nullptr);
    bool succeeded = false;
    for (std::size_t checkpoint = 0; checkpoint < 256; ++checkpoint) {
      SCOPED_TRACE(checkpoint);
      try {
        const FailAllocation fault(checkpoint);
        if (assignment) {
          destination = source;
        } else {
          T copy(source);
          destination = std::move(copy);
        }
        succeeded = true;
      } catch (const std::bad_alloc&) {
        check(destination, old_destination);
        EXPECT_EQ(NumericTestPeer::BackingAddress(destination), old_backing);
      }
      EXPECT_EQ(NumericTestPeer::BackingAddress(source), source_backing);
      check(source, source_snapshot);
      if (succeeded) {
        EXPECT_GT(checkpoint, 0u);
        check(destination, source);
        EXPECT_NE(NumericTestPeer::BackingAddress(destination), source_backing);
        break;
      }
    }
    ASSERT_TRUE(succeeded);
  }
}

FpFormat Format(std::uint32_t e, std::uint32_t s) {
  return Take(FpFormat::Make(Unsigned(e), Unsigned(s)));
}

TEST(UtilNumericFailure, IntegerAndRationalFactoriesAndOperatorsRecover) {
  const auto wide = Int("18446744073709551617");
  const auto other = Int("18446744073709551619");
  const auto a = wide.Borrow();
  const auto b = other.Borrow();
  const auto no_inputs = [] {};
  const auto check_integer_inputs = [&] {
    EXPECT_EQ(a.ToDecimal(), "18446744073709551617");
    EXPECT_EQ(b.ToDecimal(), "18446744073709551619");
  };
  const auto check_a = [&](const Integer& result) {
    EXPECT_EQ(result.Borrow(), a);
  };
  SweepChecked(
      "integer decimal import",
      [] { return Integer::ParseDecimal("18446744073709551617"); }, check_a,
      no_inputs);
  SweepChecked(
      "integer radix import",
      [] { return Integer::ParseRadix("10000000000000001", 16); }, check_a,
      no_inputs);
  SweepChecked(
      "integer unsigned promotion",
      [] { return Integer::FromUnsigned(UINT64_MAX); },
      [](const Integer& result) {
        EXPECT_EQ(result.Borrow().TryUint64(), UINT64_MAX);
      },
      no_inputs);
  SweepChecked(
      "wide integer multiplication", [&] { return a * b; },
      [](const Integer& result) {
        EXPECT_EQ(result.ToDecimal(),
                  "340282366920938463537161583726606417923");
      },
      check_integer_inputs);
  SweepChecked(
      "wide integer negation", [&] { return Integer::Negate(a); },
      [](const Integer& result) {
        EXPECT_EQ(result.ToDecimal(), "-18446744073709551617");
      },
      check_integer_inputs);
  SweepChecked(
      "wide Euclidean quotient and remainder",
      [&] { return Integer::DivMod(a, IntegerView(1)); },
      [&](const auto& result) {
        const auto& [q, r] = result;
        EXPECT_EQ(q.Borrow(), a);
        EXPECT_EQ(r.Borrow().TryInt64(), 0);
      },
      check_integer_inputs);

  const auto wide_rational = Take(Rational::Make(a, b));
  const auto fraction = wide_rational.Borrow();
  const auto original_n = fraction.Numerator();
  const auto original_d = fraction.Denominator();
  const auto check_rational_inputs = [&] {
    check_integer_inputs();
    EXPECT_EQ(original_n, a);
    EXPECT_EQ(original_d, b);
  };
  SweepChecked(
      "rational normalization", [&] { return Rational::Make(a, b); },
      [&](const Rational& result) { EXPECT_EQ(result.Borrow(), fraction); },
      check_rational_inputs);
  SweepChecked(
      "rational decimal fraction import",
      [] { return Rational::ParseDecimalFraction("0.000000000000000000001"); },
      [](const Rational& result) {
        EXPECT_EQ(result.ToString(), "1/1000000000000000000000");
      },
      no_inputs);
  SweepChecked(
      "integer to rational", [&] { return Rational::FromInteger(a); },
      [&](const Rational& result) {
        EXPECT_EQ(result.Borrow().Numerator(), a);
        EXPECT_EQ(result.Borrow().Denominator().TryUint64(), 1u);
      },
      check_rational_inputs);
  SweepChecked(
      "rational negation", [&] { return Rational::Negate(fraction); },
      [](const Rational& result) {
        EXPECT_EQ(result.ToString(),
                  "-18446744073709551617/18446744073709551619");
      },
      check_rational_inputs);
  const auto one = Ratio(1);
  const auto check_text = [](std::string_view expected) {
    return [expected](const Rational& result) {
      EXPECT_EQ(result.ToString(), expected);
    };
  };
  SweepChecked(
      "rational addition", [&] { return fraction + one.Borrow(); },
      check_text("36893488147419103236/18446744073709551619"),
      check_rational_inputs);
  SweepChecked(
      "rational subtraction", [&] { return fraction - one.Borrow(); },
      check_text("-2/18446744073709551619"), check_rational_inputs);
  const auto divisor = Ratio(3);
  SweepChecked(
      "rational multiplication", [&] { return fraction * divisor.Borrow(); },
      check_text("55340232221128654851/18446744073709551619"),
      check_rational_inputs);
  SweepChecked(
      "rational division", [&] { return fraction / divisor.Borrow(); },
      [](const Rational& result) {
        EXPECT_EQ(result.ToString(),
                  "18446744073709551617/55340232221128654857");
      },
      check_rational_inputs);
}

TEST(UtilNumericFailure, WideBitVectorAndFpCalculationsRecover) {
  const auto no_inputs = [] {};
  const std::string binary = "1" + std::string(63, '0') + "1";
  const auto wide = Take(BitVector::FromBinaryDigits(binary));
  const auto width = Unsigned(65);
  const auto wide_integer = Int("18446744073709551617");
  const auto view = wide_integer.Borrow();
  const auto check_wide = [&](const BitVector& result) {
    EXPECT_EQ(result, wide);
    EXPECT_EQ(Take(result.ToBinary()), binary);
  };
  const auto check_inputs = [&] {
    EXPECT_EQ(Take(wide.ToBinary()), binary);
    EXPECT_EQ(view.ToDecimal(), "18446744073709551617");
  };
  SweepChecked(
      "binary import", [&] { return BitVector::FromBinaryDigits(binary); },
      check_wide, check_inputs);
  SweepChecked(
      "hex import",
      [] { return BitVector::FromHexDigits("10000000000000001"); },
      [&](const BitVector& result) {
        EXPECT_EQ(result.Width(), 68u);
        EXPECT_EQ(result.Chunk(0), 1u);
        EXPECT_EQ(result.Chunk(1), 1u);
      },
      check_inputs);
  SweepChecked(
      "decimal BV import",
      [&] {
        return BitVector::FromDecimalModulo(width, "18446744073709551617");
      },
      check_wide, check_inputs);
  SweepChecked(
      "exact integer to BV",
      [&] { return BitVector::FromIntegerExact(width, view); }, check_wide,
      check_inputs);
  SweepChecked(
      "modulo integer to BV",
      [&] { return BitVector::FromIntegerModulo(width, view); }, check_wide,
      check_inputs);
  SweepChecked(
      "BV to unsigned integer",
      [&] { return BitVector::ToUnsignedInteger(wide); },
      [&](const Integer& result) { EXPECT_EQ(result.Borrow(), view); },
      check_inputs);
  SweepChecked(
      "wide BV formatting", [&] { return wide.ToBinary(); },
      [&](const std::string& result) { EXPECT_EQ(result, binary); },
      check_inputs);
  SweepChecked(
      "wide all-ones mask", [] { return BitVector::AllOnes(IntegerView(65)); },
      [](const BitVector& result) {
        EXPECT_EQ(result.Width(), 65u);
        EXPECT_EQ(result.Chunk(0), UINT64_MAX);
        EXPECT_EQ(result.Chunk(1), 1u);
      },
      no_inputs);
  const auto shift_source = Bits(65, std::uint64_t{1} << 63);
  const auto shift = Bits(65, 1);
  SweepChecked(
      "shift from inline to wide",
      [&] { return BitVector::Shl(shift_source, shift); },
      [](const BitVector& result) {
        EXPECT_EQ(result.Width(), 65u);
        EXPECT_EQ(result.Chunk(0), 0u);
        EXPECT_EQ(result.Chunk(1), 1u);
      },
      [&] { ExpectBits(shift_source, 65, std::uint64_t{1} << 63); });
  const auto two = Bits(65, 2);
  SweepChecked(
      "BV multiplication preserves high bit",
      [&] { return BitVector::Mul(wide, Bits(65, 3)); },
      [](const BitVector& result) {
        EXPECT_EQ(result.Width(), 65u);
        EXPECT_EQ(result.Chunk(0), 3u);
        EXPECT_EQ(result.Chunk(1), 1u);
      },
      check_inputs);
  SweepChecked(
      "wide BV quotient", [&] { return BitVector::Udiv(wide, Bits(65, 1)); },
      check_wide, check_inputs);
  SweepChecked(
      "wide BV concatenation", [&] { return BitVector::Concat(wide, two); },
      [](const BitVector& result) {
        EXPECT_EQ(result.Width(), 130u);
        EXPECT_EQ(result.Chunk(0), 2u);
        EXPECT_EQ(result.Chunk(1), 2u);
        EXPECT_EQ(result.Chunk(2), 2u);
      },
      check_inputs);

  const auto format = Format(8, 80);
  constexpr auto kRne = RoundingMode::kRne;
  const auto rational = Take(Rational::FromInteger(view));
  const auto fp = Take(RoundRationalToFp(format, kRne, rational.Borrow()));
  const auto fp_bits = Take(fp.ToBits());
  const auto check_fp = [&](const FloatingPointValue& result) {
    EXPECT_EQ(result, fp);
    const auto exact = Take(result.ToRational());
    EXPECT_EQ(exact.Borrow(), rational.Borrow());
  };
  const auto check_fp_inputs = [&] {
    check_inputs();
    EXPECT_EQ(Take(fp.ToBits()), fp_bits);
    EXPECT_EQ(rational.Borrow().Numerator(), view);
  };
  SweepChecked(
      "FP from packed bits",
      [&] { return FloatingPointValue::FromBits(format, fp_bits); }, check_fp,
      check_fp_inputs);
  SweepChecked(
      "rational to FP",
      [&] { return RoundRationalToFp(format, kRne, rational.Borrow()); },
      check_fp, check_fp_inputs);
  SweepChecked(
      "BV to FP",
      [&] { return FloatingPointValue::FromUnsignedBv(format, kRne, wide); },
      check_fp, check_fp_inputs);
  SweepChecked(
      "FP widening",
      [&] { return FloatingPointValue::Convert(Format(9, 90), kRne, fp); },
      [&](const FloatingPointValue& result) {
        EXPECT_EQ(Take(result.ToRational()).ToString(), "18446744073709551617");
      },
      check_fp_inputs);
  SweepChecked(
      "FP to exact rational", [&] { return fp.ToRational(); },
      [&](const Rational& result) {
        EXPECT_EQ(result.Borrow(), rational.Borrow());
      },
      check_fp_inputs);
  SweepChecked(
      "FP to defined wide BV",
      [&] { return FloatingPointValue::ToUnsignedBv(width, kRne, fp); },
      [&](const FpOutcome<BitVector>& result) {
        ASSERT_EQ(result.Kind(), FpOutcomeKind::kDefined);
        check_wide(result.Value());
      },
      check_fp_inputs);
  const auto zero =
      Take(FloatingPointValue::Special(format, FpCategory::kPositiveZero));
  SweepChecked(
      "FP add returns an independent wide value",
      [&] { return FloatingPointValue::Add(kRne, fp, zero); }, check_fp,
      check_fp_inputs);
  const auto half = Ratio(1, 2);
  const auto half_fp = Take(RoundRationalToFp(format, kRne, half.Borrow()));
  SweepChecked(
      "exact FMA to wide FP",
      [&] { return FloatingPointValue::Fma(kRne, fp, half_fp, fp); },
      [](const FloatingPointValue& result) {
        EXPECT_EQ(Take(result.ToRational()).ToString(),
                  "55340232221128654851/2");
      },
      check_fp_inputs);
  SweepChecked(
      "FP to packed bits", [&] { return fp.ToBits(); },
      [&](const BitVector& result) { EXPECT_EQ(result, fp_bits); },
      check_fp_inputs);
  SweepChecked(
      "FP max returns an independent defined wide outcome",
      [&] { return FloatingPointValue::Max(fp, zero); },
      [&](const FpOutcome<FloatingPointValue>& result) {
        ASSERT_EQ(result.Kind(), FpOutcomeKind::kDefined);
        check_fp(result.Value());
      },
      check_fp_inputs);
}

TEST(UtilNumericFailure, WideUnsignedDivisionOfSameValueDoesNotAllocate) {
  const auto value =
      Take(BitVector::FromBinaryDigits("1" + std::string(63, '0') + "1"));
  const FailAllocation fault(0);
  ExpectBits(Take(BitVector::Udiv(value, value)), 65, 1);
  ExpectBits(Take(BitVector::Urem(value, value)), 65, 0);
  EXPECT_EQ(NumericTestPeer::AllocationAttempts(), 0u);
}

TEST(UtilNumericFailure, WideFpAdditionPreservesOperandsAndBorrowedBits) {
  constexpr auto kRne = RoundingMode::kRne;
  const auto format = Format(65, 66);
  const auto left_integer = Int("55340232221128654848");  // 2^65 + 2^64
  const auto left_rational = Take(Rational::FromInteger(left_integer.Borrow()));
  const auto one = Ratio(1);
  const auto left =
      Take(RoundRationalToFp(format, kRne, left_rational.Borrow()));
  const auto right = Take(RoundRationalToFp(format, kRne, one.Borrow()));
  const auto left_snapshot = left;
  const auto right_snapshot = right;
  const auto left_view = left.Borrow();
  const auto right_view = right.Borrow();
  const auto* left_backing = NumericTestPeer::BackingAddress(left);
  const auto* right_backing = NumericTestPeer::BackingAddress(right);
  ASSERT_NE(left_backing, nullptr);
  ASSERT_NE(right_backing, nullptr);

  SweepChecked(
      "F(65,66) addition",
      [&] { return FloatingPointValue::Add(kRne, left, right); },
      [&](const FloatingPointValue& result) {
        EXPECT_EQ(result.Format(), format);
        EXPECT_EQ(Take(result.ToRational()).ToString(), "55340232221128654849");
      },
      [&] {
        EXPECT_EQ(left, left_snapshot);
        EXPECT_EQ(right, right_snapshot);
        ASSERT_EQ(NumericTestPeer::BackingAddress(left), left_backing);
        ASSERT_EQ(NumericTestPeer::BackingAddress(right), right_backing);
        EXPECT_EQ(left_view, left_snapshot.Borrow());
        EXPECT_EQ(right_view, right_snapshot.Borrow());
      });
}

TEST(UtilNumericFailure, WideCopyAndAssignmentKeepPriorStateOnEveryFailure) {
  const auto a = Int("18446744073709551617");
  const auto b = Int("18446744073709551619");
  const auto check_integer = [](const Integer& x, const Integer& y) {
    EXPECT_EQ(x.Borrow(), y.Borrow());
  };
  SweepCopy(a, b, check_integer);
  const auto r = Take(Rational::Make(a.Borrow(), b.Borrow()));
  const auto s = Take(Rational::Make(b.Borrow(), a.Borrow()));
  const auto check_rational = [](const Rational& x, const Rational& y) {
    EXPECT_EQ(x.Borrow(), y.Borrow());
  };
  SweepCopy(r, s, check_rational);
  const auto x = Take(BitVector::FromIntegerExact(IntegerView(65), a.Borrow()));
  const auto y = Take(BitVector::FromIntegerExact(IntegerView(65), b.Borrow()));
  SweepCopy(x, y, [](const BitVector& left, const BitVector& right) {
    EXPECT_EQ(left, right);
  });
  const auto format = Format(8, 80);
  const auto f =
      Take(RoundRationalToFp(format, RoundingMode::kRne, r.Borrow()));
  const auto g =
      Take(RoundRationalToFp(format, RoundingMode::kRne, s.Borrow()));
  SweepCopy(f, g,
            [](const FloatingPointValue& left,
               const FloatingPointValue& right) { EXPECT_EQ(left, right); });

  auto target_integer = b;
  auto target_rational = s;
  const auto int_borrow = target_integer.Borrow();
  const auto rat_borrow = target_rational.Borrow();
  const auto component = rat_borrow.Numerator();
  {
    const FailAllocation fault(0);
    EXPECT_THROW((void)a.Borrow().Copy(), std::bad_alloc);
    NumericTestPeer::FailAllocationAfter(0);
    EXPECT_THROW((void)a.ToDecimal(), std::bad_alloc);
    NumericTestPeer::FailAllocationAfter(0);
    EXPECT_THROW((void)r.Borrow().Copy(), std::bad_alloc);
    NumericTestPeer::FailAllocationAfter(0);
    EXPECT_THROW(target_integer = a, std::bad_alloc);
    NumericTestPeer::FailAllocationAfter(0);
    EXPECT_THROW(target_rational = r, std::bad_alloc);
  }
  EXPECT_EQ(int_borrow, b.Borrow());
  EXPECT_EQ(rat_borrow, s.Borrow());
  EXPECT_EQ(component, b.Borrow());
  target_integer = a;
  target_rational = r;
  check_integer(target_integer, a);
  check_rational(target_rational, r);
}

TEST(UtilNumericFailure,
     InlineStorageBorrowingAndSpecialOutcomesAllocateNothing) {
  const auto large = Int("18446744073709551617");
  const auto wide_rational =
      Take(Rational::Make(large.Borrow(), Unsigned(UINT64_MAX)));
  const auto word_rational =
      Take(Rational::Make(IntegerView(1), Unsigned(UINT64_MAX)));
  const auto max_format = Format(UINT32_MAX - 2, 2);
  const auto fp_format = Format(2, 3);
  const auto wide_vector =
      Take(BitVector::FromIntegerExact(IntegerView(65), large.Borrow()));
  auto fp = Take(RoundRationalToFp(Format(8, 80), RoundingMode::kRne,
                                   wide_rational.Borrow()));
  EXPECT_NE(NumericTestPeer::BackingAddress(fp), nullptr);
  const FailAllocation fault(0);
  const Integer min(INT64_MIN);
  auto copy = min;
  EXPECT_EQ(copy.Borrow().TryInt64(), INT64_MIN);
  EXPECT_EQ(copy.Borrow().Hash(), min.Borrow().Hash());
  EXPECT_EQ(Int64(IntegerView(4) + IntegerView(5)), 9);
  const auto word_copy = word_rational;
  EXPECT_EQ(word_copy.Borrow(), word_rational.Borrow());
  EXPECT_EQ(word_copy.Borrow().Denominator().TryUint64(), UINT64_MAX);
  EXPECT_EQ(wide_rational.Borrow().Numerator(), large.Borrow());
  EXPECT_EQ(wide_rational.Borrow().Denominator().TryUint64(), UINT64_MAX);
  EXPECT_EQ(word_copy.Borrow().Hash(), word_rational.Borrow().Hash());
  const auto half = Ratio(1, 2);
  const auto unit = Take(half.Borrow() + half.Borrow());
  EXPECT_EQ(unit.Borrow().Numerator().TryInt64(), 1);
  const auto high = Bits(64, UINT64_MAX);
  const auto sparse = Bits(UINT32_MAX, 1);
  EXPECT_EQ(Take(BitVector::Add(sparse, sparse)).TryUint64(), 2u);
  auto high_copy = high;
  EXPECT_EQ(high_copy, high);
  EXPECT_EQ(high_copy.Hash(), high.Hash());
  EXPECT_EQ(Take(BitVector::Neg(high_copy)).TryUint64(), 1u);
  const auto tiny =
      Take(FloatingPointValue::FromBits(max_format, IntegerView(1)));
  const auto tiny_copy = tiny;
  EXPECT_EQ(tiny_copy, tiny);
  EXPECT_EQ(tiny_copy.Hash(), tiny.Hash());
  EXPECT_TRUE(tiny.IsSubnormal());
  const auto positive =
      Take(FloatingPointValue::Special(fp_format, FpCategory::kPositiveZero));
  const auto negative =
      Take(FloatingPointValue::Special(fp_format, FpCategory::kNegativeZero));
  const auto nan =
      Take(FloatingPointValue::Special(max_format, FpCategory::kNaN));
  const auto minimum = Take(FloatingPointValue::Min(positive, negative));
  const auto maximum = Take(FloatingPointValue::Max(negative, positive));
  EXPECT_EQ(minimum.Kind(), FpOutcomeKind::kTwoChoices);
  EXPECT_EQ(maximum.Kind(), FpOutcomeKind::kTwoChoices);
  const auto unspecified = Take(FloatingPointValue::ToUnsignedBv(
      Unsigned(UINT32_MAX), RoundingMode::kRne, nan));
  ASSERT_EQ(unspecified.Kind(), FpOutcomeKind::kUnspecified);
  EXPECT_EQ(unspecified.Destination(), UINT32_MAX);

  const void* const fp_backing = NumericTestPeer::BackingAddress(fp);
  auto& alias = fp;
  fp = std::move(alias);
  auto moved_fp = std::move(fp);
  EXPECT_EQ(NumericTestPeer::BackingAddress(moved_fp), fp_backing);
  EXPECT_EQ(fp.Format(), Format(2, 2));
  EXPECT_TRUE(fp.IsZero());
  const void* const wide_backing = NumericTestPeer::BackingAddress(wide_vector);
  EXPECT_NE(wide_backing, nullptr);
  EXPECT_EQ(NumericTestPeer::AllocationAttempts(), 0u);
}

TEST(UtilNumericCapacity, GmpLimbBoundaryAndWordOrReducedResults) {
  const CapacityGuard reset;
  const auto limb_bits = NumericTestPeer::GmpLimbBits();
  const std::string at = "1" + std::string(3 * limb_bits - 1, '0');
  const std::string past = "1" + std::string(3 * limb_bits, '0');
  const auto expected = Take(Integer::ParseRadix(at, 2));
  const auto expected_view = expected.Borrow();
  const auto half = Ratio(1, 2);
  const auto three = IntegerView(3);
  NumericTestPeer::SetLimbCapacity(3);
  const auto imported = Take(Integer::ParseRadix(at, 2));
  EXPECT_EQ(imported.Borrow(), expected_view);
  ExpectError(Integer::ParseRadix(past, 2), NumericError::kSizeLimit);
  EXPECT_EQ(expected_view.BitLength(), 3 * limb_bits);
  const auto within =
      Take(Integer::ParseRadix("1" + std::string(2 * limb_bits, '0'), 2));
  const auto doubled = Take(within.Borrow() * IntegerView(2));
  EXPECT_EQ(doubled.Borrow().BitLength(), 2 * limb_bits + 2);
  NumericTestPeer::SetLimbCapacity(0);
  EXPECT_EQ(Take(Integer::ParseRadix("00000101", 2)).ToDecimal(), "5");
  EXPECT_EQ(Int64(three * three), 9);
  EXPECT_EQ(Take(half.Borrow() / half.Borrow()).ToString(), "1");
  ExpectError(Integer::Power(IntegerView(2), Unsigned(3 * limb_bits)),
              NumericError::kSizeLimit);
  NumericTestPeer::ClearFailures();
  const auto retried = Take(Integer::ParseRadix(past, 2));
  EXPECT_EQ(retried.Borrow().BitLength(), 3 * limb_bits + 1);
}

TEST(UtilNumericCapacity, MandatoryGmpWorkingLimbsCountAgainstCapacity) {
  const CapacityGuard reset;
  NumericTestPeer::SetLimbCapacity(3);
  EXPECT_TRUE(NumericTestPeer::CheckGmpWorkingLimbs(3, 0).has_value());
  EXPECT_TRUE(NumericTestPeer::CheckGmpWorkingLimbs(2, 1).has_value());
  ExpectError(NumericTestPeer::CheckGmpWorkingLimbs(3, 1),
              NumericError::kSizeLimit);
  ExpectError(NumericTestPeer::CheckGmpWorkingLimbs(2, 2),
              NumericError::kSizeLimit);
  ExpectError(NumericTestPeer::CheckGmpWorkingLimbs(UINT64_MAX, 1),
              NumericError::kSizeLimit);
}

TEST(UtilNumericCapacity, LimbAndByteCountsFailBeforeCppAllocation) {
  const auto wide = Int("18446744073709551617");
  const auto input = wide.Borrow();
  const auto make = [&] {
    return BitVector::FromIntegerExact(IntegerView(65), input);
  };
  const auto max_format = Format(2, 65);
  const auto packed = Take(BitVector::FromIntegerExact(IntegerView(67), input));
  const CapacityGuard reset;
  NumericTestPeer::SetBitVectorLimbCapacity(1);
  NumericTestPeer::FailAllocationAfter(0);
  ExpectError(make(), NumericError::kSizeLimit);
  EXPECT_EQ(NumericTestPeer::AllocationAttempts(), 0u);
  EXPECT_EQ(input.MagnitudeChunk(0), 1u);
  EXPECT_EQ(input.MagnitudeChunk(1), 1u);
  NumericTestPeer::ClearFailures();
  NumericTestPeer::SetBitVectorLimbCapacity(2);
  EXPECT_EQ(Take(make()).EffectiveBitLength(), 65u);
  NumericTestPeer::ClearFailures();

  NumericTestPeer::SetByteCapacity(2 * sizeof(std::uint64_t) - 1);
  ExpectError(make(), NumericError::kSizeLimit);
  ExpectError(FloatingPointValue::FromBits(max_format, packed),
              NumericError::kSizeLimit);
  EXPECT_EQ(packed.Chunk(0), 1u);
  EXPECT_EQ(packed.Chunk(1), 1u);
  ExpectBits(Bits(UINT32_MAX, 1), UINT32_MAX, 1);
  NumericTestPeer::SetByteCapacity(2 * sizeof(std::uint64_t));
  EXPECT_EQ(Take(make()).EffectiveBitLength(), 65u);
  EXPECT_TRUE(
      Take(FloatingPointValue::FromBits(max_format, packed)).IsNormal());
}

}  // namespace
}  // namespace dandelion::testing::util

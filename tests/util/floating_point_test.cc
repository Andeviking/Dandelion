#include <array>
#include <iterator>
#include <string>

#include "fp_oracle.h"
#include "test_support.h"

namespace dandelion::testing::util {
namespace {

using fp_reference::Fraction;
using fp_reference::Mode;
using fp_reference::TinyFormat;
using Fp = FloatingPointValue;

static_assert(sizeof(Fp) == 24);
static_assert(kNothrowOwnership<Fp>);
static_assert(
    std::is_same_v<std::underlying_type_t<RoundingMode>, std::uint8_t>);
static_assert(std::is_same_v<std::underlying_type_t<FpCategory>, std::uint8_t>);
static_assert(!std::is_convertible_v<FpOutcome<Fp>, Fp>);
static_assert(!std::is_convertible_v<FpOutcome<BitVector>, BitVector>);

constexpr std::pair<Mode, RoundingMode> kModes[] = {
    {Mode::kEven, RoundingMode::kRne},
    {Mode::kAway, RoundingMode::kRna},
    {Mode::kUp, RoundingMode::kRtp},
    {Mode::kDown, RoundingMode::kRtn},
    {Mode::kZero, RoundingMode::kRtz}};
constexpr std::array kCategories = {FpCategory::kNaN,
                                    FpCategory::kPositiveInfinity,
                                    FpCategory::kNegativeInfinity,
                                    FpCategory::kPositiveZero,
                                    FpCategory::kNegativeZero,
                                    FpCategory::kFiniteBits};

FpFormat Format(std::uint32_t exponent, std::uint32_t significand) {
  return Take(FpFormat::Make(Unsigned(exponent), Unsigned(significand)));
}

Fp Raw(FpFormat format, std::uint64_t packed) {
  return Take(Fp::FromBits(format, Unsigned(packed)));
}

Fp Raw(FpFormat format, std::string_view packed) {
  const auto owner = Int(packed);
  return Take(Fp::FromBits(format, owner.Borrow()));
}

Fp Rounded(FpFormat format, Fraction value, RoundingMode mode) {
  const auto rational = Ratio(value.Numerator(), value.Denominator());
  return Take(RoundRationalToFp(format, mode, rational.Borrow()));
}

FpCategory Category(const TinyFormat& reference, int raw) {
  if (reference.IsNan(raw)) return FpCategory::kNaN;
  if (reference.IsInfinity(raw)) {
    return reference.Negative(raw) ? FpCategory::kNegativeInfinity
                                   : FpCategory::kPositiveInfinity;
  }
  if (reference.IsZero(raw)) {
    return reference.Negative(raw) ? FpCategory::kNegativeZero
                                   : FpCategory::kPositiveZero;
  }
  return FpCategory::kFiniteBits;
}

class TinyValues {
 public:
  explicit TinyValues(std::uint32_t significand)
      : reference_(static_cast<int>(significand)),
        format_(Format(2, significand)),
        encodings_(reference_.Values()) {
    for (int raw = 0; raw < reference_.PatternCount(); ++raw) {
      values_.push_back(Raw(format_, static_cast<unsigned>(raw)));
    }
  }

  const TinyFormat& Reference() const { return reference_; }
  FpFormat FormatValue() const { return format_; }
  const std::vector<int>& Encodings() const { return encodings_; }
  const Fp& At(int raw) const { return values_[static_cast<std::size_t>(raw)]; }

 private:
  TinyFormat reference_;
  FpFormat format_;
  std::vector<int> encodings_;
  std::vector<Fp> values_;
};

// Fail at the first mismatch in each bounded exhaustive case. Print encodings
// and mode on failure without building a trace for every successful FMA triple.
::testing::AssertionResult Matches(const NumericResult<Fp>& actual,
                                   const TinyValues& tiny, int expected,
                                   const char* op, RoundingMode mode, int a,
                                   int b = -1, int c = -1) {
  if (actual && *actual == tiny.At(expected))
    return ::testing::AssertionSuccess();
  auto failure = ::testing::AssertionFailure();
  failure << op << " F(2," << tiny.Reference().Significand() << ") mode "
          << static_cast<int>(mode) << " inputs " << a << "," << b << "," << c
          << " expected bits " << expected;
  if (!actual) return failure << " error " << static_cast<int>(actual.error());
  const auto bits = actual->ToBits();
  if (bits && bits->TryUint64())
    return failure << " actual bits " << *bits->TryUint64();
  return failure << " output has no tiny encoding";
}

TEST(UtilFloatingPoint, ExactReferenceMatchesHandDerivedExamples) {
  const TinyFormat tiny(3);
  EXPECT_EQ(tiny.Value(1), Fraction(1, 4));
  EXPECT_EQ(tiny.Value(4), Fraction(1));
  EXPECT_EQ(tiny.Value(11), Fraction(7, 2));
  EXPECT_EQ(tiny.Fma(6, 6, 24, Mode::kEven), 1);
  EXPECT_EQ(tiny.Add(tiny.Mul(6, 6, Mode::kEven), 24, Mode::kEven), 0);
  EXPECT_EQ(tiny.Fma(8, 8, 24, Mode::kEven), 8);
  EXPECT_EQ(tiny.Fma(1, 2, 1, Mode::kEven), 2);
  EXPECT_EQ(tiny.Sqrt(9, Mode::kEven), 6);
  EXPECT_EQ(tiny.Sqrt(4, Mode::kEven), 4);
  EXPECT_EQ(tiny.Sqrt(18, Mode::kEven), tiny.Nan());
  EXPECT_EQ(tiny.Rem(11, 4), 18);
  EXPECT_EQ(tiny.Rem(9, 4), 2);
  EXPECT_EQ(tiny.Rem(24, 4), 16);
  EXPECT_EQ(tiny.Round(Fraction(9, 8), Mode::kEven), 4);
  EXPECT_EQ(tiny.Round(Fraction(9, 8), Mode::kAway), 5);
  EXPECT_EQ(tiny.Round(Fraction(-1, 8), Mode::kDown), 17);
  EXPECT_EQ(tiny.Round(Fraction(15, 4), Mode::kEven), 12);

  const TinyFormat larger(4);
  EXPECT_EQ(larger.RoundToIntegral(22, Mode::kEven), 24);
  EXPECT_EQ(larger.RoundToIntegral(22, Mode::kZero), 20);
  EXPECT_EQ(larger.RoundToIntegral(54, Mode::kUp), 52);
  EXPECT_EQ(larger.RoundToIntegral(54, Mode::kDown), 56);
  EXPECT_EQ(larger.RoundToIntegral(8, Mode::kEven), 8);
}

TEST(UtilFloatingPoint, FormatAndConstructionValidation) {
  for (std::size_t i = 0; i < std::size(kModes); ++i) {
    EXPECT_EQ(static_cast<std::size_t>(kModes[i].second), i);
  }
  for (std::size_t i = 0; i < kCategories.size(); ++i) {
    EXPECT_EQ(static_cast<std::size_t>(kCategories[i]), i);
  }
  const auto format = Format(2, 3);
  EXPECT_EQ(format.ExponentWidth(), 2u);
  EXPECT_EQ(format.SignificandWidth(), 3u);
  EXPECT_EQ(format, Format(2, 3));
  EXPECT_NE(format, Format(3, 2));
  for (const auto invalid : {-1, 0, 1}) {
    ExpectError(FpFormat::Make(IntegerView(invalid), IntegerView(2)),
                NumericError::kInvalidArgument);
    ExpectError(FpFormat::Make(IntegerView(2), IntegerView(invalid)),
                NumericError::kInvalidArgument);
  }
  const auto huge = Int("18446744073709551616");
  ExpectError(FpFormat::Make(IntegerView(2), huge.Borrow()),
              NumericError::kSizeLimit);
  ExpectError(
      FpFormat::Make(Unsigned(std::uint64_t{1} + UINT32_MAX), IntegerView(2)),
      NumericError::kSizeLimit);
  ExpectError(FpFormat::Make(Unsigned(UINT32_MAX - 1), IntegerView(2)),
              NumericError::kSizeLimit);
  ExpectError(Fp::FromBits(format, Bits(4, 0)), NumericError::kInvalidArgument);
  ExpectError(Fp::FromBits(format, Bits(6, 0)), NumericError::kInvalidArgument);
  ExpectError(Fp::FromBits(format, IntegerView(-1)),
              NumericError::kValueOutOfRange);
  ExpectError(Fp::FromBits(format, IntegerView(32)),
              NumericError::kValueOutOfRange);
  ExpectError(Fp::FromBits(format, huge.Borrow()),
              NumericError::kValueOutOfRange);
  ExpectError(Fp::FromFields(Bits(2, 0), Bits(2, 0), Bits(2, 0)),
              NumericError::kInvalidArgument);
  ExpectError(Fp::FromFields(Bits(1, 0), Bits(1, 0), Bits(2, 0)),
              NumericError::kInvalidArgument);
  ExpectError(Fp::FromFields(Bits(1, 0), Bits(2, 0), Bits(UINT32_MAX, 0)),
              NumericError::kSizeLimit);
  ExpectError(Fp::Special(format, FpCategory::kFiniteBits),
              NumericError::kInvalidArgument);
  ExpectError(Fp::Special(format, static_cast<FpCategory>(255)),
              NumericError::kInvalidArgument);
  EXPECT_NE(Raw(format, 0), Raw(Format(3, 2), 0));
  EXPECT_NE(Raw(format, 13), Raw(Format(3, 2), 15));
  EXPECT_NE(Raw(format, 4), Raw(Format(3, 2), 6));  // Both are numerically one.
  const auto zero_hash = Raw(Format(2, 2), 0).Hash();
  bool format_changes_hash = false;
  for (std::uint32_t width = 3; width <= 10; ++width) {
    format_changes_hash |= Raw(Format(2, width), 0).Hash() != zero_hash;
    format_changes_hash |= Raw(Format(width, 2), 0).Hash() != zero_hash;
  }
  EXPECT_TRUE(format_changes_hash);
}

TEST(UtilFloatingPoint, EveryTinyEncodingFieldsAndValueIdentity) {
  for (const auto significand : {2u, 3u, 4u}) {
    const TinyValues tiny(significand);
    const auto& ref = tiny.Reference();
    const auto width = 2 + significand;
    for (int raw = 0; raw < ref.PatternCount(); ++raw) {
      SCOPED_TRACE(significand);
      SCOPED_TRACE(raw);
      const auto& value = tiny.At(raw);
      const auto packed = Bits(width, static_cast<unsigned>(raw));
      const auto from_bv = Take(Fp::FromBits(tiny.FormatValue(), packed));
      const auto from_fields = Take(Fp::FromFields(
          Bits(1, ref.Negative(raw)),
          Bits(2, static_cast<unsigned>(ref.ExponentBits(raw))),
          Bits(significand - 1, static_cast<unsigned>(ref.FractionBits(raw)))));
      EXPECT_EQ(value, from_bv);
      EXPECT_EQ(value, from_fields);
      EXPECT_EQ(value.Hash(), from_bv.Hash());
      EXPECT_EQ(value.Hash(), from_fields.Hash());
      EXPECT_EQ(value.Format(), tiny.FormatValue());
      EXPECT_EQ(value.Category(), Category(ref, raw));
      for (int other = 0; other < ref.PatternCount(); ++other) {
        EXPECT_EQ(value == tiny.At(other),
                  ref.Canonical(raw) == ref.Canonical(other))
            << other;
      }
      EXPECT_EQ(value.IsNormal(), ref.IsNormal(raw));
      EXPECT_EQ(value.IsSubnormal(), ref.IsSubnormal(raw));
      EXPECT_EQ(value.IsZero(), ref.IsZero(raw));
      EXPECT_EQ(value.IsInfinite(), ref.IsInfinity(raw));
      EXPECT_EQ(value.IsNaN(), ref.IsNan(raw));
      EXPECT_EQ(value.IsNegative(), !ref.IsNan(raw) && ref.Negative(raw));
      EXPECT_EQ(value.IsPositive(), !ref.IsNan(raw) && !ref.Negative(raw));
      EXPECT_EQ(value.BackingBytes(), 0u);
      const auto output = Take(value.ToBits());
      EXPECT_EQ(output.Width(), width);
      if (ref.IsNan(raw)) {
        EXPECT_EQ(value, tiny.At(ref.Nan()));
        EXPECT_EQ(value.Hash(), tiny.At(ref.Nan()).Hash());
        EXPECT_EQ(Take(Fp::FromBits(tiny.FormatValue(), output)), value);
      } else {
        EXPECT_EQ(output, packed);
        EXPECT_EQ(value.SignBit(), ref.Negative(raw));
        ExpectBits(Take(value.ExponentBits()), 2,
                   static_cast<unsigned>(ref.ExponentBits(raw)));
        ExpectBits(Take(value.FractionBits()), significand - 1,
                   static_cast<unsigned>(ref.FractionBits(raw)));
      }
      if (value.Category() != FpCategory::kFiniteBits) {
        const auto special =
            Take(Fp::Special(tiny.FormatValue(), value.Category()));
        EXPECT_EQ(special, value);
        EXPECT_EQ(special.Hash(), value.Hash());
      }
      const auto exact = value.ToRational();
      if (ref.IsFinite(raw)) {
        const auto fraction = ref.Value(raw);
        const auto expected =
            Ratio(fraction.Numerator(), fraction.Denominator());
        ASSERT_TRUE(exact.has_value());
        EXPECT_EQ(exact->Borrow(), expected.Borrow());
      } else {
        ExpectError(exact, NumericError::kInvalidArgument);
      }
    }
    EXPECT_NE(tiny.At(0), tiny.At(ref.SignBit()));
  }
}

TEST(UtilFloatingPoint, ExhaustiveTinyUnaryOperations) {
  for (const auto significand : {2u, 3u, 4u}) {
    const TinyValues tiny(significand);
    const auto& ref = tiny.Reference();
    for (const int a : tiny.Encodings()) {
      const auto& value = tiny.At(a);
      ASSERT_TRUE(Matches(Fp::Abs(value), tiny, ref.Abs(a), "abs",
                          RoundingMode::kRne, a));
      ASSERT_TRUE(Matches(Fp::Neg(value), tiny, ref.Neg(a), "neg",
                          RoundingMode::kRne, a));
      for (const auto& [oracle_mode, mode] : kModes) {
        ASSERT_TRUE(Matches(Fp::Sqrt(mode, value), tiny,
                            ref.Sqrt(a, oracle_mode), "sqrt", mode, a));
        ASSERT_TRUE(Matches(Fp::RoundToIntegral(mode, value), tiny,
                            ref.RoundToIntegral(a, oracle_mode),
                            "roundToIntegral", mode, a));
      }
    }
  }
}

using RoundedBinary = NumericResult<Fp> (*)(RoundingMode, const Fp&, const Fp&);
using FpPredicate = NumericResult<bool> (*)(const Fp&, const Fp&);
using MinMax = NumericResult<FpOutcome<Fp>> (*)(const Fp&, const Fp&);

struct BinaryOp {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  const char* name;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  RoundedBinary evaluate;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  int (TinyFormat::*reference)(int, int, Mode) const;
};

constexpr BinaryOp kBinaryOps[] = {{"add", Fp::Add, &TinyFormat::Add},
                                   {"sub", Fp::Sub, &TinyFormat::Sub},
                                   {"mul", Fp::Mul, &TinyFormat::Mul},
                                   {"div", Fp::Div, &TinyFormat::Div}};

constexpr std::pair<const char*, FpPredicate> kPredicates[] = {
    {"eq", Fp::Eq},
    {"lt", Fp::Lt},
    {"leq", Fp::Leq},
    {"gt", Fp::Gt},
    {"geq", Fp::Geq}};
constexpr std::pair<const char*, MinMax> kChoices[] = {{"min", Fp::Min},
                                                       {"max", Fp::Max}};

void ExpectZeroChoices(const FpOutcome<Fp>& outcome, const Fp& positive,
                       const Fp& negative) {
  ASSERT_EQ(outcome.Kind(), FpOutcomeKind::kTwoChoices);
  const auto& [a, b] = outcome.Choices();
  EXPECT_TRUE((a == positive && b == negative) ||
              (a == negative && b == positive));
}

TEST(UtilFloatingPoint, ExhaustiveTinyOrderedPairs) {
  for (const auto significand : {2u, 3u, 4u}) {
    SCOPED_TRACE(significand);
    const TinyValues tiny(significand);
    const auto& ref = tiny.Reference();
    for (const int a : tiny.Encodings()) {
      SCOPED_TRACE(a);
      for (const int b : tiny.Encodings()) {
        SCOPED_TRACE(b);
        const auto& left = tiny.At(a);
        const auto& right = tiny.At(b);
        for (const auto& [oracle_mode, mode] : kModes) {
          for (const auto& op : kBinaryOps) {
            ASSERT_TRUE(Matches(op.evaluate(mode, left, right), tiny,
                                (ref.*op.reference)(a, b, oracle_mode), op.name,
                                mode, a, b));
          }
        }
        ASSERT_TRUE(Matches(Fp::Rem(left, right), tiny, ref.Rem(a, b), "rem",
                            RoundingMode::kRne, a, b));

        const bool ordered = !ref.IsNan(a) && !ref.IsNan(b);
        const int order = ordered ? ref.Compare(a, b) : 0;
        const std::array answers = {order == 0, order < 0, order <= 0,
                                    order > 0, order >= 0};
        static_assert(answers.size() == std::size(kPredicates));
        for (std::size_t i = 0; i < answers.size(); ++i) {
          const auto& [name, predicate] = kPredicates[i];
          EXPECT_EQ(Take(predicate(left, right)), ordered && answers[i])
              << name;
        }
        for (const auto& [name, select] : kChoices) {
          SCOPED_TRACE(name);
          const auto outcome = Take(select(left, right));
          if (ref.IsZero(a) && ref.IsZero(b) && a != b) {
            ExpectZeroChoices(outcome, tiny.At(0), tiny.At(ref.SignBit()));
            continue;
          }
          int expected = a;
          const bool select_b = select == Fp::Min ? order > 0 : order < 0;
          if (ref.IsNan(a) || select_b) expected = b;
          ASSERT_EQ(outcome.Kind(), FpOutcomeKind::kDefined);
          EXPECT_EQ(outcome.Value(), tiny.At(expected));
        }
      }
    }
  }
}

class UtilFloatingPointFma : public ::testing::TestWithParam<unsigned> {};

TEST_P(UtilFloatingPointFma, ExhaustiveOrderedTriplesInFiveModes) {
  const TinyValues tiny(GetParam());
  const auto& ref = tiny.Reference();
  for (const int a : tiny.Encodings()) {
    for (const int b : tiny.Encodings()) {
      for (const int c : tiny.Encodings()) {
        for (const auto& [oracle_mode, mode] : kModes) {
          ASSERT_TRUE(Matches(Fp::Fma(mode, tiny.At(a), tiny.At(b), tiny.At(c)),
                              tiny, ref.Fma(a, b, c, oracle_mode), "fma", mode,
                              a, b, c));
        }
      }
    }
  }
}

INSTANTIATE_TEST_SUITE_P(TinyFormats, UtilFloatingPointFma,
                         ::testing::Values(2u, 3u, 4u));

TEST(UtilFloatingPoint, FixedTiesSingleRoundingAndIntegralOverflow) {
  const auto format = Format(2, 3);
  const std::array samples = {Fraction(9, 8),  Fraction(-9, 8),
                              Fraction(1, 8),  Fraction(-1, 8),
                              Fraction(15, 4), Fraction(-15, 4)};
  constexpr std::uint64_t expected[6][5] = {
      {4, 5, 5, 4, 4},      {20, 21, 20, 21, 20}, {0, 1, 1, 0, 0},
      {16, 17, 16, 17, 16}, {12, 12, 12, 11, 11}, {28, 28, 27, 28, 27}};
  for (std::size_t i = 0; i < samples.size(); ++i) {
    SCOPED_TRACE(i);
    for (std::size_t k = 0; k < std::size(kModes); ++k) {
      SCOPED_TRACE(k);
      EXPECT_EQ(Rounded(format, samples[i], kModes[k].second),
                Raw(format, expected[i][k]));
    }
  }
  constexpr auto kEven = RoundingMode::kRne;
  const auto one_and_half = Raw(format, 6);
  const auto two = Raw(format, 8);
  const auto minus_two = Raw(format, 24);
  EXPECT_EQ(Take(Fp::Fma(kEven, one_and_half, one_and_half, minus_two)),
            Raw(format, 1));
  const auto product = Take(Fp::Mul(kEven, one_and_half, one_and_half));
  EXPECT_EQ(product, two);
  EXPECT_EQ(Take(Fp::Add(kEven, product, minus_two)), Raw(format, 0));
  const auto negative = Raw(format, 22);
  EXPECT_EQ(Take(Fp::Fma(kEven, one_and_half, negative, two)), Raw(format, 17));
  EXPECT_EQ(Take(Fp::Mul(kEven, two, two)), Raw(format, 12));
  EXPECT_EQ(Take(Fp::Fma(kEven, two, two, minus_two)), two);
  const auto quarter = Raw(format, 1);
  const auto half = Raw(format, 2);
  EXPECT_EQ(Take(Fp::Mul(kEven, quarter, half)), Raw(format, 0));
  EXPECT_EQ(Take(Fp::Fma(kEven, quarter, half, quarter)), half);
  EXPECT_EQ(Take(Fp::Sqrt(kEven, Raw(format, 9))), one_and_half);
  EXPECT_EQ(Take(Fp::Rem(Raw(format, 11), Raw(format, 4))), Raw(format, 18));

  const auto larger = Format(2, 4);
  constexpr std::array positive{24u, 24u, 24u, 20u, 20u};
  constexpr std::array negative_bits{56u, 56u, 52u, 56u, 52u};
  for (std::size_t i = 0; i < std::size(kModes); ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(Take(Fp::RoundToIntegral(kModes[i].second, Raw(larger, 22))),
              Raw(larger, positive[i]));
    EXPECT_EQ(Take(Fp::RoundToIntegral(kModes[i].second, Raw(larger, 54))),
              Raw(larger, negative_bits[i]));
    EXPECT_EQ(Take(Fp::RoundToIntegral(kModes[i].second, Raw(larger, 8))),
              Raw(larger, 8));
  }
}

TEST(UtilFloatingPoint, RationalRoundingOnBothSidesOfEveryTinyMidpoint) {
  for (const auto significand : {2u, 3u, 4u}) {
    SCOPED_TRACE(significand);
    const TinyValues tiny(significand);
    const auto& ref = tiny.Reference();
    const auto& grid = ref.Grid();
    const auto check = [&](Fraction magnitude) {
      for (const auto sign : {-1, 1}) {
        const auto value = Fraction(sign) * magnitude;
        SCOPED_TRACE(::testing::Message()
                     << value.Numerator() << "/" << value.Denominator());
        for (const auto& [oracle_mode, mode] : kModes) {
          SCOPED_TRACE(static_cast<int>(mode));
          EXPECT_EQ(Rounded(tiny.FormatValue(), value, mode),
                    tiny.At(ref.Round(value, oracle_mode)));
        }
      }
    };
    for (const auto point : grid) check(point);
    for (std::size_t high = 1; high < grid.size(); ++high) {
      const auto midpoint = (grid[high] + grid[high - 1]) / Fraction(2);
      check(midpoint - Fraction(1, 128));
      check(midpoint);
      check(midpoint + Fraction(1, 128));
    }
    check(Fraction(17, 4));
  }
}

TEST(UtilFloatingPoint, ExhaustiveConversionsAcrossTinyFormatsAndBvWidths) {
  for (const auto significand : {2u, 3u, 4u}) {
    SCOPED_TRACE(significand);
    const TinyValues tiny(significand);
    const auto& ref = tiny.Reference();
    for (const auto destination_size : {2u, 3u, 4u}) {
      SCOPED_TRACE(destination_size);
      const TinyValues destination(destination_size);
      for (const int a : tiny.Encodings()) {
        SCOPED_TRACE(a);
        for (const auto& [oracle_mode, mode] : kModes) {
          ASSERT_TRUE(Matches(
              Fp::Convert(destination.FormatValue(), mode, tiny.At(a)),
              destination, ref.Convert(a, destination.Reference(), oracle_mode),
              "convert", mode, a));
        }
      }
    }
    for (std::uint32_t width = 1; width <= 6; ++width) {
      SCOPED_TRACE(width);
      const auto modulus = std::int64_t{1} << width;
      for (std::int64_t u = 0; u < modulus; ++u) {
        const auto bv = Bits(width, static_cast<std::uint64_t>(u));
        const auto s = u < modulus / 2 ? u : u - modulus;
        for (const auto& [oracle_mode, mode] : kModes) {
          ASSERT_TRUE(Matches(Fp::FromUnsignedBv(tiny.FormatValue(), mode, bv),
                              tiny, ref.Round(Fraction(u), oracle_mode),
                              "from_unsigned_bv", mode, static_cast<int>(u)));
          ASSERT_TRUE(Matches(Fp::FromSignedBv(tiny.FormatValue(), mode, bv),
                              tiny, ref.Round(Fraction(s), oracle_mode),
                              "from_signed_bv", mode, static_cast<int>(u)));
        }
      }
      for (const int a : tiny.Encodings()) {
        SCOPED_TRACE(a);
        for (const auto& [oracle_mode, mode] : kModes) {
          SCOPED_TRACE(static_cast<int>(mode));
          const bool finite = ref.IsFinite(a);
          const auto rounded =
              finite ? fp_reference::RoundInteger(ref.Value(a), oracle_mode)
                     : 0;
          for (const bool signed_result : {false, true}) {
            SCOPED_TRACE(signed_result);
            const auto lower = signed_result ? -modulus / 2 : 0;
            const auto upper = signed_result ? modulus / 2 : modulus;
            const auto call = signed_result ? Fp::ToSignedBv : Fp::ToUnsignedBv;
            const auto outcome = Take(call(Unsigned(width), mode, tiny.At(a)));
            if (finite && lower <= rounded && rounded < upper) {
              ASSERT_EQ(outcome.Kind(), FpOutcomeKind::kDefined);
              const auto residue =
                  static_cast<std::uint64_t>((rounded + modulus) % modulus);
              ExpectBits(outcome.Value(), width, residue);
            } else {
              ASSERT_EQ(outcome.Kind(), FpOutcomeKind::kUnspecified);
              EXPECT_EQ(outcome.Destination(), width);
            }
          }
        }
      }
    }
  }
}

TEST(UtilFloatingPoint,
     InvalidModesAndMismatchedFormatsAreCheckedBeforeDispatch) {
  const auto format = Format(2, 3);
  const auto q = Ratio(0);
  const auto nan = Raw(format, 13);
  const auto other = Raw(Format(2, 2), 0);
  const auto invalid = static_cast<RoundingMode>(255);
  for (const auto& [name, evaluate, reference] : kBinaryOps) {
    SCOPED_TRACE(name);
    ExpectError(evaluate(invalid, nan, nan), NumericError::kInvalidArgument);
    ExpectError(evaluate(RoundingMode::kRne, nan, other),
                NumericError::kInvalidArgument);
    ExpectError(evaluate(RoundingMode::kRne, other, nan),
                NumericError::kInvalidArgument);
  }
  for (const auto& [name, predicate] : kPredicates) {
    SCOPED_TRACE(name);
    ExpectError(predicate(nan, other), NumericError::kInvalidArgument);
    ExpectError(predicate(other, nan), NumericError::kInvalidArgument);
  }
  for (const auto& [name, select] : kChoices) {
    SCOPED_TRACE(name);
    ExpectError(select(nan, other), NumericError::kInvalidArgument);
    ExpectError(select(other, nan), NumericError::kInvalidArgument);
  }
  constexpr auto kRne = RoundingMode::kRne;
  ExpectError(Fp::Rem(nan, other), NumericError::kInvalidArgument);
  ExpectError(Fp::Rem(other, nan), NumericError::kInvalidArgument);
  ExpectError(Fp::Fma(kRne, nan, other, other), NumericError::kInvalidArgument);
  ExpectError(Fp::Fma(kRne, other, nan, other), NumericError::kInvalidArgument);
  ExpectError(Fp::Fma(kRne, other, other, nan), NumericError::kInvalidArgument);
  ExpectError(Fp::Fma(invalid, nan, nan, nan), NumericError::kInvalidArgument);
  ExpectError(Fp::Sqrt(invalid, nan), NumericError::kInvalidArgument);
  ExpectError(Fp::RoundToIntegral(invalid, nan),
              NumericError::kInvalidArgument);
  ExpectError(Fp::Convert(format, invalid, nan),
              NumericError::kInvalidArgument);
  ExpectError(Fp::FromSignedBv(format, invalid, Bits(1, 0)),
              NumericError::kInvalidArgument);
  ExpectError(Fp::FromUnsignedBv(format, invalid, Bits(1, 0)),
              NumericError::kInvalidArgument);
  ExpectError(RoundRationalToFp(format, invalid, q.Borrow()),
              NumericError::kInvalidArgument);
  for (const auto convert : {Fp::ToSignedBv, Fp::ToUnsignedBv}) {
    ExpectError(convert(IntegerView(1), invalid, nan),
                NumericError::kInvalidArgument);
    ExpectError(convert(IntegerView(0), kRne, nan),
                NumericError::kInvalidArgument);
    ExpectError(convert(IntegerView(-1), kRne, nan),
                NumericError::kInvalidArgument);
    ExpectError(convert(Unsigned(std::uint64_t{1} + UINT32_MAX), kRne, nan),
                NumericError::kSizeLimit);
    const auto unspecified = Take(convert(Unsigned(UINT32_MAX), kRne, nan));
    ASSERT_EQ(unspecified.Kind(), FpOutcomeKind::kUnspecified);
    EXPECT_EQ(unspecified.Destination(), UINT32_MAX);
  }
}

TEST(UtilFloatingPoint, OutcomesRetainOwnedValuesAndDestinationMetadata) {
  const auto format = Format(2, 3);
  const auto plus = Raw(format, 0);
  const auto minus = Raw(format, 16);
  for (const auto& [name, select] : kChoices) {
    SCOPED_TRACE(name);
    const auto choices = Take(select(Raw(format, 0), Raw(format, 16)));
    ExpectZeroChoices(choices, plus, minus);
    const auto reversed = Take(select(minus, plus));
    ExpectZeroChoices(reversed, plus, minus);
    for (const auto* zero : {&plus, &minus}) {
      const auto same = Take(select(*zero, *zero));
      ASSERT_EQ(same.Kind(), FpOutcomeKind::kDefined);
      EXPECT_EQ(same.Value(), *zero);
    }
  }
  const auto absent = FpOutcome<Fp>::Unspecified(format);
  EXPECT_EQ(absent.Kind(), FpOutcomeKind::kUnspecified);
  EXPECT_EQ(absent.Destination(), format);

  const auto small_negative = Raw(format, 17);
  const auto rtz = Take(
      Fp::ToUnsignedBv(IntegerView(2), RoundingMode::kRtz, small_negative));
  ASSERT_EQ(rtz.Kind(), FpOutcomeKind::kDefined);
  ExpectBits(rtz.Value(), 2, 0);
  const auto rtn = Take(
      Fp::ToUnsignedBv(IntegerView(2), RoundingMode::kRtn, small_negative));
  ASSERT_EQ(rtn.Kind(), FpOutcomeKind::kUnspecified);
  EXPECT_EQ(rtn.Destination(), 2u);

  const auto source = Format(2, 65);
  const auto picked =
      Take(Fp::Max(Raw(source, "36893488147419103233"), Raw(source, 0)));
  ASSERT_EQ(picked.Kind(), FpOutcomeKind::kDefined);
  EXPECT_EQ(picked.Value(), Raw(source, "36893488147419103233"));
  EXPECT_GT(picked.Value().BackingBytes(), 0u);
}

TEST(UtilFloatingPoint, SparseMaximumFormatAndPackedWordBoundary) {
  const auto max_format = Format(UINT32_MAX - 2, 2);
  const auto tiny = Raw(max_format, 1);
  const auto zero = Take(Fp::Special(max_format, FpCategory::kPositiveZero));
  const auto nan = Take(Fp::Special(max_format, FpCategory::kNaN));
  EXPECT_TRUE(tiny.IsSubnormal());
  EXPECT_TRUE(tiny.IsPositive());
  EXPECT_TRUE(zero.IsZero());
  EXPECT_TRUE(nan.IsNaN());
  EXPECT_EQ(tiny.BackingBytes(), 0u);
  EXPECT_EQ(nan.BackingBytes(), 0u);
  EXPECT_EQ(zero.BackingBytes(), 0u);
  ExpectBits(Take(tiny.ToBits()), UINT32_MAX, 1);
  EXPECT_EQ(Take(Fp::Sub(RoundingMode::kRne, tiny, tiny)), zero);
  EXPECT_EQ(
      Take(Fp::FromFields(Bits(1, 0), Bits(UINT32_MAX - 2, 0), Bits(1, 1))),
      tiny);

  const auto format = Format(2, 63);
  const auto inlined = Raw(format, std::uint64_t{1} << 63);
  EXPECT_TRUE(inlined.IsNormal());
  EXPECT_EQ(inlined.BackingBytes(), 0u);
  ExpectBits(Take(inlined.ToBits()), 65, std::uint64_t{1} << 63);
  const auto wide = Raw(format, "18446744073709551617");
  EXPECT_TRUE(wide.IsSubnormal());
  EXPECT_TRUE(wide.IsNegative());
  EXPECT_GT(wide.BackingBytes(), 0u);
  const auto magnitude = Take(Fp::Abs(wide));
  EXPECT_EQ(magnitude, Raw(format, 1));
  EXPECT_EQ(magnitude.BackingBytes(), 0u);
  EXPECT_EQ(Take(Fp::Neg(magnitude)), wide);
  const auto parts = Take(wide.ToBits());
  EXPECT_EQ(parts.Chunk(0), 1u);
  EXPECT_EQ(parts.Chunk(1), 1u);
}

TEST(UtilFloatingPoint, HugeExponentDifferencesRoundWithoutExpandingDyadics) {
  constexpr auto kRne = RoundingMode::kRne;
  constexpr auto kRna = RoundingMode::kRna;
  constexpr auto kRtp = RoundingMode::kRtp;
  constexpr auto kRtn = RoundingMode::kRtn;
  constexpr auto kRtz = RoundingMode::kRtz;
  const auto format64 = Format(64, 4);
  const auto one64 = Rounded(format64, Fraction(1), kRne);
  ExpectBits(Take(one64.ExponentBits()), 64, INT64_MAX);
  const auto format = Format(65, 4);
  const auto one = Raw(format, "147573952589676412920");
  const auto next = Raw(format, "147573952589676412921");
  const auto previous = Raw(format, "147573952589676412919");
  const auto tiny = Raw(format, 1);
  const auto zero = Raw(format, 0);
  const auto negative_zero =
      Take(Fp::Special(format, FpCategory::kNegativeZero));
  const auto inf = Take(Fp::Special(format, FpCategory::kPositiveInfinity));
  const auto largest = Raw(format, "295147905179352825847");
  const auto huge = Raw(format, "295147905179352825840");
  const auto two = Raw(format, "147573952589676412928");
  const auto three = Raw(format, "147573952589676412932");
  const auto minus_one = Take(Fp::Neg(one));
  EXPECT_EQ(Rounded(format, Fraction(1), kRne), one);
  ExpectBits(Take(one.ExponentBits()), 65, UINT64_MAX);
  EXPECT_EQ(Take(Fp::Add(kRne, one, tiny)), one);
  EXPECT_EQ(Take(Fp::Add(kRtp, one, tiny)), next);
  EXPECT_EQ(Take(Fp::Sub(kRtn, one, tiny)), previous);
  EXPECT_EQ(Take(Fp::Sub(kRne, one, tiny)), one);
  EXPECT_EQ(Take(Fp::Sub(kRtn, huge, huge)), negative_zero);
  EXPECT_EQ(Take(Fp::Sub(kRne, huge, huge)), zero);
  EXPECT_EQ(Take(Fp::Mul(kRne, huge, two)), inf);
  EXPECT_EQ(Take(Fp::Mul(kRtz, huge, two)), largest);
  EXPECT_EQ(Take(Fp::Div(kRne, tiny, two)), zero);
  EXPECT_EQ(Take(Fp::Div(kRna, tiny, two)), tiny);
  EXPECT_EQ(Take(Fp::Div(kRtp, tiny, two)), tiny);
  EXPECT_EQ(Take(Fp::Div(kRne, huge, huge)), one);
  EXPECT_EQ(Take(Fp::Fma(kRtp, one, one, tiny)), next);
  EXPECT_EQ(Take(Fp::Rem(huge, three)), minus_one);
  EXPECT_EQ(Take(Fp::Rem(tiny, huge)), tiny);
  EXPECT_EQ(Take(Fp::RoundToIntegral(kRne, huge)), huge);
  const auto narrowing = Format(2, 3);
  EXPECT_TRUE(Take(Fp::Convert(narrowing, kRne, huge)).IsInfinite());
  EXPECT_EQ(Take(Fp::Convert(narrowing, kRtp, tiny)), Raw(narrowing, 1));
  EXPECT_EQ(Take(Fp::Convert(narrowing, kRne, tiny)), Raw(narrowing, 0));
  for (const auto convert : {Fp::ToSignedBv, Fp::ToUnsignedBv}) {
    const auto result = Take(convert(Unsigned(UINT32_MAX), kRne, huge));
    ASSERT_EQ(result.Kind(), FpOutcomeKind::kUnspecified);
    EXPECT_EQ(result.Destination(), UINT32_MAX);
  }
  ExpectError(huge.ToRational(), NumericError::kSizeLimit);
  ExpectError(tiny.ToRational(), NumericError::kSizeLimit);
}

TEST(UtilFloatingPoint, ExactMantissaWithWideExponentAndSignificand) {
  constexpr auto kRne = RoundingMode::kRne;
  constexpr auto kExactValue = "55340232221128654849";  // 2^65 + 2^64 + 1
  const auto format = Format(65, 66);
  const auto integer = Int(kExactValue);
  const auto rational = Take(Rational::FromInteger(integer.Borrow()));
  const auto integer_bits =
      Take(BitVector::FromIntegerExact(Unsigned(66), integer.Borrow()));
  const auto fp = Take(Fp::FromUnsignedBv(format, kRne, integer_bits));
  EXPECT_EQ(Take(RoundRationalToFp(format, kRne, rational.Borrow())), fp);
  EXPECT_EQ(fp.Format(), format);
  EXPECT_TRUE(fp.IsNormal());
  EXPECT_FALSE(fp.SignBit());

  const auto exponent = Take(fp.ExponentBits());
  EXPECT_EQ(exponent.Width(), 65u);
  EXPECT_EQ(exponent.Chunk(0), 64u);
  EXPECT_EQ(exponent.Chunk(1), 1u);
  const auto fraction = Take(fp.FractionBits());
  EXPECT_EQ(fraction.Width(), 65u);
  EXPECT_EQ(fraction.Chunk(0), 1u);
  EXPECT_EQ(fraction.Chunk(1), 1u);
  const auto packed = Take(fp.ToBits());
  EXPECT_EQ(packed.Width(), 131u);
  EXPECT_EQ(packed.Chunk(0), 1u);
  EXPECT_EQ(packed.Chunk(1), 129u);
  EXPECT_EQ(packed.Chunk(2), 2u);
  EXPECT_EQ(Take(fp.ToRational()).ToString(), kExactValue);
}

TEST(UtilFloatingPoint, WideBvConversionsAndCopyMoveOwnership) {
  constexpr auto kRne = RoundingMode::kRne;
  const auto format = Format(8, 80);
  const auto byte = Bits(8, 255);
  EXPECT_EQ(Take(Fp::FromSignedBv(format, kRne, byte)),
            Rounded(format, Fraction(-1), kRne));
  EXPECT_EQ(Take(Fp::FromUnsignedBv(format, kRne, byte)),
            Rounded(format, Fraction(255), kRne));
  const auto wide =
      Take(BitVector::FromBinaryDigits("1" + std::string(63, '0') + "1"));
  for (const bool signed_input : {false, true}) {
    SCOPED_TRACE(signed_input);
    const auto from = signed_input ? Fp::FromSignedBv : Fp::FromUnsignedBv;
    const auto to = signed_input ? Fp::ToSignedBv : Fp::ToUnsignedBv;
    const auto fp = Take(from(format, kRne, wide));
    EXPECT_EQ(fp.IsNegative(), signed_input);
    EXPECT_EQ(Take(fp.ToRational()).ToString(),
              signed_input ? "-18446744073709551615" : "18446744073709551617");
    const auto back = Take(to(IntegerView(65), kRne, fp));
    ASSERT_EQ(back.Kind(), FpOutcomeKind::kDefined);
    EXPECT_EQ(back.Value(), wide);
    auto copy = fp;
    CheckSelfAssignment(copy);
    auto moved = std::move(copy);
    EXPECT_EQ(copy, Raw(Format(2, 2), 0));
    auto assigned = Take(Fp::Abs(fp));
    assigned = std::move(moved);
    EXPECT_EQ(moved, Raw(Format(2, 2), 0));
    EXPECT_EQ(assigned, fp);
    EXPECT_EQ(assigned.Hash(), fp.Hash());
    EXPECT_GT(assigned.BackingBytes(), 0u);
  }
}

}  // namespace
}  // namespace dandelion::testing::util

#ifndef DANDELION_BENCHMARKS_UTIL_FIXTURES_H
#define DANDELION_BENCHMARKS_UTIL_FIXTURES_H

import std;
import dandelion.util.numeric;

namespace dandelion::util_bench {

using numeric::BitVector;
using numeric::FloatingPointValue;
using numeric::FpCategory;
using numeric::FpFormat;
using numeric::FpOutcomeKind;
using numeric::Integer;
using numeric::IntegerView;
using numeric::NumericResult;
using numeric::Rational;
using numeric::RoundRationalToFp;
using numeric::RoundingMode;

inline constexpr std::size_t kBatchSize = 40;
inline constexpr std::uint64_t kSeed = 0x44414e44454c494f;
inline constexpr auto kRounding = RoundingMode::kRne;
inline constexpr std::array kBvWidths{32u, 64u, 128u, 256u, 4096u};

using NamedFormat = std::pair<std::string_view, FpFormat>;
inline const std::array kFpFormats{
    NamedFormat{"binary16", FpFormat::FromCanonicalWidths(5, 11)},
    NamedFormat{"binary32", FpFormat::FromCanonicalWidths(8, 24)},
    NamedFormat{"binary64", FpFormat::FromCanonicalWidths(11, 53)},
    NamedFormat{"binary128", FpFormat::FromCanonicalWidths(15, 113)},
    NamedFormat{"3_4", FpFormat::FromCanonicalWidths(3, 4)},
    NamedFormat{"20_24", FpFormat::FromCanonicalWidths(20, 24)},
};

template <class Generator>
auto Generate(Generator generator) {
  std::mt19937_64 random(kSeed);
  std::array<std::invoke_result_t<Generator, std::mt19937_64&, std::size_t>,
             kBatchSize>
      inputs;
  for (std::size_t index = 0; index < inputs.size(); ++index)
    inputs[index] = generator(random, index);
  return inputs;
}

inline std::uint64_t Mask(std::uint32_t bits) {
  return std::numeric_limits<std::uint64_t>::max() >> (64 - bits);
}

inline std::uint64_t ExactWord(std::mt19937_64& random, std::uint32_t bits) {
  return (random() & Mask(bits)) | (std::uint64_t{1} << (bits - 1));
}

inline std::vector<std::uint64_t> Magnitude(std::mt19937_64& random,
                                            std::uint32_t bits) {
  std::vector<std::uint64_t> limbs((bits - 1) / 64 + 1);
  for (auto& limb : limbs) limb = random();
  limbs.back() = ExactWord(random, (bits - 1) % 64 + 1);
  return limbs;
}

inline Integer ExactInteger(std::mt19937_64& random, std::uint32_t bits,
                            bool negative = false) {
  if (bits < 64) {
    const auto magnitude = static_cast<std::int64_t>(ExactWord(random, bits));
    return Integer(negative ? -magnitude : magnitude);
  }
  const auto limbs = Magnitude(random, bits);
  return IntegerView::FromCanonicalMagnitude(limbs, negative).Copy();
}

inline std::array<Integer, 2> WordIntegers(std::mt19937_64& random) {
  return {ExactInteger(random, 60, true), ExactInteger(random, 29)};
}

inline std::array<Integer, 2> SmallProductIntegers(std::mt19937_64& random) {
  return {ExactInteger(random, 32, true), ExactInteger(random, 30)};
}

inline std::array<Integer, 2> AddOverflowIntegers(std::mt19937_64& random) {
  return {Integer(std::numeric_limits<std::int64_t>::max() -
                  static_cast<std::int64_t>(random() % 1024)),
          ExactInteger(random, 24)};
}

inline std::array<Integer, 2> MixedIntegers(std::mt19937_64& random,
                                            std::size_t index) {
  if (index % 10 == 9)
    return {ExactInteger(random, 256, true), ExactInteger(random, 128)};
  return SmallProductIntegers(random);
}

inline BitVector DenseBv(std::mt19937_64& random, std::uint32_t width,
                         std::uint32_t effective_bits) {
  if (effective_bits <= 64)
    return BitVector::FromUint64(IntegerView(width),
                                 ExactWord(random, effective_bits))
        .value();
  const auto limbs = Magnitude(random, effective_bits);
  return BitVector::FromIntegerExact(
             IntegerView(width),
             IntegerView::FromCanonicalMagnitude(limbs, false))
      .value();
}

inline BitVector SparseBv(std::mt19937_64& random, std::uint32_t width) {
  return DenseBv(random, width, 64);
}

inline Rational Fraction(std::int64_t numerator, std::uint64_t denominator) {
  return Rational::Make(IntegerView(numerator),
                        IntegerView::FromUnsigned(denominator))
      .value();
}

inline Rational WordRational(std::mt19937_64& random) {
  return Fraction(static_cast<std::int64_t>(ExactWord(random, 19) | 1),
                  std::uint64_t{1} << (4 + random() % 12));
}

inline Rational WideRational(std::mt19937_64& random, std::uint32_t bits) {
  const auto numerator = ExactInteger(random, bits);
  const auto denominator = ExactInteger(random, bits);
  return Rational::Make(numerator.Borrow(), denominator.Borrow()).value();
}

struct RationalChain {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::array<Rational, kBatchSize + 1> values;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::array<Rational, kBatchSize> factors;
};

inline RationalChain MakeRationalChain(std::uint32_t bits) {
  std::mt19937_64 random(kSeed);
  RationalChain chain;
  for (std::size_t index = 0; index < chain.values.size(); ++index) {
    const auto numerator = ExactInteger(random, bits);
    const auto denominator = IntegerView(
        static_cast<std::int64_t>(std::uint64_t{1} << (2 + index % 3)));
    chain.values[index] =
        Rational::Make(numerator.Borrow(), denominator).value();
  }
  for (std::size_t index = 0; index < chain.factors.size(); ++index) {
    chain.factors[index] =
        (chain.values[index + 1].Borrow() / chain.values[index].Borrow())
            .value();
  }
  return chain;
}

inline BitVector FpBits(std::mt19937_64& random, FpFormat format,
                        std::uint32_t exponent, bool negative) {
  const auto fraction = format.SignificandWidth() - 1;
  const auto sign = static_cast<std::uint64_t>(negative);
  if (format.Width() <= 64) {
    const auto bits = (sign << (format.Width() - 1)) |
                      (static_cast<std::uint64_t>(exponent) << fraction) |
                      (random() & Mask(fraction));
    return BitVector::FromUint64(IntegerView(format.Width()), bits).value();
  }
  // The benchmark's two-limb format keeps the entire exponent in the top limb.
  const std::array limbs{
      random(),
      (sign << (format.Width() - 65)) |
          (static_cast<std::uint64_t>(exponent) << (fraction - 64)) |
          (random() & Mask(fraction - 64)),
  };
  return BitVector::FromIntegerExact(
             IntegerView(format.Width()),
             IntegerView::FromCanonicalMagnitude(limbs, false))
      .value();
}

inline FloatingPointValue NormalFp(std::mt19937_64& random, FpFormat format,
                                   std::uint32_t exponent, bool negative) {
  return FloatingPointValue::FromBits(
             format, FpBits(random, format, exponent, negative))
      .value();
}

inline std::array<FloatingPointValue, 2> FpWithExponentGap(
    std::mt19937_64& random, FpFormat format) {
  const auto highest_normal = (std::uint32_t{1} << format.ExponentWidth()) - 2;
  return {NormalFp(random, format, highest_normal - 1, false),
          NormalFp(random, format, 2, false)};
}

}  // namespace dandelion::util_bench

#endif  // DANDELION_BENCHMARKS_UTIL_FIXTURES_H

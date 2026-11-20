module;
#include <cstdint>
#include <gmp.h>

module dandelion.util.numeric;
import :internal;
import std;

#ifdef DANDELION_UTIL_TESTING
#include "../../tests/util/numeric_test_peer.h"
#endif

namespace dandelion::numeric::detail {

struct FpAccess {
  static inline FloatingPointValue Special(FpFormat format,
                                           FpCategory category) noexcept {
    FloatingPointValue result;
    result.format_ = format;
    result.category_ = category;
    return result;
  }

  static std::uint64_t Chunk(const FloatingPointValue& value,
                             std::uint64_t i) noexcept {
    if (value.limb_count_ == 0) return i == 0 ? value.word_ : 0;
    return i < value.limb_count_ ? value.limbs_[i] : 0;
  }

  static std::uint64_t BitLength(const FloatingPointValue& value) noexcept {
    if (value.limb_count_ == 0) return WordBits(value.word_);
    return 64 * std::uint64_t{value.limb_count_ - 1} +
           WordBits(value.limbs_[value.limb_count_ - 1]);
  }

  static inline FloatingPointValue FiniteWord(FpFormat format,
                                              std::uint64_t word) noexcept {
    auto result = Special(format, FpCategory::kFiniteBits);
    result.word_ = word;
    return result;
  }

  static inline FloatingPointValue TakeFinite(
      FpFormat format, std::unique_ptr<std::uint64_t[]> limbs,
      std::uint32_t count) noexcept {
    auto result = Special(format, FpCategory::kFiniteBits);
    result.limbs_ = limbs.release();
    result.limb_count_ = count;
    return result;
  }

  template <class ChunkAt>
  static FloatingPointValue Finite(FpFormat format, std::uint64_t bits,
                                   ChunkAt chunk) {
    auto count = Words(bits);
    auto last = chunk(count - 1);
    while (count > 1 && last == 0) last = chunk(--count - 1);
    if (count == 1) return FiniteWord(format, last);
    auto limbs = NewPacked(count);
    limbs[count - 1] = last;
    for (std::uint64_t i = 0; i + 1 < count; ++i) limbs[i] = chunk(i);
    return TakeFinite(format, std::move(limbs),
                      static_cast<std::uint32_t>(count));
  }

  template <class T>
  static FpOutcome<T> Defined(T value) {
    return FpOutcome<T>(std::move(value));
  }

  static FpOutcome<FloatingPointValue> Choices(FloatingPointValue a,
                                               FloatingPointValue b) {
    return FpOutcome<FloatingPointValue>(std::move(a), std::move(b));
  }

  static FpOutcome<BitVector> Unspecified(std::uint32_t width) {
    return FpOutcome<BitVector>(width);
  }

  static const void* Address(const FloatingPointValue& value) noexcept {
    return value.limb_count_ != 0 ? value.limbs_ : nullptr;
  }
};

}  // namespace dandelion::numeric::detail

namespace dandelion::numeric {
namespace {

using namespace detail;
using Fp = FloatingPointValue;
using Bits = BitVectorAccess;

void ValidateMode(RoundingMode mode) {
  Require(mode <= RoundingMode::kRtz, NumericError::kInvalidArgument);
}

void SameFormat(const Fp& a, const Fp& b) {
  Require(a.Format() == b.Format(), NumericError::kInvalidArgument);
}

Fp Zero(FpFormat format, bool negative) noexcept {
  return FpAccess::Special(
      format, negative ? FpCategory::kNegativeZero : FpCategory::kPositiveZero);
}

Fp Infinity(FpFormat format, bool negative) noexcept {
  return FpAccess::Special(format, negative ? FpCategory::kNegativeInfinity
                                            : FpCategory::kPositiveInfinity);
}

Fp NaN(FpFormat format) noexcept {
  return FpAccess::Special(format, FpCategory::kNaN);
}

template <class Chunk>
std::uint64_t ReadRight(Chunk chunk, std::uint64_t i,
                        std::uint64_t shift) noexcept {
  const auto offset = shift / 64, fraction = shift % 64;
  auto word = chunk(i + offset) >> fraction;
  if (fraction != 0) word |= chunk(i + offset + 1) << (64 - fraction);
  return word;
}

template <class Chunk>
std::uint64_t ReadLeft(Chunk chunk, std::uint64_t i,
                       std::uint64_t shift) noexcept {
  const auto offset = shift / 64, fraction = shift % 64;
  if (i < offset) return 0;
  auto word = chunk(i - offset) << fraction;
  if (fraction != 0 && i > offset)
    word |= chunk(i - offset - 1) >> (64 - fraction);
  return word;
}

std::uint64_t Flag(std::uint64_t index, std::uint64_t bit) noexcept {
  return index == bit / 64 ? std::uint64_t{1} << (bit % 64) : 0;
}

std::uint64_t FieldMask(std::uint64_t i, std::uint64_t begin,
                        std::uint64_t end) noexcept {
  const auto base = i * 64;
  if (end <= base || begin >= base + 64) return 0;
  return Mask(end - base) & ~Mask(begin > base ? begin - base : 0);
}

template <class Chunk>
bool IsFieldFilled(std::uint64_t bits, std::uint64_t start,
                   std::uint64_t length, Chunk chunk, bool ones) noexcept {
  const auto end = start + length;
  if (bits <= start) return !ones;
  if (ones && bits < end) return false;
  const auto stop = std::min(bits, end);
  for (auto i = start / 64; i < Words(stop); ++i) {
    const auto mask = FieldMask(i, start, end);
    if ((chunk(i) & mask) != (ones ? mask : 0)) return false;
  }
  return true;
}

FpCategory ClassifyBits(FpFormat format,
                        std::span<const std::uint64_t> limbs) noexcept {
  if (limbs.empty()) return FpCategory::kPositiveZero;
  const auto sign = format.Width() - 1;
  const auto sign_mask = std::uint64_t{1} << (sign % 64);
  const auto top = limbs.back();
  const bool negative = limbs.size() > sign / 64 && (top & sign_mask) != 0;
  if (negative && top == sign_mask &&
      std::ranges::all_of(limbs.first(limbs.size() - 1),
                          [](auto word) { return word == 0; }))
    return FpCategory::kNegativeZero;

  const auto tail = format.SignificandWidth() - 1;
  const auto last = (sign - 1) / 64;
  if (last >= limbs.size()) return FpCategory::kFiniteBits;
  const auto mask = FieldMask(last, tail, sign);
  if ((limbs[last] & mask) != mask) return FpCategory::kFiniteBits;

  const auto chunk = [limbs](auto i) { return limbs[i]; };
  const auto bits = std::uint64_t{limbs.size()} * 64;
  if (tail / 64 != last &&
      !IsFieldFilled(bits, tail, format.ExponentWidth(), chunk, true))
    return FpCategory::kFiniteBits;
  if (!IsFieldFilled(bits, 0, tail, chunk, false)) return FpCategory::kNaN;
  return negative ? FpCategory::kNegativeInfinity
                  : FpCategory::kPositiveInfinity;
}

[[gnu::noinline]] NumericResult<Fp> CopyPackedFinite(
    FpFormat format, std::span<const std::uint64_t> bits) {
  return Checked([&] {
    auto limbs = NewPacked(bits.size());
    std::ranges::copy(bits, limbs.get());
    return FpAccess::TakeFinite(format, std::move(limbs),
                                static_cast<std::uint32_t>(bits.size()));
  });
}

NumericResult<Fp> FromPackedBits(FpFormat format,
                                 std::span<const std::uint64_t> bits) {
  const auto category = ClassifyBits(format, bits);
  if (category != FpCategory::kFiniteBits)
    return FpAccess::Special(format, category);
  if (bits.size() == 1) return FpAccess::FiniteWord(format, bits.front());
  [[unlikely]] return CopyPackedFinite(format, bits);
}

auto PackedChunks(const Fp& value) noexcept {
  return [&value](auto i) { return FpAccess::Chunk(value, i); };
}

BitVector Field(const Fp& value, std::uint32_t start, std::uint32_t length) {
  if (value.IsNaN() || value.IsInfinite())
    return start != 0
               ? Bits::Build(length, length, [](auto) { return UINT64_MAX; })
               : Bits::Word(length, value.IsNaN());
  const auto bits = FpAccess::BitLength(value);
  return Bits::Build(
      length, bits - std::min<std::uint64_t>(bits, start),
      [&](auto i) { return ReadRight(PackedChunks(value), i, start); });
}

Fp WithSign(const Fp& value, bool negative) {
  const auto format = value.Format();
  if (value.IsNaN()) return NaN(format);
  if (value.IsZero()) return Zero(format, negative);
  if (value.IsInfinite()) return Infinity(format, negative);
  if (value.SignBit() == negative) return value;
  const auto sign = format.Width() - 1;
  const auto bits = negative ? format.Width() : FpAccess::BitLength(value) - 1;
  return FpAccess::Finite(format, bits, [&](auto i) {
    return FpAccess::Chunk(value, i) ^ Flag(i, sign);
  });
}

int CompareFields(const Fp& a, const Fp& b) noexcept {
  const auto sign = a.Format().Width() - 1;
  const auto bits = std::min<std::uint64_t>(
      sign, std::max(FpAccess::BitLength(a), FpAccess::BitLength(b)));
  for (auto i = Words(bits); i-- != 0;) {
    const auto mask = Mask(sign - i * 64);
    const auto x = FpAccess::Chunk(a, i) & mask;
    const auto y = FpAccess::Chunk(b, i) & mask;
    if (x != y) return x < y ? -1 : 1;
  }
  return 0;
}

// Operands have the same format and neither is NaN.
int Compare(const Fp& a, const Fp& b) noexcept {
  if (a.IsZero() && b.IsZero()) return 0;
  if (a.SignBit() != b.SignBit()) return a.SignBit() ? -1 : 1;
  const auto direction = a.SignBit() ? -1 : 1;
  if (a.IsInfinite() || b.IsInfinite())
    return direction * (a.IsInfinite() - b.IsInfinite());
  return direction * CompareFields(a, b);
}

bool Outward(RoundingMode mode, bool negative) noexcept {
  return mode == (negative ? RoundingMode::kRtn : RoundingMode::kRtp);
}

bool Increment(RoundingMode mode, bool negative, bool inexact, int half,
               bool odd) noexcept {
  switch (mode) {
    case RoundingMode::kRne:
      return half > 0 || (half == 0 && odd);
    case RoundingMode::kRna:
      return half >= 0;
    case RoundingMode::kRtp:
    case RoundingMode::kRtn:
      return inexact && Outward(mode, negative);
    case RoundingMode::kRtz:
      return false;
  }
  std::unreachable();
}

Z AddWord(mpz_srcptr left, std::int64_t right) {
  const ZView word{IntegerView(right)};
  return AddZ(left, word.Get());
}

struct Dyadic {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  Z magnitude;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  Z exponent;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  bool negative;
};

struct Grid {
  explicit Grid(FpFormat target)
      : format(target),
        tail(target.SignificandWidth() - 1),
        bias(AddWord(PowerOfTwo(target.ExponentWidth() - 1).Get(), -1)),
        minimum(AddWord(bias.Get(), -1)) {
    mpz_neg(minimum.Get(), minimum.Get());
  }

  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  FpFormat format;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint32_t tail;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  Z bias;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  Z minimum;
};

std::uint64_t ShiftCount(mpz_srcptr value) {
  Require(BitLength(value) <= 64, NumericError::kSizeLimit);
  return MagnitudeChunk(value, 0);
}

Dyadic Decode(const Grid& grid, const Fp& value, bool negative) {
  const auto payload_bits = FpAccess::BitLength(value);
  const auto tail = grid.tail;
  const auto chunks = PackedChunks(value);
  const auto fraction_chunk = [&](auto i) {
    return i < Words(tail) ? chunks(i) & Mask(tail - i * 64) : 0;
  };

  if (value.IsSubnormal()) {
    return {ImportChunks(std::min<std::uint64_t>(tail, payload_bits),
                         fraction_chunk),
            AddWord(grid.minimum.Get(), -std::int64_t{tail}), negative};
  }
  const auto exponent_width = grid.format.ExponentWidth();
  const auto exponent_bits =
      std::min<std::uint64_t>(exponent_width, payload_bits - tail);
  const auto exponent = ImportChunks(exponent_bits, [&](auto i) {
    return i < Words(exponent_width)
               ? ReadRight(chunks, i, tail) & Mask(exponent_width - i * 64)
               : 0;
  });
  auto leading = AddZ(exponent.Get(), grid.bias.Get(), true);
  return {
      ImportChunks(tail + std::uint64_t{1},
                   [&](auto i) { return fraction_chunk(i) | Flag(i, tail); }),
      AddWord(leading.Get(), -std::int64_t{tail}), negative};
}

Z Leading(const Dyadic& value) {
  return AddWord(
      value.exponent.Get(),
      static_cast<std::int64_t>(BitLength(value.magnitude.Get()) - 1));
}

Z Quantum(const Grid& grid, mpz_srcptr leading) {
  const auto binade =
      mpz_cmp(leading, grid.minimum.Get()) < 0 ? grid.minimum.Get() : leading;
  return AddWord(binade, -std::int64_t{grid.tail});
}

Fp Overflow(const Grid& grid, RoundingMode mode, bool negative) {
  if (mode == RoundingMode::kRne || mode == RoundingMode::kRna ||
      Outward(mode, negative))
    return Infinity(grid.format, negative);
  const auto sign = grid.format.Width() - 1;
  return FpAccess::Finite(grid.format, grid.format.Width(), [&](auto i) {
    auto word = FieldMask(i, 0, sign) & ~Flag(i, grid.tail);
    return negative ? word | Flag(i, sign) : word;
  });
}

Fp Encode(const Grid& grid, RoundingMode mode, bool negative, Z coefficient,
          mpz_srcptr quantum) {
  if (mpz_sgn(coefficient.Get()) == 0) return Zero(grid.format, negative);
  auto used = BitLength(coefficient.Get());
  Z exponent;
  if (used > grid.tail) {
    const bool carry = used > std::uint64_t{grid.tail} + 1;
    auto leading = AddWord(quantum, std::int64_t{grid.tail} + carry);
    if (mpz_cmp(leading.Get(), grid.bias.Get()) > 0)
      return Overflow(grid, mode, negative);
    exponent = AddZ(leading.Get(), grid.bias.Get());
    if (carry) coefficient = ShiftRight(coefficient.Get(), 1);
    used = grid.tail;
  }
  const auto sign = grid.format.Width() - 1;
  const auto e_bits = BitLength(exponent.Get());
  const auto bits = negative      ? grid.format.Width()
                    : e_bits != 0 ? e_bits + grid.tail
                                  : used;
  return FpAccess::Finite(grid.format, bits, [&](auto i) {
    auto word =
        MagnitudeChunk(coefficient.Get(), i) & FieldMask(i, 0, grid.tail);
    if (e_bits != 0) {
      word |=
          ReadLeft([&](auto j) { return MagnitudeChunk(exponent.Get(), j); }, i,
                   grid.tail);
    }
    return negative ? word | Flag(i, sign) : word;
  });
}

Z RoundCoefficient(mpz_srcptr numerator, mpz_srcptr denominator,
                   mpz_srcptr scale, mpz_srcptr leading, mpz_srcptr quantum,
                   std::int64_t ratio_floor, RoundingMode mode, bool negative) {
  Z coefficient;
  int half = -1;
  bool inexact;
  if (mpz_cmp(leading, quantum) < 0) {
    inexact = true;
    const auto half_quantum = AddWord(quantum, -1);
    if (mpz_cmp(leading, half_quantum.Get()) == 0)
      half = CompareScaled(numerator, denominator, ratio_floor);
  } else {
    const auto delta = AddZ(scale, quantum, true);
    const auto shift = ShiftCount(delta.Get());
    Z scaled;
    if (mpz_sgn(delta.Get()) > 0) {
      scaled = ShiftLeft(numerator, shift);
      numerator = scaled.Get();
    } else if (shift != 0) {
      scaled = ShiftLeft(denominator, shift);
      denominator = scaled.Get();
    }
    auto [q, remainder] = QuotientRemainder(numerator, denominator);
    coefficient = std::move(q);
    inexact = mpz_sgn(remainder.Get()) != 0;
    if (inexact) half = CompareScaled(remainder.Get(), denominator, -1);
  }
  if (Increment(mode, negative, inexact, half,
                mpz_odd_p(coefficient.Get()) != 0))
    coefficient = AddWord(coefficient.Get(), 1);
  return coefficient;
}

Fp Round(const Grid& grid, RoundingMode mode, bool negative, mpz_srcptr n,
         mpz_srcptr d, mpz_srcptr scale) {
  if (mpz_sgn(n) == 0) return Zero(grid.format, negative);
  const auto ratio_floor = FloorLogRatio(n, d);
  const auto leading = AddWord(scale, ratio_floor);
  if (mpz_cmp(leading.Get(), grid.bias.Get()) > 0)
    return Overflow(grid, mode, negative);
  const auto quantum = Quantum(grid, leading.Get());
  auto coefficient = RoundCoefficient(n, d, scale, leading.Get(), quantum.Get(),
                                      ratio_floor, mode, negative);
  return Encode(grid, mode, negative, std::move(coefficient), quantum.Get());
}

Fp Round(const Grid& grid, RoundingMode mode, const Dyadic& value) {
  const ZView one{IntegerView(1)};
  return Round(grid, mode, value.negative, value.magnitude.Get(), one.Get(),
               value.exponent.Get());
}

Fp Sum(const Grid& grid, RoundingMode mode, Dyadic a, Dyadic b) {
  const bool opposite = a.negative != b.negative;
  if (mpz_sgn(a.magnitude.Get()) == 0) {
    if (mpz_sgn(b.magnitude.Get()) == 0)
      return Zero(grid.format,
                  opposite ? mode == RoundingMode::kRtn : a.negative);
    return Round(grid, mode, b);
  }
  if (mpz_sgn(b.magnitude.Get()) == 0) return Round(grid, mode, a);
  auto a_leading = Leading(a), b_leading = Leading(b);
  if (mpz_cmp(a_leading.Get(), b_leading.Get()) < 0) {
    std::swap(a, b);
    std::swap(a_leading, b_leading);
  }
  const auto quantum = Quantum(grid, a_leading.Get());
  const auto boundary_lattice = AddWord(quantum.Get(), -2);
  const auto dominant_lattice =
      mpz_cmp(a.exponent.Get(), boundary_lattice.Get()) < 0
          ? a.exponent.Get()
          : boundary_lattice.Get();
  const auto cutoff = AddWord(dominant_lattice, -1);
  if (mpz_cmp(b_leading.Get(), cutoff.Get()) < 0) {
    // The adjacent rounding boundaries lie on the quarter-quantum lattice,
    // even just below a binade edge. The dominant value is on its own dyadic
    // lattice. Neither the tail nor this proxy can reach a distinct boundary.
    b.magnitude = UnsignedZ(1);
    b.exponent = Z(cutoff.Get());
  }
  auto gap = AddZ(a.exponent.Get(), b.exponent.Get(), true);
  const auto shift = ShiftCount(gap.Get());
  const auto exponent =
      mpz_sgn(gap.Get()) >= 0 ? b.exponent.Get() : a.exponent.Get();
  if (mpz_sgn(gap.Get()) > 0) a.magnitude = ShiftLeft(a.magnitude.Get(), shift);
  if (mpz_sgn(gap.Get()) < 0) b.magnitude = ShiftLeft(b.magnitude.Get(), shift);
  auto result = AddZ(a.magnitude.Get(), b.magnitude.Get(), opposite);
  const int sign = mpz_sgn(result.Get());
  if (sign == 0) return Zero(grid.format, mode == RoundingMode::kRtn);
  if (sign < 0) mpz_neg(result.Get(), result.Get());
  const ZView one{IntegerView(1)};
  return Round(grid, mode, a.negative != (sign < 0), result.Get(), one.Get(),
               exponent);
}

Dyadic Product(Dyadic a, Dyadic b) {
  return {MulZ(a.magnitude.Get(), b.magnitude.Get()),
          AddZ(a.exponent.Get(), b.exponent.Get()), a.negative != b.negative};
}

Fp AddValues(RoundingMode mode, const Fp& a, const Fp& b, bool subtract) {
  const auto format = a.Format();
  const bool a_sign = a.SignBit(), b_sign = b.SignBit() != subtract;
  if (a.IsNaN() || b.IsNaN()) return NaN(format);
  if (a.IsInfinite() && b.IsInfinite() && a_sign != b_sign) return NaN(format);
  if (a.IsInfinite()) return Infinity(format, a_sign);
  if (b.IsInfinite()) return Infinity(format, b_sign);
  if (a.IsZero() && b.IsZero())
    return Zero(format, a_sign == b_sign ? a_sign : mode == RoundingMode::kRtn);
  if (b.IsZero()) return a;
  if (a.IsZero()) return WithSign(b, b_sign);
  if (a_sign != b_sign && CompareFields(a, b) == 0)
    return Zero(format, mode == RoundingMode::kRtn);
  const Grid grid(format);
  return Sum(grid, mode, Decode(grid, a, a_sign), Decode(grid, b, b_sign));
}

Z IntegralMagnitude(const Dyadic& value, mpz_srcptr leading,
                    RoundingMode mode) {
  if (mpz_sgn(value.exponent.Get()) >= 0)
    return ShiftLeft(value.magnitude.Get(), ShiftCount(value.exponent.Get()));
  const ZView one{IntegerView(1)}, zero{IntegerView(0)};
  return RoundCoefficient(
      value.magnitude.Get(), one.Get(), value.exponent.Get(), leading,
      zero.Get(),
      static_cast<std::int64_t>(BitLength(value.magnitude.Get()) - 1), mode,
      value.negative);
}

Fp FromBv(FpFormat destination, RoundingMode mode, const BitVector& value,
          bool signed_input) {
  if (value.EffectiveBitLength() == 0) return Zero(destination, false);
  const ZView d{IntegerView(1)}, zero{IntegerView(0)};
  auto magnitude = Bits::ToZ(value, signed_input);
  const bool negative = mpz_sgn(magnitude.Get()) < 0;
  mpz_abs(magnitude.Get(), magnitude.Get());
  return Round(Grid(destination), mode, negative, magnitude.Get(), d.Get(),
               zero.Get());
}

FpOutcome<BitVector> ToBv(IntegerView width, RoundingMode mode, const Fp& value,
                          bool signed_result) {
  const auto w = Width(width);
  ValidateMode(mode);
  const auto unspecified = FpAccess::Unspecified(w);
  if (value.IsNaN() || value.IsInfinite()) return unspecified;
  if (value.IsZero()) return FpAccess::Defined(Bits::Word(w, 0));
  const Grid grid(value.Format());
  auto source = Decode(grid, value, value.SignBit());
  const auto leading = Leading(source);
  const ZView bound{IntegerView(w - (signed_result && !source.negative))};
  if (mpz_cmp(leading.Get(), bound.Get()) >= 0 ||
      (!signed_result && source.negative && mpz_sgn(leading.Get()) >= 0))
    return unspecified;
  if (signed_result && source.negative && mpz_sgn(source.exponent.Get()) >= 0) {
    const ZView edge{IntegerView(w - 1)};
    if (mpz_cmp(leading.Get(), edge.Get()) == 0 &&
        mpz_popcount(source.magnitude.Get()) != 1)
      return unspecified;
  }
  auto magnitude = IntegralMagnitude(source, leading.Get(), mode);
  const auto used = BitLength(magnitude.Get());
  if (used == 0) return FpAccess::Defined(Bits::Word(w, 0));
  if (used > w ||
      (used == w && signed_result &&
       (!source.negative || mpz_scan1(magnitude.Get(), 0) != w - 1)) ||
      (!signed_result && source.negative))
    return unspecified;
  if (!source.negative)
    return FpAccess::Defined(Bits::FromZ(w, magnitude.Get()));
  mpz_neg(magnitude.Get(), magnitude.Get());
  return FpAccess::Defined(Unwrap(BitVector::FromIntegerModulo(
      IntegerView(w), IntegerAccess::View(magnitude.Get()))));
}

}  // namespace

NumericResult<FpFormat> FpFormat::Make(IntegerView exponent,
                                       IntegerView significand) {
  return Checked([&] {
    const auto e = detail::Width(exponent, 2),
               s = detail::Width(significand, 2);
    PaddedWidth(std::uint64_t{e} + s);
    return FpFormat(e, s);
  });
}

FloatingPointValue::FloatingPointValue(const FloatingPointValue& other)
    : FloatingPointValue(other.Borrow().Copy()) {}

FloatingPointValue& FloatingPointValue::operator=(
    const FloatingPointValue& other) {
  if (this != &other) *this = FloatingPointValue(other);
  return *this;
}

FloatingPointValue& FloatingPointValue::operator=(
    FloatingPointValue&& other) noexcept {
  if (this != &other) {
    if (limb_count_ != 0) delete[] limbs_;
    MoveFrom(other);
  }
  return *this;
}

FloatingPointView FloatingPointValue::Borrow() const& noexcept {
  if (category_ != FpCategory::kFiniteBits) {
    return FloatingPointView::FromCanonicalSpecial(
        format_, static_cast<FpSpecial>(category_));
  }
  const auto width = format_.Width();
  const auto bits =
      limb_count_ == 0
          ? BitVectorView::FromCanonicalWord(width, word_)
          : BitVectorView::FromCanonicalLimbs(width, {limbs_, limb_count_});
  return FloatingPointView::FromCanonicalBits(format_, bits);
}

FloatingPointView FloatingPointView::FromCanonicalSpecial(
    FpFormat format, FpSpecial special) noexcept {
  return FloatingPointView(format, static_cast<FpCategory>(special),
                           std::nullopt);
}

FloatingPointView FloatingPointView::FromCanonicalBits(
    FpFormat format, BitVectorView bits) noexcept {
  return FloatingPointView(format, FpCategory::kFiniteBits, bits);
}

FloatingPointValue FloatingPointView::Copy() const {
  if (!bits_) return FpAccess::Special(format_, category_);
  return FpAccess::Finite(format_, bits_->EffectiveBitLength(),
                          [&](auto i) { return bits_->Chunk(i); });
}

bool FloatingPointValue::SignBit() const noexcept {
  if (category_ == FpCategory::kFiniteBits) {
    const auto sign = format_.Width() - 1;
    return (FpAccess::Chunk(*this, sign / 64) & Flag(sign / 64, sign)) != 0;
  }
  return category_ == FpCategory::kNegativeZero ||
         category_ == FpCategory::kNegativeInfinity;
}

bool FloatingPointValue::IsZero() const noexcept {
  return category_ == FpCategory::kPositiveZero ||
         category_ == FpCategory::kNegativeZero;
}

bool FloatingPointValue::IsInfinite() const noexcept {
  return category_ == FpCategory::kPositiveInfinity ||
         category_ == FpCategory::kNegativeInfinity;
}

bool FloatingPointValue::IsNormal() const noexcept {
  return category_ == FpCategory::kFiniteBits && !IsSubnormal();
}

bool FloatingPointValue::IsSubnormal() const noexcept {
  return category_ == FpCategory::kFiniteBits &&
         IsFieldFilled(FpAccess::BitLength(*this),
                       format_.SignificandWidth() - 1, format_.ExponentWidth(),
                       PackedChunks(*this), false);
}

std::uint64_t FloatingPointValue::Hash() const noexcept {
  auto hash = Mix(Mix(format_.ExponentWidth(), format_.SignificandWidth()),
                  std::to_underlying(category_));
  const auto count = Words(FpAccess::BitLength(*this));
  for (std::uint64_t i = 0; i < count; ++i)
    hash = Mix(hash, FpAccess::Chunk(*this, i));
  return hash;
}

std::size_t FloatingPointValue::BackingBytes() const noexcept {
  return limb_count_ * sizeof(word_);
}

bool FloatingPointValue::operator==(
    const FloatingPointValue& other) const noexcept {
  if (format_ != other.format_ || category_ != other.category_ ||
      limb_count_ != other.limb_count_)
    return false;
  if (limb_count_ == 0) return word_ == other.word_;
  return std::equal(limbs_, limbs_ + limb_count_, other.limbs_);
}

NumericResult<Fp> FloatingPointValue::Special(FpFormat format,
                                              FpCategory category) {
  if (category >= FpCategory::kFiniteBits)
    return std::unexpected(NumericError::kInvalidArgument);
  return FpAccess::Special(format, category);
}

NumericResult<Fp> FloatingPointValue::FromBits(FpFormat format,
                                               const BitVector& bits) {
  if (bits.Width() != format.Width()) [[unlikely]]
    return std::unexpected(NumericError::kInvalidArgument);
  return FromPackedBits(format, Bits::Limbs(bits));
}

NumericResult<Fp> FloatingPointValue::FromBits(FpFormat format,
                                               IntegerView bits) {
  if (const auto word = IntegerAccess::TryUnsigned(bits)) {
    if (WordBits(*word) > format.Width()) [[unlikely]]
      return std::unexpected(NumericError::kValueOutOfRange);
    return FromPackedBits(format, {&*word, *word == 0 ? 0u : 1u});
  }
  if (bits.Sign() < 0 || bits.BitLength() > format.Width()) [[unlikely]]
    return std::unexpected(NumericError::kValueOutOfRange);
  return FromPackedBits(format, bits.MagnitudeLimbs());
}

NumericResult<Fp> FloatingPointValue::FromFields(const BitVector& sign,
                                                 const BitVector& exponent,
                                                 const BitVector& fraction) {
  return Checked([&] {
    Require(sign.Width() == 1, NumericError::kInvalidArgument);
    const auto s = PaddedWidth(std::uint64_t{fraction.Width()} + 1);
    const auto format =
        Unwrap(FpFormat::Make(IntegerView(exponent.Width()), IntegerView(s)));
    const auto tail = s - 1;
    const auto sign_bit = format.Width() - 1;
    const auto exponent_bits = exponent.EffectiveBitLength();
    const bool negative = sign.TestBit(0);
    const auto all_ones = IsFieldFilled(
        exponent_bits, 0, exponent.Width(),
        [&](auto i) { return exponent.Chunk(i); }, true);
    const auto fraction_bits = fraction.EffectiveBitLength();
    if (all_ones)
      return fraction_bits == 0 ? Infinity(format, negative) : NaN(format);
    if (exponent_bits == 0 && fraction_bits == 0) return Zero(format, negative);
    const auto bits = negative             ? format.Width()
                      : exponent_bits == 0 ? fraction_bits
                                           : tail + exponent_bits;
    return FpAccess::Finite(format, bits, [&](auto i) {
      const auto magnitude =
          fraction.Chunk(i) |
          ReadLeft([&](auto j) { return exponent.Chunk(j); }, i, tail);
      return negative ? magnitude | Flag(i, sign_bit) : magnitude;
    });
  });
}

NumericResult<BitVector> FloatingPointValue::ToBits() const {
  return Checked([&] {
    const auto width = format_.Width();
    const auto sign = width - 1;
    if (category_ == FpCategory::kFiniteBits)
      return Bits::Build(width, FpAccess::BitLength(*this),
                         PackedChunks(*this));
    const auto tail = format_.SignificandWidth() - 1;
    const auto bits = SignBit() ? width : (IsNaN() || IsInfinite() ? sign : 0);
    return Bits::Build(width, bits, [&](auto i) {
      const auto sign_bits = SignBit() ? Flag(i, sign) : 0;
      const auto exponent =
          IsNaN() || IsInfinite() ? FieldMask(i, tail, sign) : 0;
      return sign_bits | exponent | (i == 0 && IsNaN());
    });
  });
}

NumericResult<BitVector> FloatingPointValue::ExponentBits() const {
  return Checked([&] {
    return Field(*this, format_.SignificandWidth() - 1,
                 format_.ExponentWidth());
  });
}

NumericResult<BitVector> FloatingPointValue::FractionBits() const {
  return Checked(
      [&] { return Field(*this, 0, format_.SignificandWidth() - 1); });
}

NumericResult<Rational> FloatingPointValue::ToRational() const {
  return Checked([&] {
    Require(!IsNaN() && !IsInfinite(), NumericError::kInvalidArgument);
    if (IsZero()) return Rational();
    if (IsSubnormal()) {
      // Even after cancelling every trailing zero, the denominator needs
      // at least 2^(exponent width - 1) bits.
      const auto power = format_.ExponentWidth() - 1;
      Require(power < 64 && (std::uint64_t{1} << power) <=
                                GmpLimbCapacity() * GMP_NUMB_BITS,
              NumericError::kSizeLimit);
    }
    auto source = Decode(Grid(format_), *this, SignBit());
    const auto shift = ShiftCount(source.exponent.Get());
    const bool integral = mpz_sgn(source.exponent.Get()) >= 0;
    auto denominator = UnsignedZ(1);
    if (integral) {
      source.magnitude = ShiftLeft(source.magnitude.Get(), shift);
    } else {
      const auto cancelled =
          std::min<std::uint64_t>(shift, mpz_scan1(source.magnitude.Get(), 0));
      if (cancelled != 0)
        source.magnitude = ShiftRight(source.magnitude.Get(), cancelled);
      if (shift != cancelled) denominator = PowerOfTwo(shift - cancelled);
    }
    if (source.negative)
      mpz_neg(source.magnitude.Get(), source.magnitude.Get());
    return RationalAccess::Take(std::move(source.magnitude),
                                std::move(denominator));
  });
}

NumericResult<Fp> FloatingPointValue::Abs(const Fp& value) {
  return Checked([&] { return WithSign(value, false); });
}
NumericResult<Fp> FloatingPointValue::Neg(const Fp& value) {
  return Checked([&] { return WithSign(value, !value.SignBit()); });
}

NumericResult<Fp> FloatingPointValue::Add(RoundingMode mode, const Fp& a,
                                          const Fp& b) {
  return Checked([&] {
    ValidateMode(mode);
    SameFormat(a, b);
    return AddValues(mode, a, b, false);
  });
}
NumericResult<Fp> FloatingPointValue::Sub(RoundingMode mode, const Fp& a,
                                          const Fp& b) {
  return Checked([&] {
    ValidateMode(mode);
    SameFormat(a, b);
    return AddValues(mode, a, b, true);
  });
}
NumericResult<Fp> FloatingPointValue::Mul(RoundingMode mode, const Fp& a,
                                          const Fp& b) {
  return Checked([&] {
    ValidateMode(mode);
    SameFormat(a, b);
    const auto format = a.Format();
    const bool negative = a.SignBit() != b.SignBit();
    if (a.IsNaN() || b.IsNaN() || (a.IsZero() && b.IsInfinite()) ||
        (a.IsInfinite() && b.IsZero()))
      return NaN(format);
    if (a.IsInfinite() || b.IsInfinite()) return Infinity(format, negative);
    if (a.IsZero() || b.IsZero()) return Zero(format, negative);
    const Grid grid(format);
    return Round(
        grid, mode,
        Product(Decode(grid, a, a.SignBit()), Decode(grid, b, b.SignBit())));
  });
}
NumericResult<Fp> FloatingPointValue::Div(RoundingMode mode, const Fp& a,
                                          const Fp& b) {
  return Checked([&] {
    ValidateMode(mode);
    SameFormat(a, b);
    const auto format = a.Format();
    const bool negative = a.SignBit() != b.SignBit();
    if (a.IsNaN() || b.IsNaN() || (a.IsZero() && b.IsZero()) ||
        (a.IsInfinite() && b.IsInfinite()))
      return NaN(format);
    if (a.IsInfinite() || b.IsZero()) return Infinity(format, negative);
    if (a.IsZero() || b.IsInfinite()) return Zero(format, negative);
    const Grid grid(format);
    const auto left = Decode(grid, a, a.SignBit()),
               right = Decode(grid, b, b.SignBit());
    const auto exponent = AddZ(left.exponent.Get(), right.exponent.Get(), true);
    return Round(grid, mode, negative, left.magnitude.Get(),
                 right.magnitude.Get(), exponent.Get());
  });
}

NumericResult<Fp> FloatingPointValue::Fma(RoundingMode mode, const Fp& a,
                                          const Fp& b, const Fp& c) {
  return Checked([&] {
    ValidateMode(mode);
    SameFormat(a, b);
    SameFormat(a, c);
    const auto format = a.Format();
    const bool negative = a.SignBit() != b.SignBit();
    if (a.IsNaN() || b.IsNaN() || c.IsNaN() || (a.IsZero() && b.IsInfinite()) ||
        (a.IsInfinite() && b.IsZero()))
      return NaN(format);
    if (a.IsInfinite() || b.IsInfinite())
      return c.IsInfinite() && negative != c.SignBit()
                 ? NaN(format)
                 : Infinity(format, negative);
    if (c.IsInfinite()) return c;
    if (a.IsZero() || b.IsZero()) {
      if (!c.IsZero()) return c;
      return Zero(format, negative == c.SignBit() ? negative
                                                  : mode == RoundingMode::kRtn);
    }
    const Grid grid(format);
    auto product =
        Product(Decode(grid, a, a.SignBit()), Decode(grid, b, b.SignBit()));
    if (c.IsZero()) return Round(grid, mode, product);
    return Sum(grid, mode, std::move(product), Decode(grid, c, c.SignBit()));
  });
}

NumericResult<Fp> FloatingPointValue::Sqrt(RoundingMode mode, const Fp& value) {
  return Checked([&] {
    ValidateMode(mode);
    if (value.IsZero() || value.IsNaN()) return value;
    if (value.SignBit()) return NaN(value.Format());
    if (value.IsInfinite()) return value;
    const Grid grid(value.Format());
    auto source = Decode(grid, value, false);
    auto leading = Leading(source);
    mpz_fdiv_q_2exp(leading.Get(), leading.Get(), 1);
    const auto quantum = Quantum(grid, leading.Get());
    const auto square_quantum = ShiftLeft(quantum.Get(), 1);
    const auto delta = AddZ(source.exponent.Get(), square_quantum.Get(), true);
    const auto magnitude = source.magnitude.Get();
    const auto count = ShiftCount(delta.Get());
    Require(count <= static_cast<std::uint64_t>(INT64_MAX - 2),
            NumericError::kSizeLimit);
    const auto shift = static_cast<std::int64_t>(count) * mpz_sgn(delta.Get());
    const auto scaled =
        shift >= 0 ? ShiftLeft(magnitude, static_cast<std::uint64_t>(shift))
                   : ShiftRight(magnitude, static_cast<std::uint64_t>(-shift));
    Z coefficient;
    CheckGmp((mpz_size(scaled.Get()) + 1) / 2);
    mpz_sqrt(coefficient.Get(), scaled.Get());
    const auto square = MulZ(coefficient.Get(), coefficient.Get());
    const bool inexact = CompareScaled(magnitude, square.Get(), -shift) != 0;
    int half = -1;
    if (inexact && (mode == RoundingMode::kRne || mode == RoundingMode::kRna)) {
      const auto twice = ShiftLeft(coefficient.Get(), 1);
      const auto midpoint = AddWord(twice.Get(), 1);
      const auto midpoint_square = MulZ(midpoint.Get(), midpoint.Get());
      half = CompareScaled(magnitude, midpoint_square.Get(), -shift - 2);
    }
    if (Increment(mode, false, inexact, half,
                  mpz_odd_p(coefficient.Get()) != 0))
      coefficient = AddWord(coefficient.Get(), 1);
    return Encode(grid, mode, false, std::move(coefficient), quantum.Get());
  });
}

NumericResult<Fp> FloatingPointValue::RoundToIntegral(RoundingMode mode,
                                                      const Fp& value) {
  return Checked([&] {
    ValidateMode(mode);
    if (value.category_ != FpCategory::kFiniteBits) return value;
    const Grid grid(value.Format());
    auto source = Decode(grid, value, value.SignBit());
    if (mpz_sgn(source.exponent.Get()) >= 0) return value;
    const auto leading = Leading(source);
    source.magnitude = IntegralMagnitude(source, leading.Get(), mode);
    source.exponent = Z();
    return Round(grid, RoundingMode::kRne, source);
  });
}

NumericResult<Fp> FloatingPointValue::Rem(const Fp& a, const Fp& b) {
  return Checked([&] {
    SameFormat(a, b);
    const auto format = a.Format();
    if (a.IsNaN() || b.IsNaN() || a.IsInfinite() || b.IsZero())
      return NaN(format);
    if (a.IsZero() || b.IsInfinite()) return a;
    const Grid grid(format);
    auto x = Decode(grid, a, a.SignBit()), y = Decode(grid, b, b.SignBit());
    const auto delta = AddZ(x.exponent.Get(), y.exponent.Get(), true);
    auto coefficient = std::move(x.magnitude);
    auto divisor = std::move(y.magnitude);
    Z scale;
    if (mpz_sgn(delta.Get()) >= 0) {
      scale = std::move(y.exponent);
      if (mpz_sgn(delta.Get()) != 0) {
        const auto modulus = ShiftLeft(divisor.Get(), 1);
        const ZView two{IntegerView(2)};
        Z power;
        // mpz_powm returns at most the modulus size and uses native-limb
        // scratch for modular multiplication.
        CheckGmp(mpz_size(modulus.Get()));
        mpz_powm(power.Get(), two.Get(), delta.Get(), modulus.Get());
        const auto product = MulZ(coefficient.Get(), power.Get());
        coefficient = QuotientRemainder(product.Get(), modulus.Get()).second;
      }
    } else {
      const auto ratio_floor = FloorLogRatio(coefficient.Get(), divisor.Get());
      const auto leading = AddWord(delta.Get(), ratio_floor);
      const ZView half{IntegerView(-1)};
      const int order = mpz_cmp(leading.Get(), half.Get());
      if (order < 0 ||
          (order == 0 &&
           CompareScaled(coefficient.Get(), divisor.Get(), ratio_floor) == 0))
        return a;
      divisor = ShiftLeft(divisor.Get(), ShiftCount(delta.Get()));
      scale = std::move(x.exponent);
    }
    auto [quotient, remainder] =
        QuotientRemainder(coefficient.Get(), divisor.Get());
    if (mpz_sgn(remainder.Get()) == 0) return Zero(format, x.negative);
    const auto order = CompareScaled(remainder.Get(), divisor.Get(), -1);
    const bool upward =
        order > 0 || (order == 0 && mpz_odd_p(quotient.Get()) != 0);
    if (upward) remainder = AddZ(divisor.Get(), remainder.Get(), true);
    return Round(
        grid, RoundingMode::kRne,
        Dyadic{std::move(remainder), std::move(scale), x.negative != upward});
  });
}

namespace {

template <class Predicate>
NumericResult<bool> CompareFp(const Fp& a, const Fp& b, Predicate predicate) {
  return Checked([&] {
    SameFormat(a, b);
    return !a.IsNaN() && !b.IsNaN() && predicate(Compare(a, b));
  });
}

NumericResult<FpOutcome<Fp>> Select(const Fp& a, const Fp& b, bool minimum) {
  return Checked([&] {
    SameFormat(a, b);
    if (a.IsNaN()) return FpAccess::Defined(Fp(b));
    if (b.IsNaN()) return FpAccess::Defined(Fp(a));
    if (a.IsZero() && b.IsZero() && a.SignBit() != b.SignBit())
      return FpAccess::Choices(a, b);
    const auto order = Compare(a, b);
    return FpAccess::Defined(minimum ? (order <= 0 ? a : b)
                                     : (order >= 0 ? a : b));
  });
}

}  // namespace

NumericResult<bool> FloatingPointValue::Eq(const Fp& a, const Fp& b) {
  return CompareFp(a, b, [](int order) { return order == 0; });
}
NumericResult<bool> FloatingPointValue::Lt(const Fp& a, const Fp& b) {
  return CompareFp(a, b, [](int order) { return order < 0; });
}
NumericResult<bool> FloatingPointValue::Leq(const Fp& a, const Fp& b) {
  return CompareFp(a, b, [](int order) { return order <= 0; });
}
NumericResult<bool> FloatingPointValue::Gt(const Fp& a, const Fp& b) {
  return CompareFp(a, b, [](int order) { return order > 0; });
}
NumericResult<bool> FloatingPointValue::Geq(const Fp& a, const Fp& b) {
  return CompareFp(a, b, [](int order) { return order >= 0; });
}

NumericResult<FpOutcome<Fp>> FloatingPointValue::Min(const Fp& a, const Fp& b) {
  return Select(a, b, true);
}
NumericResult<FpOutcome<Fp>> FloatingPointValue::Max(const Fp& a, const Fp& b) {
  return Select(a, b, false);
}

NumericResult<Fp> RoundRationalToFp(FpFormat destination, RoundingMode mode,
                                    RationalView value) {
  return Checked([&] {
    ValidateMode(mode);
    if (value.Sign() == 0) return Zero(destination, false);
    const ZView n(value.Numerator()), d(value.Denominator()),
        zero{IntegerView(0)};
    auto magnitude = Z(n.Get());
    mpz_abs(magnitude.Get(), magnitude.Get());
    return Round(Grid(destination), mode, value.Sign() < 0, magnitude.Get(),
                 d.Get(), zero.Get());
  });
}

NumericResult<Fp> FloatingPointValue::Convert(FpFormat destination,
                                              RoundingMode mode,
                                              const Fp& value) {
  return Checked([&] {
    ValidateMode(mode);
    if (destination == value.Format()) return value;
    if (value.category_ != FpCategory::kFiniteBits)
      return FpAccess::Special(destination, value.category_);
    return Round(Grid(destination), mode,
                 Decode(Grid(value.Format()), value, value.SignBit()));
  });
}

NumericResult<Fp> FloatingPointValue::FromSignedBv(FpFormat destination,
                                                   RoundingMode mode,
                                                   const BitVector& value) {
  return Checked([&] {
    ValidateMode(mode);
    return FromBv(destination, mode, value, true);
  });
}
NumericResult<Fp> FloatingPointValue::FromUnsignedBv(FpFormat destination,
                                                     RoundingMode mode,
                                                     const BitVector& value) {
  return Checked([&] {
    ValidateMode(mode);
    return FromBv(destination, mode, value, false);
  });
}
NumericResult<FpOutcome<BitVector>> FloatingPointValue::ToSignedBv(
    IntegerView width, RoundingMode mode, const Fp& value) {
  return Checked([&] { return ToBv(width, mode, value, true); });
}
NumericResult<FpOutcome<BitVector>> FloatingPointValue::ToUnsignedBv(
    IntegerView width, RoundingMode mode, const Fp& value) {
  return Checked([&] { return ToBv(width, mode, value, false); });
}
}  // namespace dandelion::numeric

#ifdef DANDELION_UTIL_TESTING
extern "C++" {
namespace dandelion::numeric {

using namespace detail;

const void* NumericTestPeer::BackingAddress(
    const FloatingPointValue& value) noexcept {
  return FpAccess::Address(value);
}

}  // namespace dandelion::numeric
}  // extern "C++"
#endif

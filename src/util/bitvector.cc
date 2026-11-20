module;
#include <cstdint>
#include <gmp.h>

module dandelion.util.numeric;
import :internal;
import std;

namespace dandelion::numeric::detail {

BitVector BitVectorAccess::Word(std::uint32_t width,
                                std::uint64_t word) noexcept {
  BitVector result;
  result.width_ = width;
  result.word_ = word & Mask(width);
  return result;
}

BitVector BitVectorAccess::Take(std::uint32_t width,
                                std::unique_ptr<std::uint64_t[]> limbs,
                                std::uint64_t count) noexcept {
  BitVector result;
  result.width_ = width;
  result.limb_count_ = static_cast<std::uint32_t>(count);
  result.limbs_ = limbs.release();
  return result;
}

const void* BitVectorAccess::Address(const BitVector& value) noexcept {
  return value.limb_count_ == 0 ? nullptr : value.limbs_;
}

BitVector BitVectorAccess::FromZ(std::uint32_t width, mpz_srcptr value) {
  return Build(width, BitLength(value),
               [value](auto i) { return MagnitudeChunk(value, i); });
}

}  // namespace dandelion::numeric::detail

namespace dandelion::numeric {
namespace {

using namespace detail;
using Access = BitVectorAccess;

bool Negative(const BitVector& value) noexcept {
  return value.TestBit(value.Width() - 1);
}

void SameWidth(const BitVector& a, const BitVector& b) {
  Require(a.Width() == b.Width(), NumericError::kInvalidArgument);
}

BitVector Ones(std::uint32_t width) {
  return Access::Build(width, width, [](auto) { return UINT64_MAX; });
}

BitVector FromDoubleWord(std::uint32_t width, Uint128 word) {
  if (width <= 64 || word <= UINT64_MAX)
    return Access::Word(width, static_cast<std::uint64_t>(word));
  return Access::Build(width, 128, [word](auto i) {
    return static_cast<std::uint64_t>(word >> (64 * i));
  });
}

Z UnsignedBitsZ(const BitVector& value) {
  return ImportChunks(value.EffectiveBitLength(),
                      [&](auto i) { return value.Chunk(i); });
}

Int128 SignedWordValue(std::uint64_t word, std::uint32_t width) noexcept {
  if (width > 64 || (word & (std::uint64_t{1} << (width - 1))) == 0)
    return Int128{word};
  return Int128{word} - (Int128{1} << width);
}

int UnsignedCompare(const BitVector& a, const BitVector& b) noexcept {
  for (auto i = Words(std::max(a.EffectiveBitLength(), b.EffectiveBitLength()));
       i-- != 0;) {
    const auto x = a.Chunk(i), y = b.Chunk(i);
    if (x != y) return x < y ? -1 : 1;
  }
  return 0;
}

int SignedCompare(const BitVector& a, const BitVector& b) noexcept {
  if (Negative(a) != Negative(b)) return Negative(a) ? -1 : 1;
  return UnsignedCompare(a, b);
}

template <class Kernel>
BitVector Sequential(std::uint32_t width, std::uint64_t bits, Kernel initial) {
  const auto count = Words(std::min<std::uint64_t>(width, bits));
  auto kernel = initial;
  std::uint64_t used = 0, word = 0;
  for (std::uint64_t i = 0; i < count; ++i) {
    const auto chunk = kernel(i) & Mask(width - i * 64);
    if (i == 0) word = chunk;
    if (chunk != 0) used = i + 1;
  }
  if (used <= 1) return Access::Word(width, word);
  auto limbs = NewPacked(used);
  auto write = initial;
  for (std::uint64_t i = 0; i < used; ++i)
    limbs[i] = write(i) & Mask(width - i * 64);
  return Access::Take(width, std::move(limbs), used);
}

BitVector Negated(const BitVector& value) {
  if (auto word = value.TryUint64();
      word && (value.Width() <= 64 || *word == 0))
    return Access::Word(value.Width(), std::uint64_t{0} - *word);
  return Sequential(value.Width(), value.Width(),
                    [&, borrow = false](auto i) mutable {
                      const auto chunk = value.Chunk(i);
                      const auto result = std::uint64_t{0} - chunk - borrow;
                      borrow = borrow || chunk != 0;
                      return result;
                    });
}

BitVector AddSub(const BitVector& a, const BitVector& b, bool subtract) {
  const auto bits = std::max(a.EffectiveBitLength(), b.EffectiveBitLength());
  if (bits <= 64) {
    const Uint128 x = a.Chunk(0), y = b.Chunk(0);
    if (!subtract) return FromDoubleWord(a.Width(), x + y);
    if (x >= y || a.Width() <= 128) return FromDoubleWord(a.Width(), x - y);
  }
  const std::uint64_t bound = subtract && UnsignedCompare(a, b) < 0
                                  ? a.Width()
                                  : std::uint64_t{bits} + 1;
  return Sequential(a.Width(), bound, [&, carry = false](auto i) mutable {
    const auto x = a.Chunk(i), y = b.Chunk(i);
    std::uint64_t word;
    const bool overflow = subtract ? __builtin_sub_overflow(x, y, &word)
                                   : __builtin_add_overflow(x, y, &word);
    const bool carry_overflow =
        subtract ? __builtin_sub_overflow(word, carry, &word)
                 : __builtin_add_overflow(word, carry, &word);
    carry = overflow || carry_overflow;
    return word;
  });
}

template <class Limb>
bool UseDirect() noexcept {
#ifdef DANDELION_UTIL_TESTING
  if (faults.portable_bv) return false;
#endif
  return std::is_same_v<Limb, mp_limb_t> && GMP_NUMB_BITS == 64;
}

template <class Limb>
BitVector Product(const BitVector& a, const BitVector& b,
                  std::span<const Limb> x, std::span<const Limb> y) {
  if constexpr (std::is_same_v<Limb, mp_limb_t> && GMP_NUMB_BITS == 64) {
    if (UseDirect<Limb>()) {
      if (x.size() < y.size()) std::swap(x, y);
      const auto count = x.size() + y.size();
      CheckGmp(count);
      auto product = Scratch<Limb>(count);
      mpn_mul(product.get(), x.data(), static_cast<mp_size_t>(x.size()),
              y.data(), static_cast<mp_size_t>(y.size()));
      return Access::Build(a.Width(), 64 * count,
                           [&](auto i) { return product[i]; });
    }
  }
  const auto left = UnsignedBitsZ(a), right = UnsignedBitsZ(b);
  const auto product = MulZ(left.Get(), right.Get());
  return Access::FromZ(a.Width(), product.Get());
}

template <class Limb>
BitVector QuotientOrRemainder(const BitVector& a, const BitVector& b,
                              std::span<const Limb> x, std::span<const Limb> y,
                              bool remainder) {
  if constexpr (std::is_same_v<Limb, mp_limb_t> && GMP_NUMB_BITS == 64) {
    if (UseDirect<Limb>()) {
      const auto q_count = x.size() - y.size() + 1;
      CheckGmp(std::max(q_count, y.size()));
      auto q = Scratch<Limb>(q_count);
      auto r = Scratch<Limb>(y.size());
      mpn_tdiv_qr(q.get(), r.get(), 0, x.data(),
                  static_cast<mp_size_t>(x.size()), y.data(),
                  static_cast<mp_size_t>(y.size()));
      return Access::Build(a.Width(), 64 * (remainder ? y.size() : q_count),
                           [&](auto i) { return remainder ? r[i] : q[i]; });
    }
  }
  const auto left = UnsignedBitsZ(a), right = UnsignedBitsZ(b);
  const auto [q, r] = QuotientRemainder(left.Get(), right.Get());
  return Access::FromZ(a.Width(), remainder ? r.Get() : q.Get());
}

BitVector DivideUnsigned(const BitVector& a, const BitVector& b,
                         bool remainder) {
  if (b.EffectiveBitLength() == 0)
    return remainder ? BitVector(a) : Ones(a.Width());
  if (a.EffectiveBitLength() < b.EffectiveBitLength())
    return remainder ? BitVector(a) : Access::Word(a.Width(), 0);
  if (auto x = a.TryUint64()) {
    const auto y = b.Chunk(0);
    return Access::Word(a.Width(), remainder ? *x % y : *x / y);
  }
  const auto dividend = Access::Limbs(a), divisor = Access::Limbs(b);
  if (dividend.data() == divisor.data())
    return Access::Word(a.Width(), remainder ? 0 : 1);
  return QuotientOrRemainder(a, b, dividend, divisor, remainder);
}

enum class Division { kQuotient, kRemainder, kModulus };

BitVector DivideSigned(const BitVector& a, const BitVector& b, Division op) {
  const auto width = a.Width();
  const bool a_negative = Negative(a), b_negative = Negative(b);
  if (b.EffectiveBitLength() == 0) {
    if (op != Division::kQuotient) return a;
    return a_negative ? Access::Word(width, 1) : Ones(width);
  }
  if (const auto x = a.TryUint64(), y = b.TryUint64(); x && y) {
    const auto left = SignedWordValue(*x, width),
               right = SignedWordValue(*y, width);
    if (op == Division::kQuotient)
      return Access::Word(width, static_cast<std::uint64_t>(left / right));
    auto remainder = left % right;
    if (op == Division::kModulus && remainder != 0 && a_negative != b_negative)
      remainder += right;
    return Access::Word(width, static_cast<std::uint64_t>(remainder));
  }
  if (!a_negative && !b_negative)
    return DivideUnsigned(a, b, op != Division::kQuotient);
  const auto a_magnitude = a_negative ? Negated(a) : BitVector(a);
  const auto b_magnitude = b_negative ? Negated(b) : BitVector(b);
  auto result =
      DivideUnsigned(a_magnitude, b_magnitude, op != Division::kQuotient);
  if (op == Division::kQuotient)
    return a_negative != b_negative ? Negated(result) : std::move(result);
  if (op == Division::kModulus && result.EffectiveBitLength() != 0 &&
      a_negative != b_negative)
    return b_negative ? AddSub(result, b, false) : AddSub(b, result, true);
  return a_negative ? Negated(result) : std::move(result);
}

std::uint64_t LeftChunk(const BitVector& value, std::uint64_t index,
                        std::uint64_t shift) noexcept {
  const auto offset = shift / 64, slice = shift % 64;
  if (index < offset) return 0;
  auto word = value.Chunk(index - offset) << slice;
  if (slice != 0 && index > offset)
    word |= value.Chunk(index - offset - 1) >> (64 - slice);
  return word;
}

std::uint64_t RightChunk(const BitVector& value, std::uint64_t index,
                         std::uint64_t shift) noexcept {
  const auto offset = shift / 64, slice = shift % 64;
  auto word = value.Chunk(index + offset) >> slice;
  if (slice != 0) word |= value.Chunk(index + offset + 1) << (64 - slice);
  return word;
}

std::uint64_t LowBitLength(const BitVector& value,
                           std::uint64_t bits) noexcept {
  const auto effective = value.EffectiveBitLength();
  if (effective <= bits) return effective;
  for (auto i = Words(bits); i-- != 0;) {
    const auto word = value.Chunk(i) & Mask(bits - 64 * i);
    if (word != 0) return 64 * i + WordBits(word);
  }
  return 0;
}

std::uint64_t SignFill(std::uint64_t index, std::uint64_t first) noexcept {
  if (index < first / 64) return 0;
  if (index > first / 64) return UINT64_MAX;
  return UINT64_MAX << (first % 64);
}

enum class Shift { kLeft, kRight, kArithmetic };

BitVector Shifted(const BitVector& a, const BitVector& b, Shift op) {
  const auto width = a.Width();
  const auto shift = b.TryUint64().value_or(UINT64_MAX);
  const bool sign_fill = op == Shift::kArithmetic && Negative(a);
  if (shift >= width) return sign_fill ? Ones(width) : Access::Word(width, 0);
  if (shift == 0) return a;
  if (op == Shift::kLeft) {
    const auto bits = LowBitLength(a, width - shift);
    if (bits == 0) return Access::Word(width, 0);
    return Access::Build(width, bits + shift,
                         [&](auto i) { return LeftChunk(a, i, shift); });
  }
  const auto bits = a.EffectiveBitLength();
  const auto bound =
      sign_fill ? width : bits - std::min<std::uint64_t>(bits, shift);
  return Access::Build(width, bound, [&](auto i) {
    const auto word = RightChunk(a, i, shift);
    return sign_fill ? word | SignFill(i, width - shift) : word;
  });
}

BitVector Join(const BitVector& a, const BitVector& b, std::uint32_t width) {
  const auto a_bits = a.EffectiveBitLength();
  const auto bits =
      a_bits == 0 ? b.EffectiveBitLength() : std::uint64_t{a_bits} + b.Width();
  return Access::Build(width, bits, [&](auto i) {
    return LeftChunk(a, i, b.Width()) | b.Chunk(i);
  });
}

enum class Logic { kAnd, kOr, kXor, kNand, kNor, kXnor };

BitVector Bitwise(const BitVector& a, const BitVector& b, Logic op) {
  const bool invert = op >= Logic::kNand;
  const auto bound =
      invert ? a.Width()
      : op == Logic::kAnd
          ? std::min(a.EffectiveBitLength(), b.EffectiveBitLength())
          : std::max(a.EffectiveBitLength(), b.EffectiveBitLength());
  return Access::Build(a.Width(), bound, [&](auto i) {
    const auto x = a.Chunk(i), y = b.Chunk(i);
    std::uint64_t word;
    switch (op) {
      case Logic::kAnd:
      case Logic::kNand:
        word = x & y;
        break;
      case Logic::kOr:
      case Logic::kNor:
        word = x | y;
        break;
      case Logic::kXor:
      case Logic::kXnor:
        word = x ^ y;
        break;
      default:
        std::unreachable();
    }
    return invert ? ~word : word;
  });
}

Z SignedBitsZ(const BitVector& value) {
  if (!Negative(value)) return UnsignedBitsZ(value);
  std::uint64_t first = 0;
  while (value.Chunk(first) == 0) ++first;
  const auto chunk = [&](std::uint64_t i) {
    if (i < first || i >= Words(value.Width())) return std::uint64_t{0};
    const auto magnitude =
        i == first ? std::uint64_t{0} - value.Chunk(i) : ~value.Chunk(i);
    return magnitude & Mask(value.Width() - 64 * i);
  };
  auto last = Words(value.Width()) - 1;
  while (chunk(last) == 0) --last;
  auto magnitude = ImportChunks(64 * last + WordBits(chunk(last)), chunk);
  mpz_neg(magnitude.Get(), magnitude.Get());
  return magnitude;
}

enum class Arithmetic { kAdd, kSub, kMul };

bool SignedOverflow(const BitVector& a, const BitVector& b, Arithmetic op) {
  const auto width = a.Width();
  if (const auto x = a.TryUint64(), y = b.TryUint64(); x && y) {
    if (width > 64) {
      if (op == Arithmetic::kMul)
        return width <= 128 && (Uint128{*x} * *y >> (width - 1)) != 0;
      return width == 65 && op == Arithmetic::kAdd &&
             Uint128{*x} + *y > UINT64_MAX;
    }
    const auto left = SignedWordValue(*x, width),
               right = SignedWordValue(*y, width);
    const auto result = op == Arithmetic::kAdd   ? left + right
                        : op == Arithmetic::kSub ? left - right
                                                 : left * right;
    const auto edge = Int128{1} << (width - 1);
    return result < -edge || result >= edge;
  }
  const auto x = SignedBitsZ(a), y = SignedBitsZ(b);
  const auto result = op == Arithmetic::kMul
                          ? MulZ(x.Get(), y.Get())
                          : AddZ(x.Get(), y.Get(), op == Arithmetic::kSub);
  const auto bits = BitLength(result.Get());
  return bits > width ||
         (bits == width && (mpz_sgn(result.Get()) >= 0 ||
                            mpz_scan1(result.Get(), 0) != width - 1));
}

bool UnsignedOverflow(const BitVector& a, const BitVector& b, Arithmetic op) {
  if (op == Arithmetic::kSub) return UnsignedCompare(a, b) < 0;
  const auto width = a.Width();
  if (const auto x = a.TryUint64(), y = b.TryUint64(); x && y) {
    if (width >= 128) return false;
    const auto word =
        op == Arithmetic::kAdd ? Uint128{*x} + *y : Uint128{*x} * *y;
    return (word >> width) != 0;
  }
  const auto left = a.EffectiveBitLength(), right = b.EffectiveBitLength();
  if (op == Arithmetic::kMul) {
    if (left == 0 || right == 0) return false;
    const auto upper = std::uint64_t{left} + right;
    if (upper <= width) return false;
    if (upper - 1 > width) return true;
  } else if (std::max(left, right) < width) {
    return false;
  }
  const auto x = UnsignedBitsZ(a), y = UnsignedBitsZ(b);
  const auto result =
      op == Arithmetic::kAdd ? AddZ(x.Get(), y.Get()) : MulZ(x.Get(), y.Get());
  return BitLength(result.Get()) > width;
}

BitVector ParseBits(std::string_view digits, int radix) {
  ValidateDigits(digits, radix);
  const unsigned step = radix == 16 ? 4 : 1;
  Require(digits.size() <= UINT32_MAX / step, NumericError::kSizeLimit);
  const auto width = static_cast<std::uint32_t>(digits.size() * step);
  const auto first = digits.find_first_not_of('0');
  if (first == std::string_view::npos) return Access::Word(width, 0);
  digits.remove_prefix(first);
  return Access::Build(width, digits.size() * step, [&](auto i) {
    std::uint64_t chunk = 0;
    const auto base = i * (64 / step);
    for (unsigned shift = 0; shift < 64 && base + shift / step < digits.size();
         shift += step)
      chunk |= static_cast<std::uint64_t>(
                   Digit(digits[digits.size() - 1 - base - shift / step]))
               << shift;
    return chunk;
  });
}

BitVector Rotate(const BitVector& value, IntegerView count, bool left) {
  Require(count.Sign() >= 0, NumericError::kInvalidArgument);
  const auto width = value.Width();
  const auto word = count.TryUint64();
  const auto amount =
      word ? *word % width : mpz_fdiv_ui(ZView(count).Get(), width);
  if (amount == 0 || value.EffectiveBitLength() == 0) return value;
  const auto shift = left ? amount : width - amount;
  const auto right_shift = width - shift;
  const auto left_bits = LowBitLength(value, right_shift);
  const auto effective = value.EffectiveBitLength();
  const auto right_bits =
      effective - std::min<std::uint64_t>(effective, right_shift);
  const auto bound =
      std::max(left_bits == 0 ? 0 : left_bits + shift, right_bits);
  return Access::Build(width, bound, [&](auto i) {
    return LeftChunk(value, i, shift) | RightChunk(value, i, right_shift);
  });
}

}  // namespace

detail::Z detail::BitVectorAccess::ToZ(const BitVector& value,
                                       bool signed_input) {
  return signed_input ? SignedBitsZ(value) : UnsignedBitsZ(value);
}

BitVector::BitVector(const BitVector& other)
    : BitVector(other.Borrow().Copy()) {}
BitVector& BitVector::operator=(const BitVector& other) {
  if (this != &other) *this = BitVector(other);
  return *this;
}
BitVector& BitVector::operator=(BitVector&& other) noexcept {
  if (this != &other) {
    if (limb_count_ != 0) delete[] limbs_;
    MoveFrom(other);
  }
  return *this;
}
BitVectorView BitVector::Borrow() const& noexcept {
  if (limb_count_ == 0) return BitVectorView::FromCanonicalWord(width_, word_);
  return BitVectorView::FromCanonicalLimbs(width_, {limbs_, limb_count_});
}

BitVectorView BitVectorView::FromCanonicalWord(std::uint32_t width,
                                               std::uint64_t word) noexcept {
  BitVectorView result(width);
  result.word_ = word;
  return result;
}

BitVectorView BitVectorView::FromCanonicalLimbs(
    std::uint32_t width, std::span<const std::uint64_t> limbs) noexcept {
  BitVectorView result(width);
  result.limb_count_ = static_cast<std::uint32_t>(limbs.size());
  result.limbs_ = limbs.data();
  return result;
}

std::uint32_t BitVectorView::EffectiveBitLength() const noexcept {
  if (limb_count_ == 0) return static_cast<std::uint32_t>(WordBits(word_));
  return (limb_count_ - 1) * 64 +
         static_cast<std::uint32_t>(WordBits(limbs_[limb_count_ - 1]));
}

std::uint64_t BitVectorView::Chunk(std::uint64_t index) const noexcept {
  if (limb_count_ == 0) return index == 0 ? word_ : 0;
  return index < limb_count_ ? limbs_[index] : 0;
}

std::optional<std::uint64_t> BitVectorView::TryUint64() const noexcept {
  if (limb_count_ != 0) return {};
  return word_;
}

std::span<const std::uint64_t> BitVectorView::Limbs() const noexcept {
  if (limb_count_ == 0) return {};
  return {limbs_, limb_count_};
}

std::uint64_t BitVectorView::Hash() const noexcept {
  auto hash = Mix(0, width_);
  if (limb_count_ == 0) return word_ == 0 ? hash : Mix(hash, word_);
  for (const auto limb : Limbs()) hash = Mix(hash, limb);
  return hash;
}

bool BitVectorView::operator==(BitVectorView other) const noexcept {
  if (width_ != other.width_ || limb_count_ != other.limb_count_) return false;
  return limb_count_ == 0 ? word_ == other.word_
                          : std::ranges::equal(Limbs(), other.Limbs());
}

BitVector BitVectorView::Copy() const {
  if (limb_count_ == 0) return Access::Word(width_, word_);
  auto limbs = NewPacked(limb_count_);
  std::ranges::copy(Limbs(), limbs.get());
  return Access::Take(width_, std::move(limbs), limb_count_);
}

std::uint32_t BitVector::EffectiveBitLength() const noexcept {
  if (limb_count_ == 0) return static_cast<std::uint32_t>(WordBits(word_));
  return (limb_count_ - 1) * 64 +
         static_cast<std::uint32_t>(WordBits(limbs_[limb_count_ - 1]));
}

std::uint64_t BitVector::Chunk(std::uint64_t index) const noexcept {
  if (limb_count_ == 0) return index == 0 ? word_ : 0;
  return index < limb_count_ ? limbs_[index] : 0;
}

bool BitVector::TestBit(std::uint64_t index) const noexcept {
  return (Chunk(index / 64) & (std::uint64_t{1} << (index % 64))) != 0;
}

std::optional<std::uint64_t> BitVector::TryUint64() const noexcept {
  if (limb_count_ != 0) return {};
  return word_;
}

std::size_t BitVector::BackingBytes() const noexcept {
  return limb_count_ * sizeof(word_);
}

std::uint64_t BitVector::Hash() const noexcept { return Borrow().Hash(); }

bool BitVector::operator==(const BitVector& other) const noexcept {
  return width_ == other.width_ &&
         std::ranges::equal(Access::Limbs(*this), Access::Limbs(other));
}

NumericResult<std::string> BitVector::ToBinary() const {
  return Checked([&] {
    std::string text;
    CheckBytes(std::uint64_t{width_} + 1, sizeof(char));
    if (width_ > text.capacity()) AllocationCheckpoint();
    text.resize(width_, '0');
    const auto bits = EffectiveBitLength();
    for (std::uint32_t i = 0; i < bits; ++i)
      if (TestBit(i)) text[width_ - 1 - i] = '1';
    return text;
  });
}

NumericResult<BitVector> BitVector::Zero(IntegerView width) {
  return Checked([&] { return Access::Word(detail::Width(width), 0); });
}
NumericResult<BitVector> BitVector::AllOnes(IntegerView width) {
  return Checked([&] { return Ones(detail::Width(width)); });
}
NumericResult<BitVector> BitVector::FromUint64(IntegerView width,
                                               std::uint64_t value) {
  const auto w = CheckedWidth(width);
  if (!w) [[unlikely]]
    return std::unexpected(w.error());
  if (*w < 64 && value >> *w != 0) [[unlikely]]
    return std::unexpected(NumericError::kValueOutOfRange);
  NumericResult<BitVector> result(std::in_place);
  result->width_ = *w;
  result->word_ = value;
  return result;
}
NumericResult<BitVector> BitVector::FromIntegerExact(IntegerView width,
                                                     IntegerView value) {
  return Checked([&] {
    const auto w = detail::Width(width);
    Require(value.Sign() >= 0 && value.BitLength() <= w,
            NumericError::kValueOutOfRange);
    return Access::Build(w, value.BitLength(),
                         [&](auto i) { return value.MagnitudeChunk(i); });
  });
}
NumericResult<BitVector> BitVector::FromIntegerModulo(IntegerView width,
                                                      IntegerView value) {
  return Checked([&] {
    const auto w = detail::Width(width);
    if (value.Sign() >= 0)
      return Access::Build(w, value.BitLength(),
                           [&](auto i) { return value.MagnitudeChunk(i); });
    return Sequential(w, w, [&, borrow = false](auto i) mutable {
      const auto chunk = value.MagnitudeChunk(i);
      const auto result = std::uint64_t{0} - chunk - borrow;
      borrow = borrow || chunk != 0;
      return result;
    });
  });
}
NumericResult<BitVector> BitVector::FromBinaryDigits(std::string_view digits) {
  return Checked([&] { return ParseBits(digits, 2); });
}
NumericResult<BitVector> BitVector::FromHexDigits(std::string_view digits) {
  return Checked([&] { return ParseBits(digits, 16); });
}
NumericResult<BitVector> BitVector::FromDecimalModulo(IntegerView width,
                                                      std::string_view digits) {
  return Checked([&] {
    const auto w = detail::Width(width);
    ValidateDigits(digits, 10);
    std::uint64_t word = 0;
    std::size_t pos = 0;
    if (w <= 64) {
      for (const char ch : digits)
        word = word * 10 + static_cast<unsigned>(Digit(ch));
      return Access::Word(w, word);
    }
    for (; pos < digits.size(); ++pos) {
      const auto next =
          Uint128{word} * 10 + static_cast<unsigned>(Digit(digits[pos]));
      if (next > UINT64_MAX) break;
      word = static_cast<std::uint64_t>(next);
    }
    if (pos == digits.size()) return Access::Word(w, word);
    auto value = UnsignedZ(word);
    while (pos < digits.size()) {
      const auto end = pos + std::min<std::size_t>(19, digits.size() - pos);
      std::uint64_t factor = 1, chunk = 0;
      for (; pos < end; ++pos) {
        factor *= 10;
        chunk = chunk * 10 + static_cast<unsigned>(Digit(digits[pos]));
      }
      const ZView scale(IntegerView::FromUnsigned(factor));
      const ZView tail(IntegerView::FromUnsigned(chunk));
      value = MulZ(value.Get(), scale.Get());
      value = AddZ(value.Get(), tail.Get());
      if (BitLength(value.Get()) > w) value = LowBits(value.Get(), w);
    }
    return Access::FromZ(w, value.Get());
  });
}
NumericResult<Integer> BitVector::ToUnsignedInteger(const BitVector& value) {
  return Checked([&] {
    if (const auto word = value.TryUint64())
      return IntegerAccess::FromUnsigned(*word);
    return IntegerAccess::Take(UnsignedBitsZ(value));
  });
}
NumericResult<Integer> BitVector::ToSignedInteger(const BitVector& value) {
  return Checked([&] {
    if (const auto word = value.TryUint64())
      return IntegerAccess::FromSigned(SignedWordValue(*word, value.Width()));
    return IntegerAccess::Take(SignedBitsZ(value));
  });
}

NumericResult<BitVector> BitVector::Not(const BitVector& value) {
  return Checked([&] {
    return Access::Build(value.Width(), value.Width(),
                         [&](auto i) { return ~value.Chunk(i); });
  });
}

NumericResult<BitVector> BitVector::Neg(const BitVector& value) {
  return Checked([&] { return Negated(value); });
}

bool BitVector::Nego(const BitVector& value) noexcept {
  if (!Negative(value)) return false;
  const auto sign = value.Width() - 1;
  if (value.Chunk(sign / 64) != std::uint64_t{1} << (sign % 64)) return false;
  for (auto i = sign / 64; i-- != 0;)
    if (value.Chunk(i) != 0) return false;
  return true;
}

NumericResult<BitVector> BitVector::And(const BitVector& a,
                                        const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return Bitwise(a, b, Logic::kAnd);
  });
}

NumericResult<BitVector> BitVector::Or(const BitVector& a, const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return Bitwise(a, b, Logic::kOr);
  });
}

NumericResult<BitVector> BitVector::Xor(const BitVector& a,
                                        const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return Bitwise(a, b, Logic::kXor);
  });
}

NumericResult<BitVector> BitVector::Nand(const BitVector& a,
                                         const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return Bitwise(a, b, Logic::kNand);
  });
}

NumericResult<BitVector> BitVector::Nor(const BitVector& a,
                                        const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return Bitwise(a, b, Logic::kNor);
  });
}

NumericResult<BitVector> BitVector::Xnor(const BitVector& a,
                                         const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return Bitwise(a, b, Logic::kXnor);
  });
}

NumericResult<BitVector> BitVector::Add(const BitVector& a,
                                        const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return AddSub(a, b, false);
  });
}

NumericResult<BitVector> BitVector::Sub(const BitVector& a,
                                        const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return AddSub(a, b, true);
  });
}

NumericResult<BitVector> BitVector::Udiv(const BitVector& a,
                                         const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return DivideUnsigned(a, b, false);
  });
}

NumericResult<BitVector> BitVector::Urem(const BitVector& a,
                                         const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return DivideUnsigned(a, b, true);
  });
}

NumericResult<BitVector> BitVector::Sdiv(const BitVector& a,
                                         const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return DivideSigned(a, b, Division::kQuotient);
  });
}

NumericResult<BitVector> BitVector::Srem(const BitVector& a,
                                         const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return DivideSigned(a, b, Division::kRemainder);
  });
}

NumericResult<BitVector> BitVector::Smod(const BitVector& a,
                                         const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return DivideSigned(a, b, Division::kModulus);
  });
}

NumericResult<BitVector> BitVector::Shl(const BitVector& a,
                                        const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return Shifted(a, b, Shift::kLeft);
  });
}

NumericResult<BitVector> BitVector::Lshr(const BitVector& a,
                                         const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return Shifted(a, b, Shift::kRight);
  });
}

NumericResult<BitVector> BitVector::Ashr(const BitVector& a,
                                         const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return Shifted(a, b, Shift::kArithmetic);
  });
}

NumericResult<BitVector> BitVector::Comp(const BitVector& a,
                                         const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return Access::Word(1, a == b);
  });
}

NumericResult<bool> BitVector::Ult(const BitVector& a, const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return UnsignedCompare(a, b) < 0;
  });
}

NumericResult<bool> BitVector::Ule(const BitVector& a, const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return UnsignedCompare(a, b) <= 0;
  });
}

NumericResult<bool> BitVector::Ugt(const BitVector& a, const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return UnsignedCompare(a, b) > 0;
  });
}

NumericResult<bool> BitVector::Uge(const BitVector& a, const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return UnsignedCompare(a, b) >= 0;
  });
}

NumericResult<bool> BitVector::Slt(const BitVector& a, const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return SignedCompare(a, b) < 0;
  });
}

NumericResult<bool> BitVector::Sle(const BitVector& a, const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return SignedCompare(a, b) <= 0;
  });
}

NumericResult<bool> BitVector::Sgt(const BitVector& a, const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return SignedCompare(a, b) > 0;
  });
}

NumericResult<bool> BitVector::Sge(const BitVector& a, const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return SignedCompare(a, b) >= 0;
  });
}

NumericResult<bool> BitVector::Uaddo(const BitVector& a, const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return UnsignedOverflow(a, b, Arithmetic::kAdd);
  });
}

NumericResult<bool> BitVector::Usubo(const BitVector& a, const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return UnsignedOverflow(a, b, Arithmetic::kSub);
  });
}

NumericResult<bool> BitVector::Umulo(const BitVector& a, const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return UnsignedOverflow(a, b, Arithmetic::kMul);
  });
}

NumericResult<bool> BitVector::Saddo(const BitVector& a, const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return SignedOverflow(a, b, Arithmetic::kAdd);
  });
}

NumericResult<bool> BitVector::Ssubo(const BitVector& a, const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return SignedOverflow(a, b, Arithmetic::kSub);
  });
}

NumericResult<bool> BitVector::Smulo(const BitVector& a, const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return SignedOverflow(a, b, Arithmetic::kMul);
  });
}

NumericResult<bool> BitVector::Sdivo(const BitVector& a, const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    return Nego(a) &&
           std::ranges::all_of(Access::Limbs(b),
                               [&, i = std::uint64_t{0}](auto word) mutable {
                                 return word == Mask(b.Width() - 64 * i++);
                               }) &&
           b.EffectiveBitLength() == b.Width();
  });
}

NumericResult<BitVector> BitVector::Mul(const BitVector& a,
                                        const BitVector& b) {
  return Checked([&] {
    SameWidth(a, b);
    if (a.EffectiveBitLength() == 0 || b.EffectiveBitLength() == 0)
      return Access::Word(a.Width(), 0);
    if (const auto x = a.TryUint64(), y = b.TryUint64(); x && y)
      return FromDoubleWord(a.Width(), Uint128{*x} * *y);
    return Product(a, b, Access::Limbs(a), Access::Limbs(b));
  });
}

NumericResult<BitVector> BitVector::Concat(const BitVector& a,
                                           const BitVector& b) {
  return Checked([&] {
    return Join(a, b, PaddedWidth(std::uint64_t{a.Width()} + b.Width()));
  });
}

NumericResult<BitVector> BitVector::Extract(const BitVector& value,
                                            IntegerView high, IntegerView low) {
  const auto h = IntegerAccess::TryUnsigned(high);
  const auto l = IntegerAccess::TryUnsigned(low);
  if (!h || !l || *l > *h || *h >= value.Width()) [[unlikely]]
    return std::unexpected(NumericError::kInvalidArgument);
  const auto width = static_cast<std::uint32_t>(*h - *l + 1);
  if (*h < 64) return Access::Word(width, value.Chunk(0) >> *l);
  if (value.limb_count_ == 0)
    return Access::Word(width, *l < 64 ? value.word_ >> *l : 0);

  const auto offset = static_cast<std::uint32_t>(*l);
  return Checked([&] {
    const auto bits = value.EffectiveBitLength();
    return Access::Build(width, bits - std::min(bits, offset),
                         [&](auto i) { return RightChunk(value, i, offset); });
  });
}

NumericResult<BitVector> BitVector::Repeat(const BitVector& value,
                                           IntegerView count) {
  return Checked([&] {
    Require(count.Sign() > 0, NumericError::kInvalidArgument);
    const auto times = count.TryUint32();
    Require(times && *times <= UINT32_MAX / value.Width(),
            NumericError::kSizeLimit);
    const auto width = *times * value.Width();
    if (value.EffectiveBitLength() == 0) return Access::Word(width, 0);
    if (*times == 1) return BitVector(value);
    auto block = value;
    std::optional<BitVector> result;
    for (auto n = *times; n != 0;) {
      if ((n & 1) != 0) {
        result = result ? Join(block, *result, block.Width() + result->Width())
                        : BitVector(block);
      }
      n >>= 1;
      if (n != 0) block = Join(block, block, block.Width() * 2);
    }
    return std::move(*result);
  });
}

NumericResult<BitVector> BitVector::ZeroExtend(const BitVector& value,
                                               IntegerView count) {
  return Checked([&] {
    const auto w =
        PaddedWidth(std::uint64_t{value.Width()} + detail::Width(count, 0));
    return Access::Build(w, value.EffectiveBitLength(),
                         [&](auto i) { return value.Chunk(i); });
  });
}

NumericResult<BitVector> BitVector::SignExtend(const BitVector& value,
                                               IntegerView count) {
  return Checked([&] {
    const auto w =
        PaddedWidth(std::uint64_t{value.Width()} + detail::Width(count, 0));
    const bool negative = Negative(value);
    return Access::Build(
        w, negative ? w : value.EffectiveBitLength(), [&](auto i) {
          return negative ? value.Chunk(i) | SignFill(i, value.Width())
                          : value.Chunk(i);
        });
  });
}

NumericResult<BitVector> BitVector::RotateLeft(const BitVector& value,
                                               IntegerView count) {
  return Checked([&] { return Rotate(value, count, true); });
}
NumericResult<BitVector> BitVector::RotateRight(const BitVector& value,
                                                IntegerView count) {
  return Checked([&] { return Rotate(value, count, false); });
}

}  // namespace dandelion::numeric

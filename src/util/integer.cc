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

IntegerView IntegerAccess::View(mpz_srcptr value) noexcept {
  const auto size = mpz_size(value);
  if (size == 0) return IntegerView();
  if (size == 1) {
    if (auto word = SignedWord(mpz_getlimbn(value, 0), mpz_sgn(value) < 0))
      return IntegerView(*word);
  }
  return BigView(value);
}

mpz_srcptr IntegerAccess::Backing(IntegerView value) noexcept {
  return value.form_ == IntegerView::Form::kBig
             ? static_cast<mpz_srcptr>(value.big_)
             : nullptr;
}

Integer IntegerAccess::Take(Z value) {
  if (const auto word = View(value.Get()).TryInt64()) return Integer(*word);
  CheckGmp(mpz_size(value.Get()));
  AllocationCheckpoint();
  auto holder = std::make_unique<BigInteger>();
  mpz_swap(holder->value, value.Get());
  Integer result;
  result.big_ = holder.release();
  return result;
}

Integer IntegerAccess::FromUnsigned(Uint128 value) {
  if (value <= INT64_MAX) return Integer(static_cast<std::int64_t>(value));
  return Take(UnsignedZ(value));
}

Integer IntegerAccess::FromMagnitude(Uint128 value, bool negative) {
  if (!negative) return FromUnsigned(value);
  if (value <= kSignedHigh)
    return Integer(*SignedWord(static_cast<std::uint64_t>(value), true));
  auto magnitude = UnsignedZ(value);
  mpz_neg(magnitude.Get(), magnitude.Get());
  return Take(std::move(magnitude));
}

Integer IntegerAccess::FromSigned(Int128 value) {
  if (value >= INT64_MIN && value <= INT64_MAX)
    return Integer(static_cast<std::int64_t>(value));
  return value < 0
             ? FromMagnitude(Uint128{0} - static_cast<Uint128>(value), true)
             : FromUnsigned(static_cast<Uint128>(value));
}

}  // namespace dandelion::numeric::detail

namespace dandelion::numeric {
namespace {

using namespace detail;

Int128 SignedMagnitude(IntegerAccess::WordMagnitude word) noexcept {
  return word.negative ? -Int128{word.magnitude} : Int128{word.magnitude};
}

Integer ParseDigits(std::string_view digits, int radix, bool negative) {
  ValidateDigits(digits, radix);
  const auto first = digits.find_first_not_of('0');
  if (first == std::string_view::npos) return Integer();
  digits.remove_prefix(first);

  std::uint64_t word = 0;
  std::size_t consumed = 0;
  for (; consumed < digits.size(); ++consumed) {
    const auto digit = static_cast<unsigned>(Digit(digits[consumed]));
    const auto next = Uint128{word} * static_cast<unsigned>(radix) + digit;
    if (next > UINT64_MAX) break;
    word = static_cast<std::uint64_t>(next);
  }
  if (consumed == digits.size()) {
    if (const auto small = SignedWord(word, negative)) return Integer(*small);
    return IntegerAccess::FromMagnitude(word, negative);
  }

  Z value;
  if (radix == 10) {
    // Extend the accumulator by up to 19 decimal digits at a time.
    value = UnsignedZ(word);
    while (consumed < digits.size()) {
      const auto count = std::min<std::size_t>(19, digits.size() - consumed);
      std::uint64_t scale = 1, chunk = 0;
      for (auto end = consumed + count; consumed < end; ++consumed) {
        scale *= 10;
        chunk = 10 * chunk + static_cast<unsigned>(Digit(digits[consumed]));
      }
      const ZView factor(IntegerView::FromUnsigned(scale));
      const ZView tail(IntegerView::FromUnsigned(chunk));
      value = MulZ(value.Get(), factor.Get());
      value = AddZ(value.Get(), tail.Get());
    }
  } else {
    const unsigned digit_bits = radix == 2 ? 1 : 4;
    Require(digits.size() - 1 <= GmpLimbCapacity() * GMP_NUMB_BITS / digit_bits,
            NumericError::kSizeLimit);
    const auto bits = (digits.size() - 1) * digit_bits +
                      WordBits(static_cast<unsigned>(Digit(digits.front())));
    value = ImportChunks(bits, [&](auto index) {
      std::uint64_t chunk = 0;
      const auto offset = index * (64 / digit_bits);
      for (unsigned shift = 0;
           shift < 64 && offset + shift / digit_bits < digits.size();
           shift += digit_bits) {
        const auto pos = digits.size() - 1 - offset - shift / digit_bits;
        chunk |= static_cast<std::uint64_t>(Digit(digits[pos])) << shift;
      }
      return chunk;
    });
  }
  if (negative) mpz_neg(value.Get(), value.Get());
  return IntegerAccess::Take(std::move(value));
}

enum class IntegerOp { kAdd, kSub, kMul };

template <IntegerOp Op>
[[gnu::noinline]] NumericResult<Integer> WideBinaryInteger(IntegerView a,
                                                           IntegerView b) {
  const auto x = IntegerAccess::TryMagnitude(a);
  const auto y = IntegerAccess::TryMagnitude(b);
  if (x && y) {
    if constexpr (Op == IntegerOp::kMul) {
      const auto magnitude = Uint128{x->magnitude} * y->magnitude;
      const bool negative = x->negative != y->negative;
      if (magnitude <= UINT64_MAX) {
        if (const auto word =
                SignedWord(static_cast<std::uint64_t>(magnitude), negative))
          return NumericResult<Integer>(std::in_place, *word);
      }
      return Checked(
          [&] { return IntegerAccess::FromMagnitude(magnitude, negative); });
    } else {
      const auto lhs = SignedMagnitude(*x), rhs = SignedMagnitude(*y);
      const auto result = Op == IntegerOp::kSub ? lhs - rhs : lhs + rhs;
      if (result >= INT64_MIN && result <= INT64_MAX)
        return NumericResult<Integer>(std::in_place,
                                      static_cast<std::int64_t>(result));
      return Checked([&] { return IntegerAccess::FromSigned(result); });
    }
  }
  return Checked([&] {
    const ZView lhs(a), rhs(b);
    if constexpr (Op == IntegerOp::kMul)
      return IntegerAccess::Take(MulZ(lhs.Get(), rhs.Get()));
    return IntegerAccess::Take(
        AddZ(lhs.Get(), rhs.Get(), Op == IntegerOp::kSub));
  });
}

template <IntegerOp Op>
bool WordOverflow(std::int64_t a, std::int64_t b, std::int64_t* out) noexcept {
  if constexpr (Op == IntegerOp::kAdd) return __builtin_add_overflow(a, b, out);
  if constexpr (Op == IntegerOp::kSub) return __builtin_sub_overflow(a, b, out);
  return __builtin_mul_overflow(a, b, out);
}

template <IntegerOp Op>
NumericResult<Integer> BinaryInteger(IntegerView a, IntegerView b) {
  if (const auto *x = IntegerAccess::InlineSigned(a),
      *y = IntegerAccess::InlineSigned(b);
      x && y) {
    std::int64_t word;
    if (!WordOverflow<Op>(*x, *y, &word))
      return NumericResult<Integer>(std::in_place, word);
  }
  [[unlikely]] return WideBinaryInteger<Op>(a, b);
}

std::pair<std::uint64_t, std::uint64_t> WordDivMod(
    IntegerAccess::WordMagnitude a, std::uint64_t b) noexcept {
  auto q = a.magnitude / b;
  auto r = a.magnitude % b;
  if (a.negative && r != 0) {
    ++q;
    r = b - r;
  }
  return {q, r};
}

using DivResult = std::pair<Integer, Integer>;

[[gnu::noinline]] NumericResult<DivResult> PromoteDivMod(std::uint64_t q,
                                                         std::uint64_t r,
                                                         bool negative) {
  return Checked([&] {
    return DivResult(IntegerAccess::FromMagnitude(q, negative),
                     IntegerAccess::FromUnsigned(r));
  });
}

[[gnu::noinline]] NumericResult<DivResult> WideDivMod(IntegerView a,
                                                      IntegerView b) {
  return Checked([&] {
    const ZView x(a), y(b);
    auto [q, r] = QuotientRemainder(x.Get(), y.Get());
    if (mpz_sgn(r.Get()) < 0) {
      const ZView one(IntegerView(1));
      const bool negative_divisor = mpz_sgn(y.Get()) < 0;
      r = AddZ(r.Get(), y.Get(), negative_divisor);
      q = AddZ(q.Get(), one.Get(), !negative_divisor);
    }
    return DivResult(IntegerAccess::Take(std::move(q)),
                     IntegerAccess::Take(std::move(r)));
  });
}

}  // namespace

Integer::Integer(const Integer& other) : Integer(other.Borrow().Copy()) {}

Integer& Integer::operator=(const Integer& other) {
  if (this != &other) *this = Integer(other);
  return *this;
}

Integer& Integer::operator=(Integer&& other) noexcept {
  if (this != &other) {
    delete big_;
    big_ = std::exchange(other.big_, nullptr);
    small_ = std::exchange(other.small_, 0);
  }
  return *this;
}

void Integer::DeleteBig(detail::BigInteger* value) noexcept { delete value; }

IntegerView Integer::BorrowBig() const noexcept {
  return IntegerAccess::BigView(big_->value);
}

std::string Integer::ToDecimal() const { return Borrow().ToDecimal(); }

IntegerView IntegerView::FromCanonicalMagnitude(
    std::span<const std::uint64_t> limbs, bool negative) noexcept {
  IntegerView result;
  result.form_ = negative ? Form::kPackedNegative : Form::kPackedPositive;
  result.limb_count_ = static_cast<std::uint32_t>(limbs.size());
  result.limbs_ = limbs.data();
  return result;
}

int IntegerView::Sign() const noexcept {
  switch (form_) {
    case Form::kSigned:
      return (signed_word_ > 0) - (signed_word_ < 0);
    case Form::kUnsigned:
      return unsigned_word_ != 0;
    case Form::kBig:
      return mpz_sgn(IntegerAccess::Backing(*this));
    case Form::kPackedPositive:
      return 1;
    case Form::kPackedNegative:
      return -1;
  }
  std::unreachable();
}

bool IntegerView::IsSmall() const noexcept { return TryInt64().has_value(); }

bool IntegerView::IsOdd() const noexcept {
  return (MagnitudeChunk(0) & 1) != 0;
}

std::uint64_t IntegerView::MagnitudeChunk(std::uint64_t index) const noexcept {
  if (limb_count_ != 0) return index < limb_count_ ? limbs_[index] : 0;
  if (form_ == Form::kBig)
    return detail::MagnitudeChunk(IntegerAccess::Backing(*this), index);
  if (index != 0) return 0;
  return form_ == Form::kSigned ? Magnitude(signed_word_) : unsigned_word_;
}

std::span<const std::uint64_t> IntegerView::MagnitudeLimbs() const noexcept {
  if (limb_count_ != 0) return {limbs_, limb_count_};
  if (const auto backing = IntegerAccess::Backing(*this))
    return {mpz_limbs_read(backing), mpz_size(backing)};
  return {};
}

std::uint64_t IntegerView::BitLength() const noexcept {
  if (limb_count_ != 0)
    return 64 * std::uint64_t{limb_count_ - 1} +
           WordBits(limbs_[limb_count_ - 1]);
  if (form_ == Form::kBig)
    return detail::BitLength(IntegerAccess::Backing(*this));
  return WordBits(MagnitudeChunk(0));
}

int IntegerView::Compare(IntegerView other) const noexcept {
  if (form_ <= Form::kUnsigned && other.form_ <= Form::kUnsigned) {
    const Int128 a =
        form_ == Form::kSigned ? Int128{signed_word_} : Int128{unsigned_word_};
    const Int128 b = other.form_ == Form::kSigned
                         ? Int128{other.signed_word_}
                         : Int128{other.unsigned_word_};
    return (a > b) - (a < b);
  }
  const ZView a(*this), b(other);
  return mpz_cmp(a.Get(), b.Get());
}

std::uint64_t IntegerView::Hash() const noexcept {
  auto hash = static_cast<std::uint64_t>(Sign() + 1);
  for (std::uint64_t i = 0; i < Words(BitLength()); ++i)
    hash = Mix(hash, MagnitudeChunk(i));
  return hash;
}

std::optional<std::int64_t> IntegerView::TryInt64() const noexcept {
  return IntegerAccess::TrySigned(*this);
}

std::optional<std::uint64_t> IntegerView::TryUint64() const noexcept {
  return IntegerAccess::TryUnsigned(*this);
}

std::optional<std::uint32_t> IntegerView::TryUint32() const noexcept {
  const auto word = TryUint64();
  if (!word || *word > UINT32_MAX) return {};
  return static_cast<std::uint32_t>(*word);
}

std::size_t IntegerView::BackingBytes() const noexcept {
  const auto value = IntegerAccess::Backing(*this);
  return value ? sizeof(BigInteger) + mpz_size(value) * sizeof(mp_limb_t) : 0;
}

Integer IntegerView::Copy() const {
  if (const auto word = TryInt64()) return Integer(*word);
  // Copying an existing value does not need spare limbs or result preflight.
  const ZView source(*this);
  AllocationCheckpoint();
  auto holder = std::make_unique<BigInteger>();
  mpz_set(holder->value, source.Get());
  Integer result;
  result.big_ = holder.release();
  return result;
}

std::string IntegerView::ToDecimal() const {
  if (form_ <= Form::kUnsigned) {
    char buffer[21];
    const auto end =
        form_ == Form::kSigned
            ? std::to_chars(std::begin(buffer), std::end(buffer), signed_word_)
                  .ptr
            : std::to_chars(std::begin(buffer), std::end(buffer),
                            unsigned_word_)
                  .ptr;
    std::string output;
    if (static_cast<std::size_t>(end - buffer) > output.capacity())
      AllocationCheckpoint();
    output.assign(buffer, end);
    return output;
  }
  const ZView source(*this);
  const auto value = source.Get();
  AllocationCheckpoint();
  std::string output(mpz_sizeinbase(value, 10) + 1, '\0');
  mpz_get_str(output.data(), 10, value);
  output.resize(std::char_traits<char>::length(output.data()));
  return output;
}

NumericResult<Integer> Integer::FromUnsigned(std::uint64_t value) {
  if (value <= INT64_MAX)
    return NumericResult<Integer>(std::in_place,
                                  static_cast<std::int64_t>(value));
  [[unlikely]] return Checked(
      [&] { return IntegerAccess::FromUnsigned(value); });
}

NumericResult<Integer> Integer::ParseDecimal(std::string_view text) {
  return Checked([&] {
    const bool negative = text.starts_with('-');
    if (negative) text.remove_prefix(1);
    return ParseDigits(text, 10, negative);
  });
}

NumericResult<Integer> Integer::ParseRadix(std::string_view digits, int radix) {
  if (radix != 2 && radix != 16)
    return std::unexpected(NumericError::kInvalidArgument);
  return Checked([&] { return ParseDigits(digits, radix, false); });
}

NumericResult<Integer> operator+(IntegerView a, IntegerView b) {
  return BinaryInteger<IntegerOp::kAdd>(a, b);
}

NumericResult<Integer> operator-(IntegerView a, IntegerView b) {
  return BinaryInteger<IntegerOp::kSub>(a, b);
}

NumericResult<Integer> operator*(IntegerView a, IntegerView b) {
  return BinaryInteger<IntegerOp::kMul>(a, b);
}

NumericResult<Integer> operator-(IntegerView value) {
  return Integer::Negate(value);
}

NumericResult<Integer> Integer::Negate(IntegerView value) {
  return Checked([&] {
    if (value.BitLength() <= 64) {
      const auto magnitude = value.MagnitudeChunk(0);
      return IntegerAccess::FromSigned(value.Sign() < 0 ? Int128{magnitude}
                                                        : -Int128{magnitude});
    }
    const ZView input(value);
    CheckGmp(mpz_size(input.Get()));
    Z result;
    mpz_neg(result.Get(), input.Get());
    return IntegerAccess::Take(std::move(result));
  });
}

NumericResult<Integer> Integer::Abs(IntegerView value) {
  if (value.Sign() < 0) return Negate(value);
  if (const auto word = value.TryUint64()) return FromUnsigned(*word);
  return Checked([&] { return value.Copy(); });
}

NumericResult<Integer> Integer::Gcd(IntegerView a, IntegerView b) {
  return Checked([&] {
    if (std::max(a.BitLength(), b.BitLength()) <= 64)
      return IntegerAccess::FromUnsigned(
          std::gcd(a.MagnitudeChunk(0), b.MagnitudeChunk(0)));
    const ZView x(a), y(b);
    return IntegerAccess::Take(GcdZ(x.Get(), y.Get()));
  });
}

NumericResult<Integer> Integer::Lcm(IntegerView a, IntegerView b) {
  return Checked([&] {
    if (a.Sign() == 0 || b.Sign() == 0) return Integer();
    if (std::max(a.BitLength(), b.BitLength()) <= 64) {
      const auto x = a.MagnitudeChunk(0), y = b.MagnitudeChunk(0);
      return IntegerAccess::FromUnsigned(Uint128{x / std::gcd(x, y)} * y);
    }
    const ZView x(a), y(b);
    const auto gcd = GcdZ(x.Get(), y.Get());
    const auto factor = ExactQuotient(x.Get(), gcd.Get());
    auto product = MulZ(factor.Get(), y.Get());
    mpz_abs(product.Get(), product.Get());
    return IntegerAccess::Take(std::move(product));
  });
}

NumericResult<std::pair<Integer, Integer>> Integer::DivMod(IntegerView a,
                                                           IntegerView b) {
  const auto y = IntegerAccess::TryMagnitude(b);
  if (y && y->magnitude == 0) [[unlikely]]
    return std::unexpected(NumericError::kDivisionByZero);
  if (const auto x = IntegerAccess::TryMagnitude(a); x && y) {
    const auto [q, r] = WordDivMod(*x, y->magnitude);
    const bool negative = x->negative != y->negative;
    if (const auto word = SignedWord(q, negative); word && r <= INT64_MAX)
      return NumericResult<DivResult>(std::in_place, *word,
                                      static_cast<std::int64_t>(r));
    [[unlikely]] return PromoteDivMod(q, r, negative);
  }
  [[unlikely]] return WideDivMod(a, b);
}

NumericResult<bool> Integer::IsDivisible(IntegerView a, IntegerView b) {
  return Checked([&] {
    Require(b.Sign() != 0, NumericError::kDivisionByZero);
    if (std::max(a.BitLength(), b.BitLength()) <= 64)
      return a.MagnitudeChunk(0) % b.MagnitudeChunk(0) == 0;
    const ZView x(a), y(b);
    CheckGmp(std::min(mpz_size(x.Get()), mpz_size(y.Get())));
    return mpz_divisible_p(x.Get(), y.Get()) != 0;
  });
}

NumericResult<Integer> Integer::ExactDiv(IntegerView a, IntegerView b) {
  return Checked([&] {
    Require(Unwrap(IsDivisible(a, b)), NumericError::kInvalidArgument);
    if (std::max(a.BitLength(), b.BitLength()) <= 64) {
      const auto magnitude = a.MagnitudeChunk(0) / b.MagnitudeChunk(0);
      return IntegerAccess::FromSigned(
          a.Sign() * b.Sign() < 0 ? -Int128{magnitude} : Int128{magnitude});
    }
    const ZView x(a), y(b);
    return IntegerAccess::Take(ExactQuotient(x.Get(), y.Get()));
  });
}

NumericResult<Integer> Integer::Power(IntegerView base, IntegerView exponent) {
  return Checked([&] {
    if (exponent.Sign() == 0) return Integer(1);
    if (base.BitLength() <= 1 || exponent.Sign() < 0)
      return Integer(base.Sign() == 1 && base.BitLength() == 1 ? 1
                     : base.Sign() == -1 && base.BitLength() == 1
                         ? (exponent.IsOdd() ? -1 : 1)
                         : 0);
    const auto power = exponent.TryUint64();
    const auto capacity = GmpLimbCapacity() * GMP_NUMB_BITS;
    // A zero injected GMP capacity still permits signed-word results.
    const auto bound = std::max<std::uint64_t>(64, capacity);
    Require(power && *power <= (bound - 1) / (base.BitLength() - 1),
            NumericError::kSizeLimit);
    auto square = base.Copy();
    Integer result(1);
    for (auto remaining = *power; remaining != 0;) {
      if (remaining & 1) result = Unwrap(result.Borrow() * square.Borrow());
      remaining >>= 1;
      if (remaining != 0) square = Unwrap(square.Borrow() * square.Borrow());
    }
    return result;
  });
}
}  // namespace dandelion::numeric

#ifdef DANDELION_UTIL_TESTING
extern "C++" {
namespace dandelion::numeric {

using namespace detail;

void NumericTestPeer::FailAllocationAfter(std::size_t successful) noexcept {
  faults.allocation = successful;
  faults.attempts = 0;
}

std::size_t NumericTestPeer::AllocationAttempts() noexcept {
  return faults.attempts;
}

void NumericTestPeer::ClearFailures() noexcept { faults = {}; }

void NumericTestPeer::SetLimbCapacity(std::uint64_t limbs) noexcept {
  faults.gmp_limbs = std::min(limbs, kGmpLimbCapacity);
}

void NumericTestPeer::SetBitVectorLimbCapacity(std::uint64_t limbs) noexcept {
  faults.packed_limbs = std::min<std::uint64_t>(limbs, UINT32_MAX);
}

void NumericTestPeer::SetByteCapacity(std::size_t bytes) noexcept {
  faults.bytes = bytes;
}

std::uint64_t NumericTestPeer::GmpLimbBits() noexcept { return GMP_NUMB_BITS; }

NumericResult<void> NumericTestPeer::CheckGmpWorkingLimbs(std::uint64_t limbs,
                                                          std::uint64_t spare) {
  return Checked([&] { CheckGmp(limbs, spare); });
}

void NumericTestPeer::ForcePortableBitVector(bool force) noexcept {
  faults.portable_bv = force;
}

const void* NumericTestPeer::BackingAddress(const Integer& value) noexcept {
  return IntegerAccess::Address(value);
}

const void* NumericTestPeer::BackingAddress(const Rational& value) noexcept {
  return RationalAccess::Address(value);
}

const void* NumericTestPeer::BackingAddress(const BitVector& value) noexcept {
  return BitVectorAccess::Address(value);
}

}  // namespace dandelion::numeric
}  // extern "C++"
#endif

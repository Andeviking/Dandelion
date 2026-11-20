module;
#include <cstdint>
#include <gmp.h>

module dandelion.util.numeric:internal;
import dandelion.util.numeric;
import std;

namespace dandelion::numeric::detail {

__extension__ using Int128 = __int128;
__extension__ using Uint128 = unsigned __int128;

// Inline numeric payloads use the same read-only limb representation as GMP.
static_assert(std::is_same_v<mp_limb_t, std::uint64_t>);
static_assert(GMP_NUMB_BITS == 64);
static_assert(std::numeric_limits<mp_size_t>::max() >=
              std::numeric_limits<int>::max());

constexpr std::uint64_t kSignedHigh = std::uint64_t{1} << 63;
constexpr std::uint64_t kGmpLimbCapacity = std::min(
    {std::uint64_t{std::numeric_limits<int>::max()},
     std::uint64_t{std::numeric_limits<mp_size_t>::max()},
     std::uint64_t{std::numeric_limits<std::size_t>::max() / sizeof(mp_limb_t)},
     std::uint64_t{std::numeric_limits<mp_bitcnt_t>::max() / GMP_NUMB_BITS}});

#ifdef DANDELION_UTIL_TESTING
struct Faults {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::optional<std::size_t> allocation;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::size_t attempts = 0;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t gmp_limbs = kGmpLimbCapacity;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t packed_limbs = UINT32_MAX;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::size_t bytes = SIZE_MAX;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  bool portable_bv = false;
};
inline thread_local Faults faults;
#endif

inline void Require(bool condition, NumericError error) {
  if (!condition) throw error;
}

template <class Function>
auto Checked(Function computation)
    -> NumericResult<std::invoke_result_t<Function>> {
  try {
    if constexpr (std::is_void_v<std::invoke_result_t<Function>>) {
      computation();
      return {};
    } else {
      return computation();
    }
  } catch (NumericError error) {
    return std::unexpected(error);
  } catch (const std::bad_alloc&) {
    return std::unexpected(NumericError::kCppAllocation);
  }
}

template <class T>
T Unwrap(NumericResult<T> result) {
  if (!result) throw result.error();
  return std::move(*result);
}

inline void AllocationCheckpoint() {
#ifdef DANDELION_UTIL_TESTING
  if (faults.allocation && faults.attempts++ == *faults.allocation)
    throw std::bad_alloc{};
#endif
}

inline std::uint64_t GmpLimbCapacity() noexcept {
#ifdef DANDELION_UTIL_TESTING
  return faults.gmp_limbs;
#else
  return kGmpLimbCapacity;
#endif
}

inline void CheckGmp(std::uint64_t limbs, std::uint64_t spare = 0,
                     std::uint64_t capacity = GmpLimbCapacity()) {
  Require(limbs <= capacity && spare <= capacity - limbs,
          NumericError::kSizeLimit);
}

inline void CheckBytes(std::uint64_t count, std::size_t element_size) {
  std::size_t capacity = SIZE_MAX;
#ifdef DANDELION_UTIL_TESTING
  capacity = faults.bytes;
#endif
  Require(count <= capacity / element_size, NumericError::kSizeLimit);
}

inline std::unique_ptr<std::uint64_t[]> NewPacked(std::uint64_t count) {
  std::uint64_t capacity = UINT32_MAX;
#ifdef DANDELION_UTIL_TESTING
  capacity = faults.packed_limbs;
#endif
  Require(count <= capacity, NumericError::kSizeLimit);
  CheckBytes(count, sizeof(std::uint64_t));
  AllocationCheckpoint();
  return std::make_unique_for_overwrite<std::uint64_t[]>(count);
}

template <class T>
std::unique_ptr<T[]> Scratch(std::size_t count) {
  CheckBytes(count, sizeof(T));
  AllocationCheckpoint();
  return std::make_unique_for_overwrite<T[]>(count);
}

template <class Word>
constexpr unsigned WordBits(Word word) noexcept {
  return static_cast<unsigned>(std::bit_width(word));
}

constexpr std::uint64_t Words(std::uint64_t bits) noexcept {
  return bits / 64 + (bits % 64 != 0);
}

constexpr std::uint64_t GmpLimbs(std::uint64_t bits) noexcept {
  return bits / GMP_NUMB_BITS + (bits % GMP_NUMB_BITS != 0);
}

constexpr std::uint64_t Mask(std::uint64_t bits) noexcept {
  return bits >= 64 ? UINT64_MAX : (std::uint64_t{1} << bits) - 1;
}

constexpr std::uint64_t Magnitude(std::int64_t value) noexcept {
  auto bits = static_cast<std::uint64_t>(value);
  return value < 0 ? std::uint64_t{0} - bits : bits;
}

constexpr std::uint64_t WordGcd(std::uint64_t a, std::uint64_t b) noexcept {
  if (std::has_single_bit(b)) {
    // The low set bit of a, capped at b, also handles a == 0.
    const auto capped = a | b;
    return capped & (std::uint64_t{0} - capped);
  }
  return std::gcd(a, b);
}

constexpr std::optional<std::int64_t> SignedWord(std::uint64_t magnitude,
                                                 bool negative) noexcept {
  if (magnitude > kSignedHigh - !negative) return {};
  return static_cast<std::int64_t>(negative ? std::uint64_t{0} - magnitude
                                            : magnitude);
}

constexpr std::uint64_t Mix(std::uint64_t seed, std::uint64_t value) noexcept {
  value ^= value >> 30;
  value *= 0xbf58476d1ce4e5b9ULL;
  value ^= value >> 27;
  value *= 0x94d049bb133111ebULL;
  return (seed ^ value ^ (value >> 31)) * 0x9e3779b97f4a7c15ULL;
}

constexpr int Digit(char ch) noexcept {
  if (ch >= '0' && ch <= '9') return ch - '0';
  if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
  if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
  return 16;
}

inline void ValidateDigits(std::string_view digits, int radix) {
  Require(!digits.empty() &&
              std::ranges::all_of(
                  digits, [radix](char ch) { return Digit(ch) < radix; }),
          NumericError::kInvalidText);
}

inline std::uint32_t PaddedWidth(std::uint64_t width) {
  Require(width <= UINT32_MAX, NumericError::kSizeLimit);
  return static_cast<std::uint32_t>(width);
}

inline std::uint64_t BitLength(mpz_srcptr value) noexcept {
  return mpz_sgn(value) == 0 ? 0 : mpz_sizeinbase(value, 2);
}

inline std::uint64_t MagnitudeChunk(mpz_srcptr value,
                                    std::uint64_t index) noexcept {
  if (index >= Words(BitLength(value))) return 0;
  if constexpr (GMP_NUMB_BITS == 64) {
    return mpz_getlimbn(value, static_cast<mp_size_t>(index));
  } else {
    std::uint64_t word = 0;
    std::uint64_t bit = index * 64;
    for (unsigned written = 0; written < 64;) {
      const auto offset = bit % GMP_NUMB_BITS;
      const auto count =
          std::min<std::uint64_t>(64 - written, GMP_NUMB_BITS - offset);
      const auto limb =
          mpz_getlimbn(value, static_cast<mp_size_t>(bit / GMP_NUMB_BITS));
      word |= (static_cast<std::uint64_t>(limb >> offset) & Mask(count))
              << written;
      written += static_cast<unsigned>(count);
      bit += count;
    }
    return word;
  }
}

struct BigInteger {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  mpz_t value;
  BigInteger() noexcept { mpz_init(value); }
  BigInteger(const BigInteger&) = delete;
  BigInteger& operator=(const BigInteger&) = delete;
  ~BigInteger() { mpz_clear(value); }
};

struct BigRational {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  mpq_t value;
  BigRational() noexcept { mpq_init(value); }
  BigRational(const BigRational&) = delete;
  BigRational& operator=(const BigRational&) = delete;
  ~BigRational() { mpq_clear(value); }
};

class Z {
 public:
  Z() noexcept = default;
  explicit Z(mpz_srcptr value) { mpz_set(value_.value, value); }
  Z(Z&& other) noexcept { mpz_swap(value_.value, other.value_.value); }
  Z& operator=(Z&& other) noexcept {
    mpz_swap(value_.value, other.value_.value);
    return *this;
  }

  mpz_ptr Get() noexcept { return value_.value; }
  mpz_srcptr Get() const noexcept { return value_.value; }

 private:
  BigInteger value_;
};

struct IntegerAccess {
  struct WordMagnitude {
    // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
    std::uint64_t magnitude;
    // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
    bool negative;
  };

  static IntegerView View(mpz_srcptr value) noexcept;
  static inline IntegerView BigView(mpz_srcptr value) noexcept {
    IntegerView view;
    view.form_ = IntegerView::Form::kBig;
    view.big_ = value;
    return view;
  }
  static mpz_srcptr Backing(IntegerView value) noexcept;
  static inline const std::int64_t* InlineSigned(
      const IntegerView& value) noexcept {
    return value.form_ == IntegerView::Form::kSigned ? &value.signed_word_
                                                     : nullptr;
  }
  static inline std::optional<std::int64_t> TrySigned(
      IntegerView value) noexcept {
    using Form = IntegerView::Form;
    if (const auto* word = InlineSigned(value)) return *word;
    if (value.form_ == Form::kUnsigned)
      return SignedWord(value.unsigned_word_, false);
    if (value.limb_count_ == 1)
      return SignedWord(value.limbs_[0], value.form_ == Form::kPackedNegative);
    return {};
  }

  static inline std::optional<WordMagnitude> TryMagnitude(
      IntegerView value) noexcept {
    using Form = IntegerView::Form;
    if (value.form_ == Form::kSigned)
      return WordMagnitude{Magnitude(value.signed_word_),
                           value.signed_word_ < 0};
    if (value.form_ == Form::kUnsigned)
      return WordMagnitude{value.unsigned_word_, false};
    if (value.limb_count_ == 1)
      return WordMagnitude{value.limbs_[0],
                           value.form_ == Form::kPackedNegative};
    if (value.form_ == Form::kBig) {
      const auto backing = static_cast<mpz_srcptr>(value.big_);
      if (mpz_size(backing) == 1)
        return WordMagnitude{mpz_getlimbn(backing, 0), mpz_sgn(backing) < 0};
    }
    return {};
  }

  static inline std::optional<std::uint64_t> TryUnsigned(
      IntegerView value) noexcept {
    const auto word = TryMagnitude(value);
    if (word && !word->negative) return word->magnitude;
    return {};
  }

  static Integer Take(Z value);
  static Integer FromSigned(Int128 value);
  static Integer FromUnsigned(Uint128 value);
  static Integer FromMagnitude(Uint128 value, bool negative);
  static const void* Address(const Integer& value) noexcept {
    return value.big_;
  }
};

inline NumericResult<std::uint32_t> CheckedWidth(
    IntegerView width, std::uint32_t minimum = 1) noexcept {
  if (const auto word = IntegerAccess::TryMagnitude(width)) {
    if (word->negative || word->magnitude < minimum)
      return std::unexpected(NumericError::kInvalidArgument);
    if (word->magnitude <= UINT32_MAX)
      return static_cast<std::uint32_t>(word->magnitude);
  } else if (width.Sign() < 0) {
    return std::unexpected(NumericError::kInvalidArgument);
  }
  return std::unexpected(NumericError::kSizeLimit);
}

inline std::uint32_t Width(IntegerView width, std::uint32_t minimum = 1) {
  return Unwrap(CheckedWidth(width, minimum));
}

struct RationalAccess {
  static Rational Word(std::int64_t numerator,
                       std::uint64_t denominator) noexcept;
  static Rational Take(Z numerator, Z denominator);
  static const void* Address(const Rational& value) noexcept {
    return value.denominator_ == 0 ? value.big_ : nullptr;
  }
};

// Stack-backed GMP inputs can be borrowed even when the configured result
// capacity is zero. GMP never mutates or frees this storage.
class ZView {
 public:
  explicit ZView(IntegerView value) noexcept
      : source_(IntegerAccess::Backing(value)) {
    if (source_) return;
    const auto packed = value.MagnitudeLimbs();
    if (!packed.empty()) {
      source_ =
          mpz_roinit_n(header_, packed.data(),
                       static_cast<mp_size_t>(packed.size()) * value.Sign());
      return;
    }
    word_ = value.MagnitudeChunk(0);
    source_ = mpz_roinit_n(header_, &word_, value.Sign());
  }
  ZView(const ZView&) = delete;
  ZView& operator=(const ZView&) = delete;
  mpz_srcptr Get() const noexcept { return source_; }

 private:
  mpz_srcptr source_;
  mp_limb_t word_;
  mpz_t header_;
};

template <class Chunk>
Z ImportChunks(std::uint64_t bits, Chunk chunk);

inline Z UnsignedZ(Uint128 value) {
  const auto high = static_cast<std::uint64_t>(value >> 64);
  const auto low = static_cast<std::uint64_t>(value);
  const auto bits = high == 0 ? WordBits(low) : 64 + WordBits(high);
  return ImportChunks(bits, [low, high](auto i) {
    return i == 0 ? low : i == 1 ? high : 0;
  });
}

inline Z AddZ(mpz_srcptr a, mpz_srcptr b, bool subtract = false) {
  int sign_a = mpz_sgn(a), sign_b = subtract ? -mpz_sgn(b) : mpz_sgn(b);
  const auto count = std::max(mpz_size(a), mpz_size(b));
  const auto capacity = GmpLimbCapacity();
  Z result;
  if (count < capacity) {
    if (subtract)
      mpz_sub(result.Get(), a, b);
    else
      mpz_add(result.Get(), a, b);
    return result;
  }
  if (sign_a == 0 || sign_b == 0) {
    CheckGmp(count);
    const bool take_b = sign_a == 0;
    mpz_set(result.Get(), take_b ? b : a);
    if (take_b && subtract) mpz_neg(result.Get(), result.Get());
    return result;
  }
  if (sign_a == sign_b) {
    CheckGmp(count);
    if (mpz_size(a) < mpz_size(b)) std::swap(a, b);
    const auto n = static_cast<mp_size_t>(count);
    auto* out = mpz_limbs_write(result.Get(), n);
    const auto carry = mpn_add(out, mpz_limbs_read(a), n, mpz_limbs_read(b),
                               static_cast<mp_size_t>(mpz_size(b)));
    Require(carry == 0, NumericError::kSizeLimit);
    mpz_limbs_finish(result.Get(), n * sign_a);
    return result;
  }
  // mpz_add and mpz_sub both reserve an extra limb. At the limit, use
  // magnitude subtraction, which reserves exactly the larger input size.
  const auto order = mpz_cmpabs(a, b);
  if (order == 0) return result;
  if (order < 0) {
    std::swap(a, b);
    std::swap(sign_a, sign_b);
  }
  const auto n = static_cast<mp_size_t>(count);
  auto* out = mpz_limbs_write(result.Get(), n);
  mpn_sub(out, mpz_limbs_read(a), n, mpz_limbs_read(b),
          static_cast<mp_size_t>(mpz_size(b)));
  mpz_limbs_finish(result.Get(), n * sign_a);
  CheckGmp(mpz_size(result.Get()));
  return result;
}

inline Z MulZ(mpz_srcptr a, mpz_srcptr b) {
  Z result;
  if (mpz_sgn(a) == 0 || mpz_sgn(b) == 0) return result;
  if (mpz_size(a) < mpz_size(b)) std::swap(a, b);
  const auto n = mpz_size(a), m = mpz_size(b);
  const auto capacity = GmpLimbCapacity();
  if (n + m <= capacity) {
    mpz_mul(result.Get(), a, b);
    return result;
  }
  CheckGmp(GmpLimbs(BitLength(a) + BitLength(b) - 1));
  // One extra native limb suffices when the product's length straddles
  // the limit. This scratch range is independent of the mpz allocation count.
  auto scratch = Scratch<mp_limb_t>(n + m);
  mpn_mul(scratch.get(), mpz_limbs_read(a), static_cast<mp_size_t>(n),
          mpz_limbs_read(b), static_cast<mp_size_t>(m));
  const auto used = n + m - (scratch[n + m - 1] == 0);
  CheckGmp(used);
  auto count = static_cast<mp_size_t>(used);
  auto* out = mpz_limbs_write(result.Get(), count);
  std::copy_n(scratch.get(), used, out);
  mpz_limbs_finish(result.Get(), count * mpz_sgn(a) * mpz_sgn(b));
  return result;
}

inline Z ShiftLeft(mpz_srcptr value, std::uint64_t shift) {
  Z result;
  const auto bits = BitLength(value);
  if (bits == 0) return result;
  const auto capacity = GmpLimbCapacity() * GMP_NUMB_BITS;
  Require(bits <= capacity && shift <= capacity - bits,
          NumericError::kSizeLimit);
  const auto used = static_cast<mp_size_t>(GmpLimbs(bits + shift));
  const auto offset = static_cast<std::size_t>(shift / GMP_NUMB_BITS);
  const auto fraction = static_cast<unsigned>(shift % GMP_NUMB_BITS);
  const auto n = mpz_size(value);
  auto* out = mpz_limbs_write(result.Get(), used);
  std::fill_n(out, offset, mp_limb_t{0});
  if (fraction == 0) {
    std::copy_n(mpz_limbs_read(value), n, out + offset);
  } else {
    const auto high = mpn_lshift(out + offset, mpz_limbs_read(value),
                                 static_cast<mp_size_t>(n), fraction);
    if (high != 0) out[offset + n] = high;
  }
  mpz_limbs_finish(result.Get(), used * mpz_sgn(value));
  return result;
}

inline Z PowerOfTwo(std::uint64_t bit) {
  Require(bit < GmpLimbCapacity() * GMP_NUMB_BITS, NumericError::kSizeLimit);
  Z value;
  mpz_setbit(value.Get(), static_cast<mp_bitcnt_t>(bit));
  return value;
}

inline Z ShiftRight(mpz_srcptr value, std::uint64_t shift) {
  Z result;
  const auto bits = BitLength(value);
  if (shift < bits) {
    // GMP's tdiv shift may reserve whole leading limbs before trimming.
    CheckGmp(mpz_size(value) - shift / GMP_NUMB_BITS);
    mpz_tdiv_q_2exp(result.Get(), value, static_cast<mp_bitcnt_t>(shift));
  }
  return result;
}

inline Z LowBits(mpz_srcptr value, std::uint64_t bits) {
  Z result;
  const auto n =
      static_cast<std::size_t>(GmpLimbs(std::min(bits, BitLength(value))));
  if (n == 0) return result;
  CheckGmp(n);
  auto* out = mpz_limbs_write(result.Get(), static_cast<mp_size_t>(n));
  std::copy_n(mpz_limbs_read(value), n, out);
  if (const auto tail = bits % GMP_NUMB_BITS; tail != 0 && n == GmpLimbs(bits))
    out[n - 1] &= static_cast<mp_limb_t>(Mask(tail));
  mpz_limbs_finish(result.Get(), static_cast<mp_size_t>(n));
  return result;
}

inline std::pair<Z, Z> QuotientRemainder(
    mpz_srcptr a, mpz_srcptr b, std::uint64_t capacity = GmpLimbCapacity()) {
  Z q, r;
  const auto n = mpz_size(a), m = mpz_size(b);
  if (n < m) {
    CheckGmp(n, 0, capacity);
    return {std::move(q), Z(a)};
  }
  CheckGmp(std::max(m, n - m + 1), 0, capacity);
  mpz_tdiv_qr(q.Get(), r.Get(), a, b);
  return {std::move(q), std::move(r)};
}

inline Z GcdZ(mpz_srcptr a, mpz_srcptr b) {
  // GMP uses native scratch for the operands and stores the normalized
  // result in the destination mpz.
  Z gcd;
  mpz_gcd(gcd.Get(), a, b);
  CheckGmp(mpz_size(gcd.Get()));
  return gcd;
}

inline Z ExactQuotient(mpz_srcptr a, mpz_srcptr b) {
  Z result;
  if (mpz_sgn(a) != 0) {
    CheckGmp(mpz_size(a) - mpz_size(b) + 1);
    mpz_divexact(result.Get(), a, b);
  }
  return result;
}

// Compare positive n and d*2^shift without expanding a shifted operand.
inline int CompareScaled(mpz_srcptr n, mpz_srcptr d,
                         std::int64_t shift) noexcept {
  if (shift < 0) return -CompareScaled(d, n, -shift);
  const auto amount = static_cast<std::uint64_t>(shift);
  const auto nbits = BitLength(n), dbits = BitLength(d);
  if (nbits < dbits || nbits - dbits != amount)
    return nbits >= dbits && nbits - dbits > amount ? 1 : -1;
  const auto offset = amount / 64, fraction = amount % 64;
  for (auto i = Words(nbits); i-- != 0;) {
    const auto lhs = MagnitudeChunk(n, i);
    std::uint64_t rhs = 0;
    if (i >= offset) {
      rhs = MagnitudeChunk(d, i - offset) << fraction;
      if (fraction != 0 && i > offset)
        rhs |= MagnitudeChunk(d, i - offset - 1) >> (64 - fraction);
    }
    if (lhs != rhs) return lhs < rhs ? -1 : 1;
  }
  return 0;
}

inline std::int64_t FloorLogRatio(mpz_srcptr n, mpz_srcptr d) noexcept {
  const auto difference = static_cast<std::int64_t>(BitLength(n)) -
                          static_cast<std::int64_t>(BitLength(d));
  return difference - (CompareScaled(n, d, difference) < 0);
}

template <class Chunk>
Z ImportChunks(std::uint64_t bits, Chunk chunk) {
  Z result;
  if (bits == 0) return result;
  const auto count = GmpLimbs(bits);
  CheckGmp(count);
  auto* out = mpz_limbs_write(result.Get(), static_cast<mp_size_t>(count));
  for (std::uint64_t i = 0; i < count; ++i) {
    const auto bit = i * GMP_NUMB_BITS;
    const auto offset = bit % 64;
    auto word = chunk(bit / 64) >> offset;
    if (offset + GMP_NUMB_BITS > 64)
      word |= chunk(bit / 64 + 1) << (64 - offset);
    out[i] = static_cast<mp_limb_t>(word & Mask(GMP_NUMB_BITS));
  }
  mpz_limbs_finish(result.Get(), static_cast<mp_size_t>(count));
  return result;
}

struct BitVectorAccess {
  static BitVector Word(std::uint32_t width, std::uint64_t word) noexcept;
  static BitVector Take(std::uint32_t width,
                        std::unique_ptr<std::uint64_t[]> limbs,
                        std::uint64_t count) noexcept;
  static inline std::span<const std::uint64_t> Limbs(
      const BitVector& value) noexcept {
    if (value.limb_count_ != 0) return {value.limbs_, value.limb_count_};
    return {&value.word_, value.word_ != 0 ? std::size_t{1} : 0};
  }
  static const void* Address(const BitVector& value) noexcept;
  static BitVector FromZ(std::uint32_t width, mpz_srcptr value);
  static Z ToZ(const BitVector& value, bool signed_input);

  template <class Chunk>
  static BitVector Build(std::uint32_t width, std::uint64_t bits, Chunk chunk) {
    bits = std::min<std::uint64_t>(bits, width);
    auto count = Words(bits);
    if (count == 0) return Word(width, 0);
    auto top = chunk(count - 1) & Mask(bits - (count - 1) * 64);
    while (top == 0 && count > 1) top = chunk(--count - 1);
    if (count == 1) return Word(width, top);
    auto limbs = NewPacked(count);
    limbs[count - 1] = top;
    for (std::uint64_t i = 0; i + 1 < count; ++i) limbs[i] = chunk(i);
    return Take(width, std::move(limbs), count);
  }
};

}  // namespace dandelion::numeric::detail

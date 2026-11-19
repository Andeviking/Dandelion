module;
#include <gmp.h>

module dandelion.numeric;
import std;

namespace dandelion::detail {
#ifdef DANDELION_NODE_TESTING
thread_local std::optional<std::size_t> numeric_failure;
thread_local int gmp_limb_limit = std::numeric_limits<int>::max();
#endif
void NumericAllocationCheckpoint() {
#ifdef DANDELION_NODE_TESTING
  if (numeric_failure) {
    if (*numeric_failure == 0) {
      numeric_failure.reset();
      throw std::bad_alloc{};
    }
    --*numeric_failure;
  }
#endif
}
struct BigInteger {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  mpz_t value;
  BigInteger() { mpz_init(value); }
  BigInteger(const BigInteger& other) { mpz_init_set(value, other.value); }
  BigInteger& operator=(const BigInteger&) = delete;
  ~BigInteger() noexcept { mpz_clear(value); }
};

struct NumericAccess {
  static void Set(mpz_ptr target, IntegerView source) noexcept {
    if (source.form_ == IntegerView::Form::kBig) {
      mpz_set(target, source.big_->value);
    } else {
      const auto magnitude = source.MagnitudeChunk(0);
      mpz_import(target, 1, -1, sizeof(magnitude), 0, 0, &magnitude);
      if (source.Sign() < 0) mpz_neg(target, target);
    }
  }
  static Integer From(mpz_srcptr value) {
    const int sign = mpz_sgn(value);
    if (mpz_sizeinbase(value, 2) <= 64) {
      std::uint64_t magnitude = 0;
      std::size_t count = 0;
      mpz_export(&magnitude, &count, -1, sizeof(magnitude), 0, 0, value);
      if (sign >= 0 &&
          magnitude <= static_cast<std::uint64_t>(
                           std::numeric_limits<std::int64_t>::max())) {
        return Integer(static_cast<std::int64_t>(magnitude));
      }
      if (sign < 0 && magnitude <= (std::uint64_t{1} << 63)) {
        return Integer(magnitude == (std::uint64_t{1} << 63)
                           ? std::numeric_limits<std::int64_t>::min()
                           : -static_cast<std::int64_t>(magnitude));
      }
    }
    Integer result;
    NumericAllocationCheckpoint();
    auto big = std::make_unique<BigInteger>();
    mpz_set(big->value, value);
    result.big_ = big.release();
    return result;
  }
};
}  // namespace dandelion::detail

namespace dandelion {
namespace {
std::uint64_t Mix(std::uint64_t value) noexcept {
  value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31);
}
std::uint64_t Append(std::uint64_t hash, std::uint64_t value) noexcept {
  return Mix(hash ^ Mix(value + 0x9e3779b97f4a7c15ULL));
}
class GmpRational {
 public:
  GmpRational() { mpq_init(value_); }
  GmpRational(const GmpRational&) = delete;
  GmpRational& operator=(const GmpRational&) = delete;
  ~GmpRational() noexcept { mpq_clear(value_); }
  mpq_ptr Get() noexcept { return value_; }

 private:
  mpq_t value_;
};
struct NumericSizeFailure {};

template <class Function>
auto GuardNumber(Function function) -> Result<std::invoke_result_t<Function>> {
  try {
    return function();
  } catch (const std::bad_alloc&) {
    return std::unexpected(
        NodeError{ErrorCode::kResourceLimit, ResourceKind::kCppAllocation});
  } catch (const std::length_error&) {
    return std::unexpected(
        NodeError{ErrorCode::kResourceLimit, ResourceKind::kSize});
  } catch (const NumericSizeFailure&) {
    return std::unexpected(
        NodeError{ErrorCode::kResourceLimit, ResourceKind::kSize});
  }
}

bool IsDigit(char ch) noexcept { return ch >= '0' && ch <= '9'; }

void SetRational(mpq_ptr out, RationalView value) {
  detail::NumericAccess::Set(mpq_numref(out), value.Numerator());
  detail::NumericAccess::Set(mpq_denref(out), value.Denominator());
}

// GMP uses int for limb counts in both mpz objects and mpz temporaries.
constexpr std::uint64_t kGmpLimbCapacity = std::min(
    {std::uint64_t{std::numeric_limits<int>::max()},
     std::uint64_t{std::numeric_limits<std::size_t>::max() / sizeof(mp_limb_t)},
     std::uint64_t{std::numeric_limits<mp_bitcnt_t>::max() / GMP_NUMB_BITS}});
constexpr auto kGmpBitCapacity = kGmpLimbCapacity * GMP_NUMB_BITS;
static_assert(kGmpBitCapacity < std::numeric_limits<std::int64_t>::max() / 2);
constexpr std::uint64_t kWordImportLimbs =
    (std::numeric_limits<std::uint64_t>::digits + GMP_NUMB_BITS - 1) /
    GMP_NUMB_BITS;

std::uint64_t GmpLimbCapacity() noexcept {
#ifdef DANDELION_NODE_TESTING
  return std::min(kGmpLimbCapacity,
                  static_cast<std::uint64_t>(detail::gmp_limb_limit));
#else
  return kGmpLimbCapacity;
#endif
}

void CheckGmpLimbs(std::uint64_t limbs) {
  if (limbs > GmpLimbCapacity()) throw NumericSizeFailure{};
}

std::uint64_t MagnitudeLimbs(IntegerView value) noexcept {
  return (value.BitLength() + GMP_NUMB_BITS - 1) / GMP_NUMB_BITS;
}

// The high half of the product, computed with 32-bit partial products.
constexpr std::uint64_t MultiplyHigh(std::uint64_t left,
                                     std::uint64_t right) noexcept {
  constexpr auto kMask = std::numeric_limits<std::uint32_t>::max();
  const auto left_low = left & kMask, left_high = left >> 32;
  const auto right_low = right & kMask, right_high = right >> 32;
  const auto middle = left_high * right_low + (left_low * right_low >> 32);
  const auto carry = (left_low * right_high + (middle & kMask)) >> 32;
  return left_high * right_high + (middle >> 32) + carry;
}

// GMP 6.3.0's mp_bases stores log2(radix) scaled by 2^(limb bits - 3).
// Its generator rounds down, using the value just below for powers of two.
constexpr std::uint64_t kLog2Ten = 0x6a4d3c25e68dc57f;

constexpr std::uint64_t StringImportLimbs(std::uint64_t digits, int radix) {
  constexpr std::uint64_t kMaxLimb = std::numeric_limits<mp_limb_t>::max();
  if (digits > std::min(kGmpBitCapacity, kMaxLimb)) throw NumericSizeFailure{};
  const std::uint64_t log =
      radix == 10 ? kLog2Ten : (std::uint64_t{1} << (radix == 2 ? 61 : 63)) - 1;
  // GMP's LIMBS_PER_DIGIT_IN_BASE truncates before multiplying by eight.
  constexpr int kPadding = 64 - GMP_LIMB_BITS;
  const auto eighths = MultiplyHigh(digits, (log >> kPadding) << kPadding);
  const auto bits = 8 * eighths;
  if (bits > kMaxLimb) throw NumericSizeFailure{};
  return bits / GMP_NUMB_BITS + 2;
}

#ifdef DANDELION_NODE_TESTING
static_assert(GMP_NUMB_BITS != 64 || kGmpLimbCapacity != 2'147'483'647 ||
              (StringImportLimbs(41'373'247'529, 10) == kGmpLimbCapacity &&
               StringImportLimbs(41'373'247'530, 10) == kGmpLimbCapacity + 1));
#endif

void Canonicalize(mpq_ptr value) {
  // GMP 6.3.0 initializes the gcd temporary with one spare limb.
  CheckGmpLimbs(
      1 + std::max(mpz_size(mpq_numref(value)), mpz_size(mpq_denref(value))));
  mpq_canonicalize(value);
}

void Multiply(mpz_ptr out, mpz_srcptr a, mpz_srcptr b) {
  CheckGmpLimbs(mpz_size(a) + mpz_size(b));
  mpz_mul(out, a, b);
}

void SetPowerOfTen(mpz_ptr out, std::uint64_t exponent) {
  if (exponent > kGmpBitCapacity) throw NumericSizeFailure{};
  const auto capacity = GmpLimbCapacity();
  // GMP 6.3.0's mpz_n_pow_ui reserves up to four bits per decimal
  // digit, including the trailing powers of two, and five spare limbs.
  if (exponent <= std::numeric_limits<unsigned long>::max() / 4 &&
      4 * exponent / GMP_NUMB_BITS + 5 <= capacity) {
    mpz_ui_pow_ui(out, 10, static_cast<unsigned long>(exponent));
    return;
  }

  // Close to the boundary, check each actual multiplication allocation.
  // A lower bound on floor(log2(10^exponent)) can rule out the result early.
  if (MultiplyHigh(8 * exponent, kLog2Ten) >= capacity * GMP_NUMB_BITS)
    throw NumericSizeFailure{};
  mpz_set_ui(out, 1);
  for (auto bit = std::bit_floor(exponent); bit != 0; bit >>= 1) {
    Multiply(out, out, out);
    if ((exponent & bit) != 0) {
      CheckGmpLimbs(mpz_size(out) + 1);
      mpz_mul_ui(out, out, 10);
    }
  }
}

void ShiftMagnitude(mpz_ptr out, mpz_srcptr in, std::uint64_t shift) {
  if (shift > std::numeric_limits<mp_bitcnt_t>::max())
    throw NumericSizeFailure{};
  // GMP 6.3.0 allocates one extra limb even for a whole-limb shift.
  if (const auto limbs = mpz_size(in); limbs != 0)
    CheckGmpLimbs(limbs + shift / GMP_NUMB_BITS + 1);
  mpz_mul_2exp(out, in, static_cast<mp_bitcnt_t>(shift));
}

void SetBit(mpz_ptr out, std::uint32_t bit) {
  CheckGmpLimbs(bit / GMP_NUMB_BITS + 1);
  mpz_setbit(out, bit);
}

std::int64_t FloorLogRatio(mpz_srcptr numerator, mpz_srcptr denominator) {
  const auto difference =
      static_cast<std::int64_t>(mpz_sizeinbase(numerator, 2)) -
      static_cast<std::int64_t>(mpz_sizeinbase(denominator, 2));
  if (difference == 0) return mpz_cmp(numerator, denominator) < 0 ? -1 : 0;
  detail::BigInteger aligned;
  if (difference > 0) {
    ShiftMagnitude(aligned.value, denominator,
                   static_cast<std::uint64_t>(difference));
    return difference - (mpz_cmp(numerator, aligned.value) < 0);
  }
  ShiftMagnitude(aligned.value, numerator,
                 static_cast<std::uint64_t>(-difference));
  return difference - (mpz_cmp(aligned.value, denominator) < 0);
}

FpEncoding FpOverflow(std::uint32_t exponent_width,
                      std::uint32_t fraction_width, bool negative,
                      bool infinity) {
  if (infinity) return {negative, true, Integer{}, Integer{}};
  detail::BigInteger exponent, fraction;
  SetBit(exponent.value, exponent_width);
  mpz_sub_ui(exponent.value, exponent.value, 2);
  SetBit(fraction.value, fraction_width);
  mpz_sub_ui(fraction.value, fraction.value, 1);
  return {negative, false, detail::NumericAccess::From(exponent.value),
          detail::NumericAccess::From(fraction.value)};
}
}  // namespace

const char* ReferenceLimit::what() const noexcept {
  return "Node manager reference count exhausted";
}
Integer::Integer(std::int64_t value) noexcept : small_(value) {}
Integer::Integer(const Integer& other) : small_(other.small_) {
  if (other.big_) {
    detail::NumericAllocationCheckpoint();
    big_ = new detail::BigInteger(*other.big_);
  }
}
Integer& Integer::operator=(const Integer& other) {
  if (this != &other) {
    Integer copy(other);
    *this = std::move(copy);
  }
  return *this;
}
Integer::Integer(Integer&& other) noexcept
    : small_(other.small_), big_(std::exchange(other.big_, nullptr)) {
  other.small_ = 0;
}
Integer& Integer::operator=(Integer&& other) noexcept {
  if (this != &other) {
    delete big_;
    big_ = std::exchange(other.big_, nullptr);
    small_ = std::exchange(other.small_, 0);
  }
  return *this;
}
Integer::~Integer() noexcept { delete big_; }
Result<Integer> Integer::ParseDecimal(std::string_view text) {
  if (text.empty())
    return std::unexpected(NodeError{ErrorCode::kInvalidLiteral});
  const auto first = text.front() == '-' ? std::size_t{1} : std::size_t{0};
  if (first == text.size())
    return std::unexpected(NodeError{ErrorCode::kInvalidLiteral});
  for (std::size_t i = first; i < text.size(); ++i) {
    if (text[i] < '0' || text[i] > '9')
      return std::unexpected(NodeError{ErrorCode::kInvalidLiteral});
  }
  std::int64_t small = 0;
  const auto parsed =
      std::from_chars(text.data(), text.data() + text.size(), small);
  if (parsed.ec == std::errc{}) return Integer(small);
  return GuardNumber([&] {
    const auto significant = text.find_first_not_of('0', first);
    const auto digits = text.substr(significant);
    CheckGmpLimbs(StringImportLimbs(digits.size(), 10));
    detail::BigInteger big;
    detail::NumericAllocationCheckpoint();
    const std::string terminated(digits);
    mpz_set_str(big.value, terminated.c_str(), 10);
    if (first != 0) mpz_neg(big.value, big.value);
    return detail::NumericAccess::From(big.value);
  });
}
Result<Integer> Integer::ParseRadix(std::string_view digits, int radix) {
  if (radix != 2 && radix != 16)
    return std::unexpected(NodeError{ErrorCode::kInvalidParameter});
  if (digits.empty())
    return std::unexpected(NodeError{ErrorCode::kInvalidLiteral});
  const auto* end = digits.data() + digits.size();
  std::uint64_t magnitude = 0;
  const auto parsed = std::from_chars(digits.data(), end, magnitude, radix);
  if (parsed.ptr != end || parsed.ec == std::errc::invalid_argument)
    return std::unexpected(NodeError{ErrorCode::kInvalidLiteral});
  return GuardNumber([&] {
    if (parsed.ec == std::errc{}) {
      const auto value = IntegerView::FromUnsigned(magnitude);
      if (!value.IsSmall()) CheckGmpLimbs(kWordImportLimbs);
      return value.Copy();
    }
    digits.remove_prefix(digits.find_first_not_of('0'));
    CheckGmpLimbs(StringImportLimbs(digits.size(), radix));
    detail::BigInteger wide;
    detail::NumericAllocationCheckpoint();
    const std::string terminated(digits);
    mpz_set_str(wide.value, terminated.c_str(), radix);
    return detail::NumericAccess::From(wide.value);
  });
}
Result<Integer> Integer::Negate(IntegerView value) {
  return GuardNumber([&] {
    const auto small = value.TryInt64();
    if (small && *small != std::numeric_limits<std::int64_t>::min())
      return Integer(-*small);
    CheckGmpLimbs(MagnitudeLimbs(value));
    detail::BigInteger number;
    detail::NumericAccess::Set(number.value, value);
    mpz_neg(number.value, number.value);
    return detail::NumericAccess::From(number.value);
  });
}
IntegerView Integer::Borrow() const& noexcept {
  IntegerView result(small_);
  if (big_) {
    result.form_ = IntegerView::Form::kBig;
    result.big_ = big_;
  }
  return result;
}
std::string Integer::ToDecimal() const { return Borrow().ToDecimal(); }
IntegerView::IntegerView(std::int64_t value) noexcept : signed_word_(value) {}
IntegerView IntegerView::FromUnsigned(std::uint64_t value) noexcept {
  if (value <=
      static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
    return IntegerView(static_cast<std::int64_t>(value));
  IntegerView result;
  result.form_ = Form::kUnsigned;
  result.unsigned_word_ = value;
  return result;
}
bool IntegerView::IsSmall() const noexcept { return form_ == Form::kSigned; }
int IntegerView::Sign() const noexcept {
  if (form_ == Form::kSigned) return (signed_word_ > 0) - (signed_word_ < 0);
  if (form_ == Form::kUnsigned) return 1;
  return mpz_sgn(big_->value);
}
std::uint64_t IntegerView::BitLength() const noexcept {
  if (form_ == Form::kBig)
    return static_cast<std::uint64_t>(mpz_sizeinbase(big_->value, 2));
  const auto magnitude = MagnitudeChunk(0);
  return static_cast<std::uint64_t>(std::bit_width(magnitude));
}
std::uint64_t IntegerView::MagnitudeChunk(std::uint64_t index) const noexcept {
  if (form_ == Form::kSigned) {
    if (index != 0) return 0;
    const auto bits = static_cast<std::uint64_t>(signed_word_);
    return signed_word_ < 0 ? std::uint64_t{0} - bits : bits;
  }
  if (form_ == Form::kUnsigned) return index == 0 ? unsigned_word_ : 0;
  if constexpr (GMP_NUMB_BITS == 64) {
    if (index >= mpz_size(big_->value)) return 0;
    return mpz_getlimbn(big_->value, static_cast<mp_size_t>(index));
  }
  const auto length = BitLength();
  if (index > (length - 1) / 64) return 0;
  const auto first_bit = index * 64;
  std::uint64_t result = 0;
  // mpz_getlimbn reads magnitude. Respect GMP's value bits, including nails.
  for (std::uint64_t bit = 0; bit < 64 && first_bit + bit < length; ++bit) {
    const auto position = first_bit + bit;
    const auto limb = mpz_getlimbn(
        big_->value, static_cast<mp_size_t>(position / GMP_NUMB_BITS));
    result |=
        static_cast<std::uint64_t>((limb >> (position % GMP_NUMB_BITS)) & 1)
        << bit;
  }
  return result;
}
int IntegerView::Compare(IntegerView other) const noexcept {
  if (form_ == Form::kSigned && other.form_ == Form::kSigned)
    return (signed_word_ > other.signed_word_) -
           (signed_word_ < other.signed_word_);
  const auto sign = Sign();
  const auto other_sign = other.Sign();
  if (sign != other_sign) return sign < other_sign ? -1 : 1;
  if (sign == 0) return 0;
  const auto length = BitLength();
  const auto other_length = other.BitLength();
  if (length != other_length) return (length < other_length ? -1 : 1) * sign;
  auto chunks = (length - 1) / 64 + 1;
  while (chunks != 0) {
    --chunks;
    const auto a = MagnitudeChunk(chunks);
    const auto b = other.MagnitudeChunk(chunks);
    if (a != b) return (a < b ? -1 : 1) * sign;
  }
  return 0;
}
std::uint64_t IntegerView::Hash() const noexcept {
  const auto length = BitLength();
  const auto chunks = length == 0 ? 0 : (length - 1) / 64 + 1;
  auto hash = Append(0x243f6a8885a308d3ULL, Sign() < 0 ? 1 : 0);
  hash = Append(hash, chunks);
  for (std::uint64_t i = 0; i < chunks; ++i)
    hash = Append(hash, MagnitudeChunk(i));
  return hash;
}
std::optional<std::int64_t> IntegerView::TryInt64() const noexcept {
  if (form_ != Form::kSigned) return std::nullopt;
  return signed_word_;
}
std::optional<std::uint64_t> IntegerView::TryUint64() const noexcept {
  if (form_ == Form::kSigned) {
    if (signed_word_ < 0) return std::nullopt;
    return static_cast<std::uint64_t>(signed_word_);
  }
  if (form_ == Form::kUnsigned) return unsigned_word_;
  if (Sign() < 0 || BitLength() > 64) return std::nullopt;
  return MagnitudeChunk(0);
}
std::optional<std::uint32_t> IntegerView::TryUint32() const noexcept {
  const auto value = TryUint64();
  if (!value || *value > std::numeric_limits<std::uint32_t>::max())
    return std::nullopt;
  return static_cast<std::uint32_t>(*value);
}
std::string IntegerView::ToDecimal() const {
  detail::NumericAllocationCheckpoint();
  if (form_ == Form::kSigned) return std::to_string(signed_word_);
  if (form_ == Form::kUnsigned) return std::to_string(unsigned_word_);
  const auto digits = mpz_sizeinbase(big_->value, 10);
  if (digits > std::string{}.max_size() - 2)
    throw std::length_error("Integer decimal size exhausted");
  std::string result(digits + 2, '\0');
  mpz_get_str(result.data(), 10, big_->value);
  result.resize(std::strlen(result.c_str()));
  return result;
}
Integer IntegerView::Copy() const {
  if (form_ == Form::kSigned) return Integer(signed_word_);
  if (form_ == Form::kBig) return detail::NumericAccess::From(big_->value);
  detail::BigInteger big;
  detail::NumericAccess::Set(big.value, *this);
  return detail::NumericAccess::From(big.value);
}
std::size_t IntegerView::BackingBytes() const noexcept {
  if (form_ != Form::kBig) return 0;
  return sizeof(detail::BigInteger) + mpz_size(big_->value) * sizeof(mp_limb_t);
}
Rational::Rational(Integer numerator, Integer denominator) noexcept
    : numerator_(std::move(numerator)), denominator_(std::move(denominator)) {}
Rational::Rational(Rational&& other) noexcept
    : numerator_(std::move(other.numerator_)),
      denominator_(std::move(other.denominator_)) {
  other.denominator_ = Integer(1);
}
Rational& Rational::operator=(Rational&& other) noexcept {
  if (this != &other) {
    numerator_ = std::move(other.numerator_);
    denominator_ = std::move(other.denominator_);
    other.denominator_ = Integer(1);
  }
  return *this;
}
Rational& Rational::operator=(const Rational& other) {
  if (this != &other) {
    Rational copy(other);
    *this = std::move(copy);
  }
  return *this;
}
Result<Rational> Rational::Make(IntegerView numerator,
                                IntegerView denominator) {
  if (denominator.Sign() == 0)
    return std::unexpected(NodeError{ErrorCode::kInvalidParameter});
  return GuardNumber([&] {
    CheckGmpLimbs(std::max(
        kWordImportLimbs,
        std::max(MagnitudeLimbs(numerator), MagnitudeLimbs(denominator)) + 1));
    GmpRational value;
    detail::NumericAccess::Set(mpq_numref(value.Get()), numerator);
    detail::NumericAccess::Set(mpq_denref(value.Get()), denominator);
    mpq_canonicalize(value.Get());
    return Rational(detail::NumericAccess::From(mpq_numref(value.Get())),
                    detail::NumericAccess::From(mpq_denref(value.Get())));
  });
}
Result<Rational> Rational::ParseDecimalFraction(std::string_view text) {
  const auto dot = text.find('.');
  if (dot == std::string_view::npos || dot == 0 || dot == text.size() - 1 ||
      !std::ranges::all_of(text.substr(0, dot), IsDigit) ||
      !std::ranges::all_of(text.substr(dot + 1), IsDigit))
    return std::unexpected(NodeError{ErrorCode::kInvalidLiteral});

  auto end = text.size();
  while (end > dot + 1 && text[end - 1] == '0') --end;
  const auto fractional_digits = end - dot - 1;
  return GuardNumber([&] {
    auto whole = text.substr(0, dot);
    auto fraction = text.substr(dot + 1, fractional_digits);
    if (const auto first = whole.find_first_not_of('0');
        first != std::string_view::npos) {
      whole.remove_prefix(first);
    } else {
      whole = {};
      const auto first_fraction = fraction.find_first_not_of('0');
      if (first_fraction == std::string_view::npos)
        return Rational(Integer{}, Integer(1));
      fraction.remove_prefix(first_fraction);
    }
    CheckGmpLimbs(StringImportLimbs(whole.size() + fraction.size(), 10));
    detail::NumericAllocationCheckpoint();
    std::string digits(whole);
    digits.append(fraction);
    GmpRational value;
    mpz_set_str(mpq_numref(value.Get()), digits.c_str(), 10);
    SetPowerOfTen(mpq_denref(value.Get()), fractional_digits);
    Canonicalize(value.Get());
    return Rational(detail::NumericAccess::From(mpq_numref(value.Get())),
                    detail::NumericAccess::From(mpq_denref(value.Get())));
  });
}
Result<Rational> Rational::Negate(RationalView value) {
  auto numerator = Integer::Negate(value.Numerator());
  if (!numerator) return std::unexpected(numerator.error());
  return GuardNumber([&] {
    return Rational(std::move(*numerator), value.Denominator().Copy());
  });
}
Result<Rational> Rational::Divide(RationalView dividend, RationalView divisor) {
  if (divisor.Numerator().Sign() == 0)
    return std::unexpected(NodeError{ErrorCode::kInvalidParameter});
  if (dividend.Numerator().Sign() == 0) return Rational(Integer{}, Integer(1));
  return GuardNumber([&] {
    CheckGmpLimbs(
        std::max({kWordImportLimbs, MagnitudeLimbs(dividend.Numerator()),
                  MagnitudeLimbs(dividend.Denominator()),
                  MagnitudeLimbs(divisor.Numerator()),
                  MagnitudeLimbs(divisor.Denominator())}));
    GmpRational left, right;
    SetRational(left.Get(), dividend);
    SetRational(right.Get(), divisor);
    auto* a = mpq_numref(left.Get());
    auto* b = mpq_denref(left.Get());
    auto* c = mpq_numref(right.Get());
    auto* d = mpq_denref(right.Get());
    const auto capacity = GmpLimbCapacity();
    if (mpz_size(a) + mpz_size(d) <= capacity &&
        mpz_size(b) + mpz_size(c) <= capacity) {
      mpq_div(left.Get(), left.Get(), right.Get());
    } else {
      // GMP cross-cancels before multiplying. Near capacity, check the
      // actual reduced factors to avoid rejecting a representable quotient.
      detail::BigInteger gcd;
      mpz_gcd(gcd.value, a, c);
      mpz_divexact(a, a, gcd.value);
      mpz_divexact(c, c, gcd.value);
      mpz_gcd(gcd.value, b, d);
      mpz_divexact(b, b, gcd.value);
      mpz_divexact(d, d, gcd.value);
      Multiply(a, a, d);
      Multiply(b, b, c);
      if (mpz_sgn(b) < 0) {
        mpz_neg(a, a);
        mpz_neg(b, b);
      }
    }
    return Rational(detail::NumericAccess::From(a),
                    detail::NumericAccess::From(b));
  });
}
RationalView Rational::Borrow() const& noexcept { return RationalView(this); }
RationalView::RationalView(const Rational* value) noexcept : value_(value) {}
IntegerView RationalView::Numerator() const noexcept {
  return value_->numerator_.Borrow();
}
IntegerView RationalView::Denominator() const noexcept {
  return value_->denominator_.Borrow();
}
Rational RationalView::Copy() const { return *value_; }

Result<FpEncoding> RoundRationalToFp(std::uint32_t exponent_width,
                                     std::uint32_t significand_width,
                                     RoundingMode mode, RationalView value) {
  if (exponent_width < 2 || significand_width < 2 || mode > RoundingMode::kRtz)
    return std::unexpected(NodeError{ErrorCode::kInvalidParameter});
  if (exponent_width >
      std::numeric_limits<std::uint32_t>::max() - significand_width)
    return std::unexpected(
        NodeError{ErrorCode::kResourceLimit, ResourceKind::kSize});
  const auto sign = value.Numerator().Sign();
  if (sign == 0) return FpEncoding{false, false, Integer{}, Integer{}};
  const bool negative = sign < 0;
  const bool nearest = mode == RoundingMode::kRne || mode == RoundingMode::kRna;
  const bool outward =
      negative ? mode == RoundingMode::kRtn : mode == RoundingMode::kRtp;
  const auto fraction_width = significand_width - 1;
  return GuardNumber([&] {
    CheckGmpLimbs(std::max({kWordImportLimbs, MagnitudeLimbs(value.Numerator()),
                            MagnitudeLimbs(value.Denominator())}));
    detail::BigInteger numerator, denominator, quotient, remainder;
    detail::NumericAccess::Set(numerator.value, value.Numerator());
    mpz_abs(numerator.value, numerator.value);
    detail::NumericAccess::Set(denominator.value, value.Denominator());
    const auto binade = FloorLogRatio(numerator.value, denominator.value);
    // At exponent width >= 64 both finite bounds are beyond any binade of
    // an input supported by GMP's limb representation.
    const auto max_binade = exponent_width < 64
                                ? (std::int64_t{1} << (exponent_width - 1)) - 1
                                : std::numeric_limits<std::int64_t>::max();
    const auto min_binade = 1 - max_binade;
    if (binade > max_binade)
      return FpOverflow(exponent_width, fraction_width, negative,
                        nearest || outward);
    if (exponent_width < 64 && binade < min_binade - fraction_width - 1)
      return FpEncoding{negative, false, Integer{}, Integer(outward ? 1 : 0)};

    auto quantum_binade = std::max(binade, min_binade);
    const auto scale =
        static_cast<std::int64_t>(fraction_width) - quantum_binade;
    if (scale >= 0)
      ShiftMagnitude(numerator.value, numerator.value,
                     static_cast<std::uint64_t>(scale));
    else
      ShiftMagnitude(denominator.value, denominator.value,
                     static_cast<std::uint64_t>(-scale));

    // The mpz quotient and remainder each fit within the input limb counts.
    mpz_tdiv_qr(quotient.value, remainder.value, numerator.value,
                denominator.value);
    if (mpz_sgn(remainder.value) != 0) {
      bool increment = outward;
      if (nearest) {
        // GMP's mpz_add and mpz_sub reserve an extra limb even without carry.
        CheckGmpLimbs(mpz_size(denominator.value) + 1);
        mpz_sub(denominator.value, denominator.value, remainder.value);
        const auto cmp = mpz_cmp(remainder.value, denominator.value);
        increment = cmp > 0 || (cmp == 0 && (mode == RoundingMode::kRna ||
                                             mpz_odd_p(quotient.value)));
      }
      if (increment) {
        CheckGmpLimbs(mpz_size(quotient.value) + 1);
        mpz_add_ui(quotient.value, quotient.value, 1);
      }
    }
    if (mpz_sizeinbase(quotient.value, 2) > significand_width) {
      mpz_tdiv_q_2exp(quotient.value, quotient.value, 1);
      ++quantum_binade;
    }
    if (quantum_binade > max_binade)
      return FpOverflow(exponent_width, fraction_width, negative, true);

    detail::BigInteger exponent;
    if (mpz_sizeinbase(quotient.value, 2) == significand_width) {
      mpz_clrbit(quotient.value, fraction_width);
      SetBit(exponent.value, exponent_width - 1);
      mpz_sub_ui(exponent.value, exponent.value, 1);
      detail::NumericAccess::Set(remainder.value, IntegerView(quantum_binade));
      CheckGmpLimbs(
          std::max(mpz_size(exponent.value), mpz_size(remainder.value)) + 1);
      mpz_add(exponent.value, exponent.value, remainder.value);
    }
    return FpEncoding{negative, false,
                      detail::NumericAccess::From(exponent.value),
                      detail::NumericAccess::From(quotient.value)};
  });
}
#ifdef DANDELION_NODE_TESTING
void NumericTestPeer::SetLimbCapacity(int limbs) noexcept {
  detail::gmp_limb_limit = limbs;
}
void NumericTestPeer::FailAllocationAfter(std::size_t successful) noexcept {
  detail::numeric_failure = successful;
}
void NumericTestPeer::ClearFailures() noexcept {
  detail::numeric_failure.reset();
}
#endif
}  // namespace dandelion

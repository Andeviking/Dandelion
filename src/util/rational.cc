module;
#include <cstdint>
#include <gmp.h>

module dandelion.util.numeric;
import :internal;
import std;

namespace dandelion::numeric::detail {

Rational RationalAccess::Word(std::int64_t numerator,
                              std::uint64_t denominator) noexcept {
  Rational result;
  result.numerator_ = numerator;
  result.denominator_ = denominator;
  return result;
}

Rational RationalAccess::Take(Z numerator, Z denominator) {
  const auto n = IntegerAccess::View(numerator.Get()).TryInt64();
  const auto d = IntegerAccess::View(denominator.Get()).TryUint64();
  if (n && d) return Word(*n, *d);
  CheckGmp(std::max(mpz_size(numerator.Get()), mpz_size(denominator.Get())));
  AllocationCheckpoint();
  auto holder = std::make_unique<BigRational>();
  // Transfer both buffers into the canonical mpq without copying limbs.
  mpz_swap(mpq_numref(holder->value), numerator.Get());
  mpz_swap(mpq_denref(holder->value), denominator.Get());
  Rational result;
  result.big_ = holder.release();
  result.denominator_ = 0;
  return result;
}

}  // namespace dandelion::numeric::detail

namespace dandelion::numeric {
namespace {

using namespace detail;

struct WordFraction {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  Uint128 numerator;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  Uint128 denominator;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  bool negative;
};

Uint128 Gcd128(Uint128 a, Uint128 b) noexcept {
  while (b != 0) {
    const auto remainder = a % b;
    a = b;
    b = remainder;
  }
  return a;
}

std::optional<std::int64_t> InlineNumerator(WordFraction fraction) noexcept {
  if (fraction.numerator > UINT64_MAX || fraction.denominator > UINT64_MAX)
    return {};
  return SignedWord(static_cast<std::uint64_t>(fraction.numerator),
                    fraction.negative);
}

Rational WideFraction(WordFraction fraction) {
  auto n = UnsignedZ(fraction.numerator);
  auto d = UnsignedZ(fraction.denominator);
  if (fraction.negative) mpz_neg(n.Get(), n.Get());
  return RationalAccess::Take(std::move(n), std::move(d));
}

Rational TakeFraction(WordFraction fraction) {
  if (const auto word = InlineNumerator(fraction))
    return RationalAccess::Word(
        *word, static_cast<std::uint64_t>(fraction.denominator));
  return WideFraction(fraction);
}

[[gnu::noinline]] NumericResult<Rational> PromoteFraction(
    WordFraction fraction) {
  return Checked([&] { return WideFraction(fraction); });
}

inline NumericResult<Rational> CheckedFraction(WordFraction fraction) {
  if (const auto word = InlineNumerator(fraction))
    return RationalAccess::Word(
        *word, static_cast<std::uint64_t>(fraction.denominator));
  [[unlikely]] return PromoteFraction(fraction);
}

enum class FractionOp { kAdd, kSub, kMul, kDiv };

template <FractionOp Op, class Word>
bool AddSubOverflows(Word a, Word b, Word& result) noexcept {
  if constexpr (Op == FractionOp::kAdd)
    return __builtin_add_overflow(a, b, &result);
  else
    return __builtin_sub_overflow(a, b, &result);
}

Uint128 WideMagnitude(Int128 value) noexcept {
  const auto bits = static_cast<Uint128>(value);
  return value < 0 ? Uint128{0} - bits : bits;
}

WordFraction ReduceWordSum(Int128 sum, std::uint64_t left, std::uint64_t right,
                           std::uint64_t common) noexcept {
  if (sum == 0) return {0, 1, false};
  const auto magnitude = WideMagnitude(sum);
  if (common == 1) return {magnitude, Uint128{left} * right, sum < 0};
  const auto reduction = WordGcd(
      magnitude <= UINT64_MAX ? static_cast<std::uint64_t>(magnitude)
                              : static_cast<std::uint64_t>(magnitude % common),
      common);
  if (reduction == 1) return {magnitude, Uint128{left} * right, sum < 0};
  const auto reduced = magnitude <= UINT64_MAX
                           ? static_cast<std::uint64_t>(magnitude) / reduction
                           : magnitude / reduction;
  return {reduced, Uint128{left} * (right / reduction), sum < 0};
}

template <FractionOp Op>
WordFraction WordProduct(std::int64_t n1, std::uint64_t d1, std::int64_t n2,
                         std::uint64_t d2) noexcept {
  auto a = Magnitude(n1), b = Magnitude(n2);
  if constexpr (Op == FractionOp::kDiv) std::swap(b, d2);
  if (const auto g = WordGcd(a, d2); g != 1) {
    a /= g;
    d2 /= g;
  }
  if (const auto g = WordGcd(b, d1); g != 1) {
    b /= g;
    d1 /= g;
  }
  return {Uint128{a} * b, Uint128{d1} * d2, (n1 < 0) != (n2 < 0)};
}

Rational WideArithmetic(IntegerView n1, IntegerView d1, IntegerView n2,
                        IntegerView d2, FractionOp op) {
  const ZView a(n1), b(d1), c(n2), d(d2);
  if (op == FractionOp::kAdd || op == FractionOp::kSub) {
    const auto gcd = GcdZ(b.Get(), d.Get());
    const auto left = ExactQuotient(b.Get(), gcd.Get());
    const auto right = ExactQuotient(d.Get(), gcd.Get());
    const auto x = MulZ(a.Get(), right.Get()), y = MulZ(c.Get(), left.Get());
    auto sum = AddZ(x.Get(), y.Get(), op == FractionOp::kSub);
    if (mpz_sgn(sum.Get()) == 0) return Rational();
    const auto reduction = GcdZ(sum.Get(), gcd.Get());
    auto numerator = ExactQuotient(sum.Get(), reduction.Get());
    const auto factor = ExactQuotient(d.Get(), reduction.Get());
    return RationalAccess::Take(std::move(numerator),
                                MulZ(left.Get(), factor.Get()));
  }
  const auto n = op == FractionOp::kMul ? c.Get() : d.Get();
  const auto den = op == FractionOp::kMul ? d.Get() : c.Get();
  const auto g1 = GcdZ(a.Get(), den), g2 = GcdZ(n, b.Get());
  const auto x1 = ExactQuotient(a.Get(), g1.Get()),
             y1 = ExactQuotient(den, g1.Get());
  const auto x2 = ExactQuotient(n, g2.Get()),
             y2 = ExactQuotient(b.Get(), g2.Get());
  auto numerator = MulZ(x1.Get(), x2.Get());
  auto denominator = MulZ(y1.Get(), y2.Get());
  if (mpz_sgn(denominator.Get()) < 0) {
    mpz_neg(numerator.Get(), numerator.Get());
    mpz_neg(denominator.Get(), denominator.Get());
  }
  return RationalAccess::Take(std::move(numerator), std::move(denominator));
}

[[gnu::noinline]] NumericResult<Rational> CheckedWideArithmetic(IntegerView n1,
                                                                IntegerView d1,
                                                                IntegerView n2,
                                                                IntegerView d2,
                                                                FractionOp op) {
  return Checked([&] { return WideArithmetic(n1, d1, n2, d2, op); });
}

template <FractionOp Op>
NumericResult<Rational> WordAddSub(std::int64_t n1, std::uint64_t d1,
                                   std::int64_t n2, std::uint64_t d2) {
  if (d1 == 1 && d2 == 1) {
    std::int64_t sum;
    if (!AddSubOverflows<Op>(n1, n2, sum)) return RationalAccess::Word(sum, 1);
  }
  if (std::has_single_bit(d1) && std::has_single_bit(d2)) {
    const auto left_exponent = std::countr_zero(d1);
    const auto right_exponent = std::countr_zero(d2);
    const auto exponent = std::max(left_exponent, right_exponent);
    // Only one operand can shift, by at most 63 bits. The sum fits in Int128.
    const auto x = Int128{n1} << (exponent - left_exponent);
    const auto y = Int128{n2} << (exponent - right_exponent);
    const auto sum = Op == FractionOp::kAdd ? x + y : x - y;
    // The denominator caps the reduction, including when the sum is zero.
    const auto denominator = std::uint64_t{1} << exponent;
    const auto reduction =
        std::countr_zero(static_cast<std::uint64_t>(sum) | denominator);
    const auto numerator = sum >> reduction;
    const auto reduced_denominator = denominator >> reduction;
    const auto word = static_cast<std::int64_t>(numerator);
    if (numerator == word)
      return RationalAccess::Word(word, reduced_denominator);
    [[unlikely]] return PromoteFraction(
        {WideMagnitude(numerator), reduced_denominator, numerator < 0});
  }
  if (d1 == d2) {
    const auto sum = Op == FractionOp::kAdd ? Int128{n1} + n2 : Int128{n1} - n2;
    return CheckedFraction(ReduceWordSum(sum, 1, d1, d1));
  }

  const auto common = WordGcd(d1, d2);
  const auto left = common == 1 ? d1 : d1 / common;
  const auto right = common == 1 ? d2 : d2 / common;
  Int128 sum;
  if (AddSubOverflows<Op>(Int128{n1} * right, Int128{n2} * left, sum))
      [[unlikely]] {
    return CheckedWideArithmetic(IntegerView(n1), IntegerView::FromUnsigned(d1),
                                 IntegerView(n2), IntegerView::FromUnsigned(d2),
                                 Op);
  }
  return CheckedFraction(ReduceWordSum(sum, left, d2, common));
}

template <FractionOp Op>
NumericResult<Rational> Arithmetic(RationalView a, RationalView b) {
  const auto n1 = a.Numerator(), n2 = b.Numerator();
  const auto x = IntegerAccess::TrySigned(n1), y = IntegerAccess::TrySigned(n2);
  if constexpr (Op == FractionOp::kDiv) {
    if (y == 0) [[unlikely]]
      return std::unexpected(NumericError::kDivisionByZero);
  }
  if constexpr (Op == FractionOp::kMul || Op == FractionOp::kDiv) {
    if (x == 0 || (Op == FractionOp::kMul && y == 0))
      return NumericResult<Rational>(std::in_place);
  }
  const auto d1 = a.Denominator(), d2 = b.Denominator();
  if (x && y) {
    const auto dx = IntegerAccess::TryUnsigned(d1);
    const auto dy = IntegerAccess::TryUnsigned(d2);
    if (dx && dy) {
      if constexpr (Op == FractionOp::kAdd || Op == FractionOp::kSub)
        return WordAddSub<Op>(*x, *dx, *y, *dy);
      else
        return CheckedFraction(WordProduct<Op>(*x, *dx, *y, *dy));
    }
  }
  [[unlikely]] return CheckedWideArithmetic(n1, d1, n2, d2, Op);
}

[[gnu::noinline]] NumericResult<Rational> MakeWide(IntegerView n,
                                                   IntegerView d) {
  return Checked([&] {
    const ZView numerator(n), denominator(d);
    const auto gcd = GcdZ(numerator.Get(), denominator.Get());
    auto reduced_d = ExactQuotient(denominator.Get(), gcd.Get());
    if (mpz_cmpabs(numerator.Get(), gcd.Get()) == 0 &&
        mpz_size(reduced_d.Get()) == 1) {
      const bool negative =
          mpz_sgn(numerator.Get()) != mpz_sgn(reduced_d.Get());
      return RationalAccess::Word(negative ? -1 : 1,
                                  mpz_getlimbn(reduced_d.Get(), 0));
    }
    auto reduced_n = ExactQuotient(numerator.Get(), gcd.Get());
    if (mpz_sgn(reduced_d.Get()) < 0) {
      mpz_neg(reduced_n.Get(), reduced_n.Get());
      mpz_neg(reduced_d.Get(), reduced_d.Get());
    }
    return RationalAccess::Take(std::move(reduced_n), std::move(reduced_d));
  });
}

enum class IntegralMode { kFloor, kCeil, kTrunc };

[[gnu::noinline]] NumericResult<Integer> WideIntegral(IntegerView n,
                                                      IntegerView d,
                                                      IntegralMode mode) {
  return Checked([&] {
    const ZView numerator(n), denominator(d);
    auto [q, r] = QuotientRemainder(numerator.Get(), denominator.Get());
    if ((mpz_sgn(r.Get()) < 0 && mode == IntegralMode::kFloor) ||
        (mpz_sgn(r.Get()) > 0 && mode == IntegralMode::kCeil)) {
      const ZView step(IntegerView(n.Sign()));
      q = AddZ(q.Get(), step.Get());
    }
    return IntegerAccess::Take(std::move(q));
  });
}

NumericResult<Integer> Integral(RationalView value, IntegralMode mode) {
  const auto n = value.Numerator(), d = value.Denominator();
  if (const auto word = IntegerAccess::TrySigned(n)) {
    if (const auto den = IntegerAccess::TryUnsigned(d)) {
      const auto magnitude = Magnitude(*word);
      auto quotient = magnitude / *den;
      if (magnitude % *den != 0 && (*word < 0 ? mode == IntegralMode::kFloor
                                              : mode == IntegralMode::kCeil))
        ++quotient;
      return NumericResult<Integer>(std::in_place,
                                    *SignedWord(quotient, *word < 0));
    }
  }
  [[unlikely]] return WideIntegral(n, d, mode);
}

}  // namespace

Rational::Rational(const Rational& other) : denominator_(other.denominator_) {
  if (denominator_ != 0) {
    numerator_ = other.numerator_;
  } else {
    AllocationCheckpoint();
    auto holder = std::make_unique<BigRational>();
    mpq_set(holder->value, other.big_->value);
    big_ = holder.release();
  }
}

Rational& Rational::operator=(const Rational& other) {
  if (this != &other) *this = Rational(other);
  return *this;
}

Rational& Rational::operator=(Rational&& other) noexcept {
  if (this != &other) {
    if (denominator_ == 0) delete big_;
    MoveFrom(other);
  }
  return *this;
}

void Rational::DeleteBig(detail::BigRational* value) noexcept { delete value; }

RationalView Rational::BorrowBig() const noexcept {
  const auto n = mpq_numref(big_->value);
  const auto d = mpq_denref(big_->value);
  auto denominator = IntegerAccess::View(d);
  if (const auto word = denominator.TryUint64())
    denominator = IntegerView::FromUnsigned(*word);
  return RationalView(
      IntegerAccess::View(n), denominator,
      sizeof(BigRational) + (mpz_size(n) + mpz_size(d)) * sizeof(mp_limb_t));
}

std::string Rational::ToString() const {
  const auto view = Borrow();
  auto output = view.Numerator().ToDecimal();
  if (!view.IsInteger()) {
    const auto denominator = view.Denominator().ToDecimal();
    const auto length = output.size() + denominator.size() + 1;
    if (length > output.capacity()) {
      AllocationCheckpoint();
      output.reserve(length);
    }
    output += '/';
    output += denominator;
  }
  return output;
}

bool RationalView::operator==(RationalView other) const noexcept {
  return Numerator() == other.Numerator() &&
         Denominator() == other.Denominator();
}

std::uint64_t RationalView::Hash() const noexcept {
  return Mix(Numerator().Hash(), Denominator().Hash());
}

Rational RationalView::Copy() const {
  const auto n = numerator_.TryInt64();
  const auto d = denominator_.TryUint64();
  if (n && d) return RationalAccess::Word(*n, *d);
  const ZView numerator(numerator_), denominator(denominator_);
  AllocationCheckpoint();
  auto holder = std::make_unique<BigRational>();
  mpz_set(mpq_numref(holder->value), numerator.Get());
  mpz_set(mpq_denref(holder->value), denominator.Get());
  Rational result;
  result.big_ = holder.release();
  result.denominator_ = 0;
  return result;
}

int RationalView::Compare(RationalView other) const {
  if (Sign() != other.Sign()) return Sign() - other.Sign();
  if (Sign() == 0) return 0;
  const auto lhs_n = numerator_.TryInt64(), rhs_n = other.numerator_.TryInt64();
  const auto lhs_d = denominator_.TryUint64(),
             rhs_d = other.denominator_.TryUint64();
  if (lhs_n && rhs_n && lhs_d && rhs_d) {
    const Int128 a = Int128{*lhs_n} * *rhs_d;
    const Int128 b = Int128{*rhs_n} * *lhs_d;
    return (a > b) - (a < b);
  }
  const ZView n1(Numerator()), d1(Denominator());
  const ZView n2(other.Numerator()), d2(other.Denominator());
  if (mpz_size(n1.Get()) + mpz_size(d2.Get()) <= kGmpLimbCapacity &&
      mpz_size(n2.Get()) + mpz_size(d1.Get()) <= kGmpLimbCapacity) {
    Z a, b;
    mpz_mul(a.Get(), n1.Get(), d2.Get());
    mpz_mul(b.Get(), n2.Get(), d1.Get());
    return mpz_cmp(a.Get(), b.Get());
  }

  // Continued fractions avoid products that exceed GMP's limb count.
  // A reciprocal reverses ordering after each equal integral quotient.
  int direction = Sign();
  Z a(n1.Get()), b(d1.Get()), c(n2.Get()), d(d2.Get());
  mpz_abs(a.Get(), a.Get());
  mpz_abs(c.Get(), c.Get());
  for (;;) {
    auto [q1, r1] = QuotientRemainder(a.Get(), b.Get(), kGmpLimbCapacity);
    auto [q2, r2] = QuotientRemainder(c.Get(), d.Get(), kGmpLimbCapacity);
    const int order = mpz_cmp(q1.Get(), q2.Get());
    if (order != 0) return direction * ((order > 0) - (order < 0));
    if (mpz_sgn(r1.Get()) == 0 || mpz_sgn(r2.Get()) == 0)
      return direction * (mpz_sgn(r1.Get()) - mpz_sgn(r2.Get()));
    a = std::move(b);
    b = std::move(r1);
    c = std::move(d);
    d = std::move(r2);
    direction = -direction;
  }
}

NumericResult<Rational> Rational::Make(IntegerView n, IntegerView d) {
  const auto denominator = IntegerAccess::TryMagnitude(d);
  if (denominator && denominator->magnitude == 0) [[unlikely]]
    return std::unexpected(NumericError::kDivisionByZero);
  const auto numerator = IntegerAccess::TryMagnitude(n);
  if (numerator && numerator->magnitude == 0)
    return NumericResult<Rational>(std::in_place);
  if (numerator && denominator) {
    auto a = numerator->magnitude, b = denominator->magnitude;
    const auto gcd = WordGcd(a, b);
    if (gcd != 1) {
      a /= gcd;
      b /= gcd;
    }
    return CheckedFraction(
        {a, b, numerator->negative != denominator->negative});
  }
  [[unlikely]] return MakeWide(n, d);
}

NumericResult<Rational> Rational::FromInteger(IntegerView value) {
  return Make(value, IntegerView(1));
}

NumericResult<Rational> Rational::ParseDecimalFraction(std::string_view text) {
  return Checked([&] {
    const auto dot = text.find('.');
    Require(dot != std::string_view::npos, NumericError::kInvalidText);
    const auto whole = text.substr(0, dot);
    auto tail = text.substr(dot + 1);
    ValidateDigits(whole, 10);
    ValidateDigits(tail, 10);
    const auto end = tail.find_last_not_of('0');
    if (end == std::string_view::npos) {
      const auto number = Unwrap(Integer::ParseDecimal(whole));
      return Unwrap(FromInteger(number.Borrow()));
    }
    tail = tail.substr(0, end + 1);

    Uint128 n = 0, d = 1;
    bool fits = true;
    for (auto digits : {whole, tail}) {
      for (const char ch : digits) {
        fits = fits && !__builtin_mul_overflow(n, 10, &n) &&
               !__builtin_add_overflow(n, Digit(ch), &n);
        if (!fits) break;
      }
    }
    for (std::size_t i = 0; fits && i < tail.size(); ++i)
      fits = !__builtin_mul_overflow(d, 10, &d);
    if (fits) {
      const auto g = Gcd128(n, d);
      return TakeFraction({n / g, d / g, false});
    }

    const auto integral = Unwrap(Integer::ParseDecimal(whole));
    const auto fraction = Unwrap(Integer::ParseDecimal(tail));
    const auto scale = Unwrap(Integer::Power(
        IntegerView(10), IntegerView::FromUnsigned(tail.size())));
    const auto scaled = Unwrap(integral.Borrow() * scale.Borrow());
    const auto sum = Unwrap(scaled.Borrow() + fraction.Borrow());
    return Unwrap(Make(sum.Borrow(), scale.Borrow()));
  });
}

NumericResult<Rational> Rational::Negate(RationalView value) {
  const auto n = value.Numerator(), d = value.Denominator();
  if (const auto word = IntegerAccess::TryMagnitude(n)) {
    if (const auto den = IntegerAccess::TryUnsigned(d))
      return CheckedFraction({word->magnitude, *den, !word->negative});
  }
  [[unlikely]] return Checked([&] {
    const ZView numerator(n), denominator(d);
    CheckGmp(std::max(mpz_size(numerator.Get()), mpz_size(denominator.Get())));
    Z negated;
    mpz_neg(negated.Get(), numerator.Get());
    return RationalAccess::Take(std::move(negated), Z(denominator.Get()));
  });
}

NumericResult<Rational> Rational::Abs(RationalView value) {
  const auto numerator = value.Numerator();
  if (const auto word = IntegerAccess::TrySigned(numerator)) {
    if (const auto den = IntegerAccess::TryUnsigned(value.Denominator()))
      return CheckedFraction({Magnitude(*word), *den, false});
  }
  if (numerator.Sign() < 0) return Negate(value);
  [[unlikely]] return Checked([&] { return value.Copy(); });
}

NumericResult<Integer> Rational::Floor(RationalView value) {
  return Integral(value, IntegralMode::kFloor);
}
NumericResult<Integer> Rational::Ceil(RationalView value) {
  return Integral(value, IntegralMode::kCeil);
}
NumericResult<Integer> Rational::Trunc(RationalView value) {
  return Integral(value, IntegralMode::kTrunc);
}

NumericResult<Integer> Rational::ToIntegerExact(RationalView value) {
  if (IntegerAccess::TryUnsigned(value.Denominator()) != 1) [[unlikely]]
    return std::unexpected(NumericError::kInvalidArgument);
  const auto numerator = value.Numerator();
  if (const auto word = IntegerAccess::TrySigned(numerator))
    return NumericResult<Integer>(std::in_place, *word);
  [[unlikely]] return Checked([&] { return numerator.Copy(); });
}

NumericResult<Rational> operator+(RationalView a, RationalView b) {
  return Arithmetic<FractionOp::kAdd>(a, b);
}
NumericResult<Rational> operator-(RationalView a, RationalView b) {
  return Arithmetic<FractionOp::kSub>(a, b);
}
NumericResult<Rational> operator*(RationalView a, RationalView b) {
  return Arithmetic<FractionOp::kMul>(a, b);
}
NumericResult<Rational> operator/(RationalView a, RationalView b) {
  return Arithmetic<FractionOp::kDiv>(a, b);
}

}  // namespace dandelion::numeric

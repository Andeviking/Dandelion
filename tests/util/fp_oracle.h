#ifndef DANDELION_TESTS_UTIL_FP_ORACLE_H
#define DANDELION_TESTS_UTIL_FP_ORACLE_H

#include <algorithm>
#include <compare>
#include <cstdint>
#include <numeric>
#include <utility>
#include <vector>

namespace dandelion::testing::util::fp_reference {

// The tiny formats and exercised rational samples keep all intermediates
// well within int64_t. This oracle never uses the numeric library.
class Fraction {
 public:
  explicit Fraction(std::int64_t numerator = 0, std::int64_t denominator = 1) {
    const auto common = std::gcd(numerator, denominator);
    numerator_ = numerator / common;
    denominator_ = denominator / common;
    if (denominator_ < 0) {
      numerator_ = -numerator_;
      denominator_ = -denominator_;
    }
  }

  std::int64_t Numerator() const { return numerator_; }
  std::int64_t Denominator() const { return denominator_; }

  friend Fraction operator-(Fraction a) {
    return Fraction(-a.numerator_, a.denominator_);
  }
  friend Fraction operator+(Fraction a, Fraction b) {
    return Fraction(
        a.numerator_ * b.denominator_ + b.numerator_ * a.denominator_,
        a.denominator_ * b.denominator_);
  }
  friend Fraction operator-(Fraction a, Fraction b) { return a + -b; }
  friend Fraction operator*(Fraction a, Fraction b) {
    return Fraction(a.numerator_ * b.numerator_,
                    a.denominator_ * b.denominator_);
  }
  friend Fraction operator/(Fraction a, Fraction b) {
    return Fraction(a.numerator_ * b.denominator_,
                    a.denominator_ * b.numerator_);
  }
  friend bool operator==(Fraction a, Fraction b) = default;
  friend auto operator<=>(Fraction a, Fraction b) {
    return (a.numerator_ * b.denominator_) <=> (b.numerator_ * a.denominator_);
  }

 private:
  std::int64_t numerator_;
  std::int64_t denominator_;
};

enum class Mode { kEven, kAway, kUp, kDown, kZero };

inline bool UseHigherMagnitude(Fraction position, Fraction midpoint,
                               bool high_is_even, bool negative, Mode mode) {
  switch (mode) {
    case Mode::kEven:
      return position > midpoint || (position == midpoint && high_is_even);
    case Mode::kAway:
      return position >= midpoint;
    case Mode::kUp:
      return !negative;
    case Mode::kDown:
      return negative;
    case Mode::kZero:
      return false;
  }
  std::unreachable();
}

inline std::int64_t RoundInteger(Fraction value, Mode mode) {
  const auto numerator = value.Numerator();
  const bool negative = numerator < 0;
  const auto n = negative ? -numerator : numerator;
  const auto d = value.Denominator();
  auto result = n / d;
  if (n % d != 0 && UseHigherMagnitude(Fraction(n % d, d), Fraction(1, 2),
                                       result % 2 != 0, negative, mode)) {
    ++result;
  }
  return negative ? -result : result;
}

class TinyFormat {
 public:
  explicit TinyFormat(int significand)
      : significand_(significand), stride_(1 << (significand - 1)) {
    for (int magnitude = 0; magnitude < Infinity(); ++magnitude) {
      const int exponent = magnitude / stride_;
      const int fraction = magnitude % stride_;
      // Bias is one in all three formats. Compute the field equation directly.
      const int coefficient = exponent == 0 ? fraction : stride_ + fraction;
      const int binade = exponent == 2 ? 2 : 1;
      grid_.emplace_back(binade * coefficient, stride_);
    }
    grid_.emplace_back(4);  // Virtual endpoint above the largest finite binade.
  }

  int Significand() const { return significand_; }
  int SignBit() const { return 4 * stride_; }
  int Infinity() const { return 3 * stride_; }
  int Nan() const { return Infinity() + 1; }
  int PatternCount() const { return 2 * SignBit(); }
  int FractionBits(int bits) const { return bits % stride_; }
  int ExponentBits(int bits) const { return (bits / stride_) % 4; }
  int Magnitude(int bits) const { return bits % SignBit(); }
  bool Negative(int bits) const { return (bits & SignBit()) != 0; }
  bool IsNan(int bits) const { return Magnitude(bits) > Infinity(); }
  bool IsInfinity(int bits) const { return Magnitude(bits) == Infinity(); }
  bool IsZero(int bits) const { return Magnitude(bits) == 0; }
  bool IsSubnormal(int bits) const {
    return ExponentBits(bits) == 0 && !IsZero(bits);
  }
  bool IsNormal(int bits) const {
    return ExponentBits(bits) == 1 || ExponentBits(bits) == 2;
  }
  bool IsFinite(int bits) const { return Magnitude(bits) < Infinity(); }
  int Canonical(int bits) const { return IsNan(bits) ? Nan() : bits; }
  int WithSign(int magnitude, bool negative) const {
    return magnitude | (negative ? SignBit() : 0);
  }

  int Abs(int bits) const { return IsNan(bits) ? Nan() : Magnitude(bits); }
  int Neg(int bits) const { return IsNan(bits) ? Nan() : bits ^ SignBit(); }

  Fraction Value(int bits) const {
    const auto magnitude = grid_[static_cast<std::size_t>(Magnitude(bits))];
    return Negative(bits) ? -magnitude : magnitude;
  }

  std::vector<int> Values() const {
    std::vector<int> result;
    for (int bits = 0; bits < PatternCount(); ++bits) {
      if (bits == Canonical(bits)) result.push_back(bits);
    }
    return result;
  }

  const std::vector<Fraction>& Grid() const { return grid_; }

  int Round(Fraction value, Mode mode, bool negative_zero = false) const {
    const bool negative =
        value < Fraction() || (value == Fraction() && negative_zero);
    const Fraction magnitude = negative ? -value : value;
    const auto upper = std::lower_bound(grid_.begin(), grid_.end(), magnitude);
    if (upper == grid_.end()) {
      // With excess beyond the virtual binade, inward modes still clamp.
      const bool inward = mode == Mode::kZero ||
                          (mode == Mode::kDown && !negative) ||
                          (mode == Mode::kUp && negative);
      return WithSign(Infinity() - (inward ? 1 : 0), negative);
    }
    const int high = static_cast<int>(upper - grid_.begin());
    if (*upper == magnitude && high != Infinity())
      return WithSign(high, negative);
    const int low = high - 1;
    const auto midpoint = (upper[-1] + *upper) / Fraction(2);
    const bool outward =
        UseHigherMagnitude(magnitude, midpoint, high % 2 == 0, negative, mode);
    return WithSign(outward ? high : low, negative);
  }

  int Add(int a, int b, Mode mode) const {
    if (IsNan(a) || IsNan(b)) return Nan();
    if (IsInfinity(a) && IsInfinity(b) && a != b) return Nan();
    if (IsInfinity(a)) return a;
    if (IsInfinity(b)) return b;
    return Round(
        Value(a) + Value(b), mode,
        SumZeroSign(IsZero(a) && IsZero(b), Negative(a), Negative(b), mode));
  }

  int Sub(int a, int b, Mode mode) const { return Add(a, Neg(b), mode); }

  int Mul(int a, int b, Mode mode) const {
    if (InvalidProduct(a, b)) return Nan();
    const bool negative = Negative(a) != Negative(b);
    if (IsInfinity(a) || IsInfinity(b)) return WithSign(Infinity(), negative);
    return Round(Value(a) * Value(b), mode, negative);
  }

  int Div(int a, int b, Mode mode) const {
    if (IsNan(a) || IsNan(b) || (IsInfinity(a) && IsInfinity(b)) ||
        (IsZero(a) && IsZero(b))) {
      return Nan();
    }
    const bool negative = Negative(a) != Negative(b);
    if (IsInfinity(a) || IsZero(b)) return WithSign(Infinity(), negative);
    if (IsInfinity(b)) return WithSign(0, negative);
    return Round(Value(a) / Value(b), mode, negative);
  }

  int Fma(int a, int b, int c, Mode mode) const {
    if (InvalidProduct(a, b) || IsNan(c)) return Nan();
    const bool negative_product = Negative(a) != Negative(b);
    if (IsInfinity(a) || IsInfinity(b)) {
      return IsInfinity(c) && Negative(c) != negative_product
                 ? Nan()
                 : WithSign(Infinity(), negative_product);
    }
    if (IsInfinity(c)) return c;
    const bool both_zero = (IsZero(a) || IsZero(b)) && IsZero(c);
    return Round(Value(a) * Value(b) + Value(c), mode,
                 SumZeroSign(both_zero, negative_product, Negative(c), mode));
  }

  int Sqrt(int a, Mode mode) const {
    if (IsNan(a)) return Nan();
    if (IsZero(a)) return a;
    if (Negative(a)) return Nan();
    if (IsInfinity(a)) return a;
    const Fraction value = Value(a);
    // Find the first candidate whose square brackets the exact radicand.
    for (std::size_t high = 1; high < grid_.size(); ++high) {
      const auto upper = grid_[high];
      if (upper * upper < value) continue;
      if (upper * upper == value) return static_cast<int>(high);
      const auto midpoint = (grid_[high - 1] + upper) / Fraction(2);
      const bool up = UseHigherMagnitude(value, midpoint * midpoint,
                                         high % 2 == 0, false, mode);
      return static_cast<int>(up ? high : high - 1);
    }
    std::unreachable();
  }

  int Rem(int a, int b) const {
    if (IsNan(a) || IsNan(b) || IsInfinity(a) || IsZero(b)) return Nan();
    if (IsInfinity(b)) return a;
    const auto quotient =
        Fraction(RoundInteger(Value(a) / Value(b), Mode::kEven));
    return Round(Value(a) - quotient * Value(b), Mode::kEven, Negative(a));
  }

  int RoundToIntegral(int a, Mode mode) const {
    if (!IsFinite(a) || IsZero(a)) return a;
    const auto integer = RoundInteger(Value(a), mode);
    const bool negative = Negative(a);
    if (integer == 4 || integer == -4) return WithSign(Infinity(), negative);
    return Round(Fraction(integer), Mode::kEven, negative);
  }

  int Compare(int a, int b) const {
    if (IsInfinity(a)) return a == b ? 0 : Negative(a) ? -1 : 1;
    if (IsInfinity(b)) return Negative(b) ? 1 : -1;
    return (Value(a) > Value(b)) - (Value(a) < Value(b));
  }

  int Convert(int a, const TinyFormat& destination, Mode mode) const {
    if (IsNan(a)) return destination.Nan();
    if (IsInfinity(a))
      return destination.WithSign(destination.Infinity(), Negative(a));
    return destination.Round(Value(a), mode, Negative(a));
  }

 private:
  static bool SumZeroSign(bool both_zero, bool negative_a, bool negative_b,
                          Mode mode) {
    if (both_zero && negative_a == negative_b) return negative_a;
    return mode == Mode::kDown;
  }

  bool InvalidProduct(int a, int b) const {
    return IsNan(a) || IsNan(b) || (IsZero(a) && IsInfinity(b)) ||
           (IsInfinity(a) && IsZero(b));
  }

  int significand_;
  int stride_;
  std::vector<Fraction> grid_;
};

}  // namespace dandelion::testing::util::fp_reference

#endif  // DANDELION_TESTS_UTIL_FP_ORACLE_H

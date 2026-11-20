export module dandelion.util.numeric:rational;
import :error;
import :integer;
import std;

namespace dandelion::numeric::detail {
struct BigRational;
struct RationalAccess;
}  // namespace dandelion::numeric::detail

export namespace dandelion::numeric {

class RationalView;

class Rational {
 public:
  inline Rational() noexcept = default;
  Rational(const Rational&);
  Rational& operator=(const Rational&);
  inline Rational(Rational&& other) noexcept { MoveFrom(other); }
  Rational& operator=(Rational&&) noexcept;
  inline ~Rational() noexcept {
    if (denominator_ == 0) DeleteBig(big_);
  }

  RationalView Borrow() const& noexcept;
  RationalView Borrow() const&& = delete;
  std::string ToString() const;

  static NumericResult<Rational> Make(IntegerView numerator,
                                      IntegerView denominator);
  static NumericResult<Rational> FromInteger(IntegerView value);
  static NumericResult<Rational> ParseDecimalFraction(std::string_view text);
  static NumericResult<Rational> Negate(RationalView value);
  static NumericResult<Rational> Abs(RationalView value);
  static NumericResult<Integer> Floor(RationalView value);
  static NumericResult<Integer> Ceil(RationalView value);
  static NumericResult<Integer> Trunc(RationalView value);
  static NumericResult<Integer> ToIntegerExact(RationalView value);

 private:
  friend class RationalView;
  friend struct detail::RationalAccess;
  static void DeleteBig(detail::BigRational* value) noexcept;
  RationalView BorrowBig() const noexcept;
  inline void MoveFrom(Rational& other) noexcept {
    denominator_ = std::exchange(other.denominator_, 1);
    if (denominator_ == 0)
      big_ = other.big_;
    else
      numerator_ = other.numerator_;
    other.numerator_ = 0;
  }

  union {
    std::int64_t numerator_ = 0;
    detail::BigRational* big_;
  };
  std::uint64_t denominator_ = 1;
};

// Keep the owners of any borrowed components alive and unchanged.
class RationalView {
 public:
  // The components must be reduced, with a positive denominator.
  static inline RationalView FromCanonicalComponents(
      IntegerView numerator, IntegerView denominator) noexcept {
    return RationalView(numerator, denominator, 0);
  }
  inline IntegerView Numerator() const noexcept { return numerator_; }
  inline IntegerView Denominator() const noexcept { return denominator_; }
  int Sign() const noexcept { return Numerator().Sign(); }
  bool IsInteger() const noexcept { return Denominator() == IntegerView(1); }
  int Compare(RationalView other) const;
  bool operator==(RationalView other) const noexcept;
  std::uint64_t Hash() const noexcept;
  std::size_t BackingBytes() const noexcept { return backing_bytes_; }
  Rational Copy() const;

 private:
  friend class Rational;
  inline RationalView(IntegerView numerator, IntegerView denominator,
                      std::size_t backing_bytes) noexcept
      : numerator_(numerator),
        denominator_(denominator),
        backing_bytes_(backing_bytes) {}
  IntegerView numerator_;
  IntegerView denominator_;
  std::size_t backing_bytes_;
};

inline RationalView Rational::Borrow() const& noexcept {
  if (denominator_ == 0) return BorrowBig();
  return RationalView::FromCanonicalComponents(
      IntegerView(numerator_), IntegerView::FromUnsigned(denominator_));
}

NumericResult<Rational> operator+(RationalView a, RationalView b);
NumericResult<Rational> operator-(RationalView a, RationalView b);
NumericResult<Rational> operator*(RationalView a, RationalView b);
NumericResult<Rational> operator/(RationalView a, RationalView b);

}  // namespace dandelion::numeric

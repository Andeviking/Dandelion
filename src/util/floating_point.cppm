export module dandelion.util.numeric:floating_point;
import :error;
import :integer;
import :rational;
import :bitvector;
import std;

namespace dandelion::numeric::detail {
struct FpAccess;
}

export namespace dandelion::numeric {

enum class FpCategory : std::uint8_t {
  kNaN,
  kPositiveInfinity,
  kNegativeInfinity,
  kPositiveZero,
  kNegativeZero,
  kFiniteBits,
};
enum class FpSpecial : std::uint8_t {
  kNaN = std::to_underlying(FpCategory::kNaN),
  kPositiveInfinity = std::to_underlying(FpCategory::kPositiveInfinity),
  kNegativeInfinity = std::to_underlying(FpCategory::kNegativeInfinity),
  kPositiveZero = std::to_underlying(FpCategory::kPositiveZero),
  kNegativeZero = std::to_underlying(FpCategory::kNegativeZero),
};
enum class RoundingMode : std::uint8_t { kRne, kRna, kRtp, kRtn, kRtz };
enum class FpOutcomeKind : std::uint8_t { kDefined, kTwoChoices, kUnspecified };

class FpFormat {
 public:
  static NumericResult<FpFormat> Make(IntegerView exponent,
                                      IntegerView significand);
  // Both widths must be at least two and their sum must fit in uint32_t.
  static FpFormat FromCanonicalWidths(std::uint32_t exponent,
                                      std::uint32_t significand) noexcept {
    return FpFormat(exponent, significand);
  }
  inline std::uint32_t ExponentWidth() const noexcept { return exponent_; }
  inline std::uint32_t SignificandWidth() const noexcept {
    return significand_;
  }
  inline std::uint32_t Width() const noexcept {
    return exponent_ + significand_;
  }
  bool operator==(const FpFormat&) const noexcept = default;

 private:
  friend class FloatingPointValue;
  inline FpFormat(std::uint32_t exponent, std::uint32_t significand) noexcept
      : exponent_(exponent), significand_(significand) {}
  std::uint32_t exponent_;
  std::uint32_t significand_;
};

template <class T>
class FpOutcome {
 public:
  using DestinationType =
      std::conditional_t<std::is_same_v<T, BitVector>, std::uint32_t, FpFormat>;

  static FpOutcome Unspecified(FpFormat destination)
    requires(!std::is_same_v<T, BitVector>)
  {
    return FpOutcome(destination);
  }

  FpOutcomeKind Kind() const noexcept {
    return static_cast<FpOutcomeKind>(value_.index());
  }
  const T& Value() const { return std::get<0>(value_); }
  const std::pair<T, T>& Choices() const { return std::get<1>(value_); }
  DestinationType Destination() const { return std::get<2>(value_); }

 private:
  friend struct detail::FpAccess;
  explicit FpOutcome(T value)
      : value_(std::in_place_index<0>, std::move(value)) {}
  FpOutcome(T a, T b)
      : value_(std::in_place_index<1>, std::move(a), std::move(b)) {}
  explicit FpOutcome(DestinationType destination)
      : value_(std::in_place_index<2>, destination) {}
  std::variant<T, std::pair<T, T>, DestinationType> value_;
};

class FloatingPointView;

class FloatingPointValue {
 public:
  inline FloatingPointValue() noexcept = default;
  FloatingPointValue(const FloatingPointValue&);
  FloatingPointValue& operator=(const FloatingPointValue&);
  inline FloatingPointValue(FloatingPointValue&& other) noexcept {
    MoveFrom(other);
  }
  FloatingPointValue& operator=(FloatingPointValue&&) noexcept;
  inline ~FloatingPointValue() noexcept {
    if (limb_count_ != 0) delete[] limbs_;
  }

  FloatingPointView Borrow() const& noexcept;
  FloatingPointView Borrow() const&& = delete;
  FpFormat Format() const noexcept { return format_; }
  FpCategory Category() const noexcept { return category_; }
  bool SignBit() const noexcept;
  bool IsNaN() const noexcept { return category_ == FpCategory::kNaN; }
  bool IsZero() const noexcept;
  bool IsInfinite() const noexcept;
  bool IsNormal() const noexcept;
  bool IsSubnormal() const noexcept;
  bool IsNegative() const noexcept { return SignBit(); }
  bool IsPositive() const noexcept { return !IsNaN() && !SignBit(); }
  std::uint64_t Hash() const noexcept;
  std::size_t BackingBytes() const noexcept;
  bool operator==(const FloatingPointValue& other) const noexcept;
  NumericResult<BitVector> ExponentBits() const;
  NumericResult<BitVector> FractionBits() const;
  NumericResult<BitVector> ToBits() const;
  NumericResult<Rational> ToRational() const;

  static NumericResult<FloatingPointValue> FromBits(FpFormat format,
                                                    const BitVector& bits);
  static NumericResult<FloatingPointValue> FromBits(FpFormat format,
                                                    IntegerView bits);
  static NumericResult<FloatingPointValue> FromFields(
      const BitVector& sign, const BitVector& exponent,
      const BitVector& fraction);
  static NumericResult<FloatingPointValue> Special(FpFormat format,
                                                   FpCategory category);
  static NumericResult<FloatingPointValue> Abs(const FloatingPointValue& value);
  static NumericResult<FloatingPointValue> Neg(const FloatingPointValue& value);
  static NumericResult<FloatingPointValue> Add(RoundingMode mode,
                                               const FloatingPointValue& a,
                                               const FloatingPointValue& b);
  static NumericResult<FloatingPointValue> Sub(RoundingMode mode,
                                               const FloatingPointValue& a,
                                               const FloatingPointValue& b);
  static NumericResult<FloatingPointValue> Mul(RoundingMode mode,
                                               const FloatingPointValue& a,
                                               const FloatingPointValue& b);
  static NumericResult<FloatingPointValue> Div(RoundingMode mode,
                                               const FloatingPointValue& a,
                                               const FloatingPointValue& b);
  static NumericResult<FloatingPointValue> Fma(RoundingMode mode,
                                               const FloatingPointValue& a,
                                               const FloatingPointValue& b,
                                               const FloatingPointValue& c);
  static NumericResult<FloatingPointValue> Sqrt(
      RoundingMode mode, const FloatingPointValue& value);
  static NumericResult<FloatingPointValue> RoundToIntegral(
      RoundingMode mode, const FloatingPointValue& value);
  static NumericResult<FloatingPointValue> Rem(const FloatingPointValue& a,
                                               const FloatingPointValue& b);
  static NumericResult<bool> Eq(const FloatingPointValue& a,
                                const FloatingPointValue& b);
  static NumericResult<bool> Lt(const FloatingPointValue& a,
                                const FloatingPointValue& b);
  static NumericResult<bool> Leq(const FloatingPointValue& a,
                                 const FloatingPointValue& b);
  static NumericResult<bool> Gt(const FloatingPointValue& a,
                                const FloatingPointValue& b);
  static NumericResult<bool> Geq(const FloatingPointValue& a,
                                 const FloatingPointValue& b);
  static NumericResult<FpOutcome<FloatingPointValue>> Min(
      const FloatingPointValue& a, const FloatingPointValue& b);
  static NumericResult<FpOutcome<FloatingPointValue>> Max(
      const FloatingPointValue& a, const FloatingPointValue& b);

  static NumericResult<FloatingPointValue> Convert(
      FpFormat destination, RoundingMode mode, const FloatingPointValue& value);
  static NumericResult<FloatingPointValue> FromSignedBv(FpFormat destination,
                                                        RoundingMode mode,
                                                        const BitVector& value);
  static NumericResult<FloatingPointValue> FromUnsignedBv(
      FpFormat destination, RoundingMode mode, const BitVector& value);
  static NumericResult<FpOutcome<BitVector>> ToSignedBv(
      IntegerView width, RoundingMode mode, const FloatingPointValue& value);
  static NumericResult<FpOutcome<BitVector>> ToUnsignedBv(
      IntegerView width, RoundingMode mode, const FloatingPointValue& value);

 private:
  friend struct detail::FpAccess;
  inline void MoveFrom(FloatingPointValue& other) noexcept {
    format_ = std::exchange(other.format_, FpFormat(2, 2));
    category_ = std::exchange(other.category_, FpCategory::kPositiveZero);
    limb_count_ = std::exchange(other.limb_count_, 0);
    if (limb_count_ != 0)
      limbs_ = other.limbs_;
    else
      word_ = other.word_;
    other.word_ = 0;
  }

  FpFormat format_{2, 2};
  std::uint32_t limb_count_ = 0;
  FpCategory category_ = FpCategory::kPositiveZero;
  union {
    std::uint64_t word_ = 0;
    std::uint64_t* limbs_;
  };
};

// Keep the backing storage for a wide finite view alive and unchanged.
class FloatingPointView {
 public:
  static FloatingPointView FromCanonicalSpecial(FpFormat format,
                                                FpSpecial special) noexcept;
  // The bits must have format.Width() bits and encode a finite nonzero value.
  static FloatingPointView FromCanonicalBits(FpFormat format,
                                             BitVectorView bits) noexcept;
  FpFormat Format() const noexcept { return format_; }
  FpCategory Category() const noexcept { return category_; }
  std::optional<BitVectorView> FiniteBits() const noexcept { return bits_; }
  bool operator==(const FloatingPointView&) const noexcept = default;
  FloatingPointValue Copy() const;

 private:
  FloatingPointView(FpFormat format, FpCategory category,
                    std::optional<BitVectorView> bits) noexcept
      : format_(format), category_(category), bits_(bits) {}
  FpFormat format_;
  FpCategory category_;
  std::optional<BitVectorView> bits_;
};

NumericResult<FloatingPointValue> RoundRationalToFp(FpFormat destination,
                                                    RoundingMode mode,
                                                    RationalView value);

}  // namespace dandelion::numeric

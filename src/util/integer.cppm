export module dandelion.util.numeric:integer;
import :error;
import std;

namespace dandelion::numeric::detail {
struct BigInteger;
struct IntegerAccess;
}  // namespace dandelion::numeric::detail

export namespace dandelion::numeric {

class IntegerView;

class Integer {
 public:
  explicit inline Integer(std::int64_t value = 0) noexcept : small_(value) {}
  Integer(const Integer&);
  Integer& operator=(const Integer&);
  inline Integer(Integer&& other) noexcept
      : small_(std::exchange(other.small_, 0)),
        big_(std::exchange(other.big_, nullptr)) {}
  Integer& operator=(Integer&&) noexcept;
  inline ~Integer() noexcept {
    if (big_) DeleteBig(big_);
  }

  IntegerView Borrow() const& noexcept;
  IntegerView Borrow() const&& = delete;
  std::string ToDecimal() const;

  static NumericResult<Integer> FromUnsigned(std::uint64_t value);
  static NumericResult<Integer> ParseDecimal(std::string_view text);
  static NumericResult<Integer> ParseRadix(std::string_view digits, int radix);
  static NumericResult<Integer> Negate(IntegerView value);
  static NumericResult<Integer> Abs(IntegerView value);
  static NumericResult<Integer> Gcd(IntegerView a, IntegerView b);
  static NumericResult<Integer> Lcm(IntegerView a, IntegerView b);
  static NumericResult<std::pair<Integer, Integer>> DivMod(IntegerView a,
                                                           IntegerView b);
  static NumericResult<Integer> ExactDiv(IntegerView a, IntegerView b);
  static NumericResult<bool> IsDivisible(IntegerView a, IntegerView b);
  static NumericResult<Integer> Power(IntegerView base, IntegerView exponent);

 private:
  friend class IntegerView;
  friend struct detail::IntegerAccess;
  static void DeleteBig(detail::BigInteger* value) noexcept;
  IntegerView BorrowBig() const noexcept;
  std::int64_t small_ = 0;
  detail::BigInteger* big_ = nullptr;
};

// Keep the owner of borrowed storage alive and unchanged.
class IntegerView {
 public:
  explicit inline IntegerView(std::int64_t value = 0) noexcept
      : signed_word_(value) {}
  static inline IntegerView FromUnsigned(std::uint64_t value) noexcept {
    IntegerView result;
    result.form_ = Form::kUnsigned;
    result.unsigned_word_ = value;
    return result;
  }
  // Borrow a nonzero, trimmed magnitude in increasing order of significance.
  // The limb count must fit in a signed int.
  static IntegerView FromCanonicalMagnitude(
      std::span<const std::uint64_t> limbs, bool negative) noexcept;
  bool IsSmall() const noexcept;
  bool IsOdd() const noexcept;
  int Sign() const noexcept;
  int Compare(IntegerView other) const noexcept;
  bool operator==(IntegerView other) const noexcept {
    return Compare(other) == 0;
  }
  std::uint64_t BitLength() const noexcept;
  std::uint64_t MagnitudeChunk(std::uint64_t index) const noexcept;
  // The borrowed magnitude, or an empty span for an inline word.
  std::span<const std::uint64_t> MagnitudeLimbs() const noexcept;
  std::uint64_t Hash() const noexcept;
  std::optional<std::int64_t> TryInt64() const noexcept;
  std::optional<std::uint64_t> TryUint64() const noexcept;
  std::optional<std::uint32_t> TryUint32() const noexcept;
  std::size_t BackingBytes() const noexcept;
  std::string ToDecimal() const;
  Integer Copy() const;

 private:
  friend struct detail::IntegerAccess;
  enum class Form : std::uint8_t {
    kSigned,
    kUnsigned,
    kBig,
    kPackedPositive,
    kPackedNegative,
  };
  Form form_ = Form::kSigned;
  std::uint32_t limb_count_ = 0;
  union {
    std::int64_t signed_word_;
    std::uint64_t unsigned_word_;
    const void* big_;
    const std::uint64_t* limbs_;
  };
};

inline IntegerView Integer::Borrow() const& noexcept {
  return big_ ? BorrowBig() : IntegerView(small_);
}

NumericResult<Integer> operator+(IntegerView a, IntegerView b);
NumericResult<Integer> operator-(IntegerView a, IntegerView b);
NumericResult<Integer> operator*(IntegerView a, IntegerView b);
NumericResult<Integer> operator-(IntegerView value);

}  // namespace dandelion::numeric

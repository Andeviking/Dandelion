export module dandelion.numeric;
import std;

namespace dandelion::detail {
struct BigInteger;
struct NumericAccess;
}  // namespace dandelion::detail

export namespace dandelion {

enum class ErrorCode : std::uint8_t {
  kNullObject,
  kForeignManager,
  kInvalidKind,
  kInvalidArity,
  kInvalidParameter,
  kSortMismatch,
  kInvalidLiteral,
  kInvalidDatatype,
  kBuilderClosed,
  kInvalidTraversalState,
  kUnsupportedFeature,
  kResourceLimit,
};
enum class ResourceKind : std::uint8_t {
  kNone,
  kCppAllocation,
  kSize,
  kIdentitySpace,
  kReferenceCount,
};
enum class DatatypeReason : std::uint8_t {
  kNone,
  kEmptyGroup,
  kNoConstructor,
  kInvalidParameterScope,
  kNestedRecursiveSort,
  kNonproductive,
};
struct NodeError {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  ErrorCode code;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  ResourceKind resource = ResourceKind::kNone;
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t operand_index = std::numeric_limits<std::uint64_t>::max();
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  std::uint64_t parameter_index = std::numeric_limits<std::uint64_t>::max();
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  DatatypeReason datatype_reason = DatatypeReason::kNone;
};
template <class T>
using Result = std::expected<T, NodeError>;

class ReferenceLimit final : public std::exception {
 public:
  const char* what() const noexcept override;
};

class IntegerView;
class Integer {
 public:
  explicit Integer(std::int64_t value = 0) noexcept;
  Integer(const Integer& other);
  Integer& operator=(const Integer& other);
  Integer(Integer&& other) noexcept;
  Integer& operator=(Integer&& other) noexcept;
  ~Integer() noexcept;
  static Result<Integer> ParseDecimal(std::string_view text);
  static Result<Integer> ParseRadix(std::string_view digits, int radix);
  static Result<Integer> Negate(IntegerView value);
  IntegerView Borrow() const& noexcept;
  IntegerView Borrow() const&& = delete;
  std::string ToDecimal() const;

 private:
  friend class IntegerView;
  friend struct detail::NumericAccess;
  std::int64_t small_ = 0;
  detail::BigInteger* big_ = nullptr;
};

// A wide view borrows backing storage. Keep its owner alive and unchanged.
class IntegerView {
 public:
  explicit IntegerView(std::int64_t value = 0) noexcept;
  static IntegerView FromUnsigned(std::uint64_t value) noexcept;
  bool IsSmall() const noexcept;
  int Sign() const noexcept;
  int Compare(IntegerView other) const noexcept;
  std::uint64_t BitLength() const noexcept;
  std::uint64_t MagnitudeChunk(std::uint64_t index) const noexcept;
  std::uint64_t Hash() const noexcept;
  std::optional<std::int64_t> TryInt64() const noexcept;
  std::optional<std::uint64_t> TryUint64() const noexcept;
  std::optional<std::uint32_t> TryUint32() const noexcept;
  std::string ToDecimal() const;
  Integer Copy() const;
  // Size of the owned GMP object and significant limbs, if any.
  std::size_t BackingBytes() const noexcept;

 private:
  friend class Integer;
  friend struct detail::NumericAccess;
  enum class Form : std::uint8_t { kSigned, kUnsigned, kBig };
  Form form_ = Form::kSigned;
  union {
    std::int64_t signed_word_;
    std::uint64_t unsigned_word_;
    const detail::BigInteger* big_;
  };
};

class RationalView;
class Rational {
 public:
  static Result<Rational> Make(IntegerView numerator, IntegerView denominator);
  static Result<Rational> ParseDecimalFraction(std::string_view text);
  static Result<Rational> Negate(RationalView value);
  static Result<Rational> Divide(RationalView dividend, RationalView divisor);
  Rational(const Rational&) = default;
  Rational& operator=(const Rational& other);
  Rational(Rational&& other) noexcept;
  Rational& operator=(Rational&& other) noexcept;
  RationalView Borrow() const& noexcept;
  RationalView Borrow() const&& = delete;

 private:
  friend class RationalView;
  Rational(Integer numerator, Integer denominator) noexcept;
  Integer numerator_;
  Integer denominator_;
};

// Keep the owning Rational alive and unchanged while using the view.
class RationalView {
 public:
  IntegerView Numerator() const noexcept;
  IntegerView Denominator() const noexcept;
  Rational Copy() const;

 private:
  friend class Rational;
  explicit RationalView(const Rational* value) noexcept;
  const Rational* value_;
};

enum class RoundingMode : std::uint8_t { kRne, kRna, kRtp, kRtn, kRtz };
struct FpEncoding {
  // NOLINTBEGIN(readability-identifier-naming): Struct fields, snake_case.
  bool negative;
  bool infinite;
  Integer exponent;
  Integer fraction;
  // NOLINTEND(readability-identifier-naming)
};
// A rounded rational is finite or an infinity. For infinity the two integer
// fields are unused. Finite fields are the unsigned stored exponent and
// trailing significand, without any sign or implicit leading bit.
Result<FpEncoding> RoundRationalToFp(std::uint32_t exponent_width,
                                     std::uint32_t significand_width,
                                     RoundingMode mode, RationalView value);

#ifdef DANDELION_NODE_TESTING
class NumericTestPeer {
 public:
  static void SetLimbCapacity(int limbs) noexcept;
  static void FailAllocationAfter(std::size_t successful) noexcept;
  static void ClearFailures() noexcept;
};
#endif

}  // namespace dandelion

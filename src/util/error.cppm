export module dandelion.util.numeric:error;
import std;

export namespace dandelion::numeric {

enum class NumericError : std::uint8_t {
  kInvalidText,
  kInvalidArgument,
  kValueOutOfRange,
  kDivisionByZero,
  kSizeLimit,
  kCppAllocation,
};

template <class T>
using NumericResult = std::expected<T, NumericError>;

}  // namespace dandelion::numeric

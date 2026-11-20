#ifndef DANDELION_TESTS_UTIL_NUMERIC_TEST_PEER_H
#define DANDELION_TESTS_UTIL_NUMERIC_TEST_PEER_H

// Include after importing std and dandelion.util.numeric.
extern "C++" {
namespace dandelion::numeric {

class NumericTestPeer {
 public:
  static void FailAllocationAfter(std::size_t successful) noexcept;
  static std::size_t AllocationAttempts() noexcept;
  static void ClearFailures() noexcept;
  static void SetLimbCapacity(std::uint64_t limbs) noexcept;
  static void SetBitVectorLimbCapacity(std::uint64_t limbs) noexcept;
  static void SetByteCapacity(std::size_t bytes) noexcept;
  static std::uint64_t GmpLimbBits() noexcept;
  static NumericResult<void> CheckGmpWorkingLimbs(std::uint64_t limbs,
                                                  std::uint64_t spare);
  static void ForcePortableBitVector(bool force) noexcept;
  static const void* BackingAddress(const Integer&) noexcept;
  static const void* BackingAddress(const Rational&) noexcept;
  static const void* BackingAddress(const BitVector&) noexcept;
  static const void* BackingAddress(const FloatingPointValue&) noexcept;
};

}  // namespace dandelion::numeric
}  // extern "C++"

#endif

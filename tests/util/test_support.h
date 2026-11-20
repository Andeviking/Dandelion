#ifndef DANDELION_TESTS_UTIL_TEST_SUPPORT_H
#define DANDELION_TESTS_UTIL_TEST_SUPPORT_H

#include <gtest/gtest.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

import std;
import dandelion.util.numeric;

#include "numeric_test_peer.h"

namespace dandelion::testing::util {

using dandelion::numeric::BitVector;
using dandelion::numeric::FloatingPointValue;
using dandelion::numeric::FpCategory;
using dandelion::numeric::FpFormat;
using dandelion::numeric::FpOutcome;
using dandelion::numeric::FpOutcomeKind;
using dandelion::numeric::Integer;
using dandelion::numeric::IntegerView;
using dandelion::numeric::NumericError;
using dandelion::numeric::NumericResult;
using dandelion::numeric::NumericTestPeer;
using dandelion::numeric::Rational;
using dandelion::numeric::RoundingMode;
using dandelion::numeric::RoundRationalToFp;

template <class T>
T Take(NumericResult<T> result) {
  if (!result) {
    ADD_FAILURE() << "Unexpected numeric error "
                  << static_cast<int>(result.error());
    throw std::runtime_error("Unexpected numeric error");
  }
  return std::move(*result);
}

template <class T>
void ExpectError(const NumericResult<T>& result, NumericError error) {
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error);
}

inline std::optional<std::int64_t> Int64(NumericResult<Integer> result) {
  const auto value = Take(std::move(result));
  return value.Borrow().TryInt64();
}

inline Integer Int(std::string_view text) {
  return Take(Integer::ParseDecimal(text));
}

inline IntegerView Unsigned(std::uint64_t value) {
  return IntegerView::FromUnsigned(value);
}

inline Rational Ratio(std::int64_t numerator, std::int64_t denominator = 1) {
  return Take(Rational::Make(IntegerView(numerator), IntegerView(denominator)));
}

inline BitVector Bits(std::uint32_t width, std::uint64_t value) {
  return Take(BitVector::FromUint64(Unsigned(width), value));
}

inline void ExpectBits(const BitVector& bits, std::uint32_t width,
                       std::uint64_t value) {
  EXPECT_EQ(bits.Width(), width);
  EXPECT_EQ(bits.TryUint64(), value);
}

template <class T>
inline constexpr bool kNothrowOwnership =
    std::is_nothrow_move_constructible_v<T> &&
    std::is_nothrow_move_assignable_v<T> && std::is_nothrow_destructible_v<T>;

template <class T>
void CheckSelfAssignment(T& value) {
  const auto& copy_alias = value;
  value = copy_alias;
  auto& move_alias = value;
  value = std::move(move_alias);
}

template <class T>
concept RvalueBorrowable = requires(T&& value) { std::move(value).Borrow(); };

}  // namespace dandelion::testing::util

#endif  // DANDELION_TESTS_UTIL_TEST_SUPPORT_H

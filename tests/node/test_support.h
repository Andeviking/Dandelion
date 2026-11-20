#ifndef DANDELION_TESTS_NODE_TEST_SUPPORT_H
#define DANDELION_TESTS_NODE_TEST_SUPPORT_H

#include <gtest/gtest.h>

import std;
import dandelion.node;
import dandelion.util.numeric;

#include "node_test_peer.h"

namespace dandelion::testing {

using numeric::BitVector;
using numeric::BitVectorView;
using numeric::FloatingPointValue;
using numeric::FloatingPointView;
using numeric::FpCategory;
using numeric::FpFormat;
using numeric::FpSpecial;
using numeric::Integer;
using numeric::IntegerView;
using numeric::NumericError;
using numeric::NumericResult;
using numeric::Rational;
using numeric::RationalView;
using numeric::RoundingMode;

template <class T>
T Take(NumericResult<T> result) {
  if (!result) {
    ADD_FAILURE() << "Unexpected numeric error "
                  << static_cast<int>(result.error());
    throw std::runtime_error("Unexpected numeric factory failure");
  }
  return std::move(*result);
}

template <class T>
void Error(const NumericResult<T>& result, NumericError error) {
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error(), error);
}

template <class T>
T Take(Result<T> result) {
  if (!result) {
    ADD_FAILURE() << "Unexpected node error "
                  << static_cast<int>(result.error().code);
    throw std::runtime_error("Unexpected node factory failure");
  }
  if constexpr (std::is_void_v<T>) {
    return;
  } else {
    return std::move(*result);
  }
}

template <class T>
void Error(const Result<T>& result, ErrorCode code) {
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, code);
}

template <class T>
void Resource(const Result<T>& result, ResourceKind resource) {
  ASSERT_FALSE(result.has_value());
  EXPECT_EQ(result.error().code, ErrorCode::kResourceLimit);
  EXPECT_EQ(result.error().resource, resource);
}

// Use only within the same full expression when the owner is a temporary.
// The product API intentionally deletes Borrow() on rvalues.
template <class T>
auto View(const T& owner) noexcept {
  return owner.Borrow();
}

template <class Actual, class Expected>
void ExpectOrderedViews(const Actual& actual, const Expected& expected) {
  ASSERT_EQ(actual.size(), expected.size());
  for (std::size_t i = 0; i < expected.size(); ++i)
    EXPECT_EQ(actual[i], expected[i]) << i;
}

inline Integer Number(std::string_view text) {
  return Take(Integer::ParseDecimal(text));
}

inline BitVector ExactBv(std::uint32_t width, IntegerView value) {
  return Take(BitVector::FromIntegerExact(IntegerView(width), value));
}

inline FpFormat Format(std::uint32_t exponent, std::uint32_t significand) {
  return Take(FpFormat::Make(IntegerView(exponent), IntegerView(significand)));
}

inline FloatingPointValue FpBits(FpFormat format, IntegerView bits) {
  return Take(FloatingPointValue::FromBits(format, bits));
}

inline FloatingPointValue SpecialFp(FpFormat format, FpSpecial special) {
  return Take(
      FloatingPointValue::Special(format, static_cast<FpCategory>(special)));
}

inline std::vector<NodeView> ExternalRetainChildren(NodeView first,
                                                    NodeView last) {
  std::vector<NodeView> children(65536, first);
  children[32768] = last;
  children.back() = last;
  return children;
}

class NodeTest : public ::testing::Test {
 protected:
  NodeManager manager_ = Take(NodeManager::Create());

  Sort Bv(std::int64_t width) {
    const Integer value(width);
    return Take(manager_.MakeBvSort(value.Borrow()));
  }
  Sort Fp(std::int64_t exponent, std::int64_t significand) {
    const Integer e(exponent);
    const Integer s(significand);
    return Take(manager_.MakeFpSort(e.Borrow(), s.Borrow()));
  }
  Node Int(std::int64_t value) {
    const Integer number(value);
    return Take(manager_.MakeInt(number.Borrow()));
  }
  Node Const(SortView sort, std::string_view name = "x") {
    return Take(manager_.MakeFreshConstant(name, sort));
  }
  Node Op(Kind kind, std::initializer_list<NodeView> children,
          std::span<const Integer> indices = {},
          std::optional<SortView> qualifier = std::nullopt,
          std::optional<RationalView> rational = std::nullopt) {
    return Take(manager_.MakeBuiltin({kind, indices, qualifier, rational},
                                     {children.begin(), children.size()}));
  }
};

}  // namespace dandelion::testing
#endif

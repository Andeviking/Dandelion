#ifndef DANDELION_TESTS_NODE_TEST_SUPPORT_H
#define DANDELION_TESTS_NODE_TEST_SUPPORT_H

#include <gtest/gtest.h>

import std;
import dandelion.node;

namespace dandelion::testing {

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

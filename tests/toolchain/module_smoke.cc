// This executable tests C++23, named modules, and the standard library module.
#include <gtest/gtest.h>

import std;
import dandelion.toolchain;

namespace {

enum class SampleValue { kFirst = 3, kSecond = 5, kThird = 8 };

}  // namespace

TEST(Toolchain, ModulesAndStandardLibrary) {
  constexpr std::array values = {std::to_underlying(SampleValue::kFirst),
                                 std::to_underlying(SampleValue::kSecond),
                                 std::to_underlying(SampleValue::kThird)};
  const int sum = dandelion::toolchain::SumValues(values);
  EXPECT_EQ(sum, 16);
}

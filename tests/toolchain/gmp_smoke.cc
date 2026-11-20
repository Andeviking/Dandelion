#include <gmp.h>
#include <gtest/gtest.h>

#include <cstdio>

#if __GNU_MP_VERSION < 6 || \
    (__GNU_MP_VERSION == 6 && __GNU_MP_VERSION_MINOR < 3)
#error "Dandelion requires GMP 6.3.0 or newer."
#endif

namespace {

class GmpIntegerTest : public ::testing::Test {
 protected:
  void SetUp() override { mpz_init(value_); }
  void TearDown() override { mpz_clear(value_); }

  mpz_t value_;
};

TEST(Gmp, LinkedRuntimeMeetsMinimumVersion) {
  int major = 0;
  int minor = 0;
  int patch = 0;
  ASSERT_EQ(std::sscanf(gmp_version, "%d.%d.%d", &major, &minor, &patch), 3)
      << gmp_version;
  EXPECT_TRUE(major > 6 || (major == 6 && minor >= 3)) << gmp_version;
}

TEST_F(GmpIntegerTest, ComputesAnIntegerLargerThan128BitsExactly) {
  mpz_ui_pow_ui(value_, 2, 128);
  char digits[50] = {};
  ASSERT_EQ(mpz_get_str(digits, 10, value_), digits);
  EXPECT_STREQ(digits, "340282366920938463463374607431768211456");
}

TEST(Gmp, ComputesExactRationalArithmetic) {
  mpq_t left;
  mpq_t right;
  mpq_init(left);
  mpq_init(right);
  mpq_set_ui(left, 1, 3);
  mpq_set_ui(right, 1, 6);
  mpq_add(left, left, right);
  EXPECT_EQ(mpq_cmp_ui(left, 1, 2), 0);
  mpq_clear(right);
  mpq_clear(left);
}

}  // namespace

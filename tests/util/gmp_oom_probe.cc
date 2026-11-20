#include <gmp.h>
#include <sys/resource.h>

#include <cstdio>
#include <cstring>

// This executable uses the default GMP allocator and runs without sanitizers.
int main(int argc, char** argv) {
  if (argc != 2) return 2;
  if (std::strcmp(argv[1], "runtime") == 0) {
    int major = 0;
    int minor = 0;
    int patch = 0;
    if (std::sscanf(gmp_version, "%d.%d.%d", &major, &minor, &patch) != 3 ||
        major < 6 || (major == 6 && minor < 3)) {
      std::fprintf(stderr, "Unsupported GMP runtime version %s\n", gmp_version);
      return 3;
    }

    mpz_t power;
    mpz_init(power);
    mpz_ui_pow_ui(power, 2, 128);
    char digits[50] = {};
    mpz_get_str(digits, 10, power);
    mpz_clear(power);
    if (std::strcmp(digits, "340282366920938463463374607431768211456") != 0) {
      return 4;
    }

    mpq_t left;
    mpq_t right;
    mpq_init(left);
    mpq_init(right);
    mpq_set_ui(left, 1, 3);
    mpq_set_ui(right, 1, 6);
    mpq_add(left, left, right);
    const bool exact = mpq_cmp_ui(left, 1, 2) == 0;
    mpq_clear(left);
    mpq_clear(right);
    return exact ? 0 : 5;
  }
  if (std::strcmp(argv[1], "oom") != 0) return 6;

  const rlimit no_core = {0, 0};
  if (::setrlimit(RLIMIT_CORE, &no_core) != 0) {
    std::perror("setrlimit(RLIMIT_CORE)");
    return 7;
  }
  constexpr rlim_t kAddressLimit = 64UL * 1024 * 1024;
  const rlimit address_limit = {kAddressLimit, kAddressLimit};
  if (::setrlimit(RLIMIT_AS, &address_limit) != 0) {
    std::perror("setrlimit(RLIMIT_AS)");
    return 8;
  }

  // This request is above RLIMIT_AS and below GMP's allocation-count limit.
  constexpr mp_bitcnt_t kRequestedBits = 128UL * 1024 * 1024 * 8;
  mpz_t value;
  mpz_init(value);
  mpz_realloc2(value, kRequestedBits);
  mpz_clear(value);
  return 9;
}

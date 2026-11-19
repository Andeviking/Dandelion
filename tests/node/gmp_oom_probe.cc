#include <gmp.h>
#include <sys/resource.h>

// This helper deliberately tests the unmodified GMP allocator in a subprocess.
// A strict address-space bound rejects the request before storage is committed.
int main() {
  const rlimit no_core = {0, 0};
  if (::setrlimit(RLIMIT_CORE, &no_core) != 0) return 1;
  constexpr rlim_t kAddressLimit = 64UL * 1024 * 1024;
  const rlimit address_limit = {kAddressLimit, kAddressLimit};
  if (::setrlimit(RLIMIT_AS, &address_limit) != 0) return 1;
  mpz_t value;
  mpz_init(value);
  mpz_realloc2(value, 128UL * 1024 * 1024 * 8);
  mpz_clear(value);
  return 2;  // A returned allocation is a failed test, not a recovered OOM.
}

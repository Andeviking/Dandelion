# Foundation numeric tests

`dandelion_util_tests` links `dandelion_util` and the shared GoogleTest
entry point. CTest discovers its cases with the `util` label. The standalone
import probe links only util. The GMP OOM probe links GMP directly and runs
without sanitizer instrumentation so its address-space limit tests GMP's
allocation failure.

## Coverage

| Cases | Tests and reference |
| --- | --- |
| I-01–I-04 | `integer_test.cc` uses bounded host arithmetic, exact decimal and chunk expectations, and Euclidean reconstruction. |
| Q-01–Q-04 | `rational_test.cc` checks exact reduced fractions, storage transitions including signed 128-bit overflow, and integral bounds. |
| B-01–B-05 | `bitvector_test.cc` enumerates widths 1–6 and uses a separate binary arithmetic oracle at widths 8–4096 with seed `0xdad311a`. It also checks BV65 overflow and self-division, and rotation by borrowed wide indices. |
| F-01–F-07 | `floating_point_test.cc` and `fp_oracle.h` enumerate tiny encodings, ordered pairs, and FMA triples using exact rational neighbors and squared midpoints. Wide cases include exact rounding and field checks for F(65,66). |
| X-01–X-02 | The value suites check layout and ownership. `failure_test.cc` sweeps allocation failures, checks capacity boundaries, and verifies preservation of operands and borrows. |
| X-03 | `gmp_failure_test.cc` and `gmp_oom_probe.cc` check GMP runtime compatibility, exact arithmetic, and `SIGABRT` in a subprocess with bounded address space. |
| X-04 | `import_probe.cc` crosses all five public partitions through the primary import. The value suites cover standalone cross-type round-trips. |

The tiny FP suite checks F(2,2), F(2,3), and F(2,4). It uses all
16, 32, and 64 raw encodings for construction and identity. The arithmetic
loops use the 15, 27, and 51 distinct semantic values after collapsing
NaN payloads. The five modes give 778,545 FMA cases. The reference derives
these results using exact rational arithmetic.

`numeric_test_peer.h` declares test-only controls for C++ allocation and
capacity failure injection, backing storage inspection, and selection of
the portable GMP conversion path for BV arithmetic. The direct and portable
BV paths run on the same supported 64-bit GMP ABI.

## Verification

```sh
CXX=g++ cmake --preset gcc-asan-ubsan
cmake --build --preset gcc-asan-ubsan --target dandelion_util_tests
ctest --preset gcc-asan-ubsan -L '^util$'

CXX=clang++ cmake --preset clang-debug
cmake --build --preset clang-debug --target dandelion_util_tests
ctest --preset clang-debug -L '^util$'

CXX=g++ cmake --preset release
cmake --build --preset release --target dandelion_util_tests
ctest --preset release -L '^util$'
```

[Numeric test design](../../docs/testing/util.md)

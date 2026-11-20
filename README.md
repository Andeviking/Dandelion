# Dandelion

Dandelion is a C++23 project building a high-performance satisfiability modulo
theories (SMT) solver from scratch.

## Current Status

- `dandelion_util` implements concrete integer, rational, bit-vector, and
  floating-point values and operations.
- `dandelion_node` implements checked construction and structural interning of
  typed expressions, sorts, and declarations.
- `dandelion_frontend` reads and type-checks the selected SMT-LIB profile and
  exposes the query to a solver provider. See the
  [frontend logic profiles](docs/frontend/logic-profile.md).
- The repository includes GoogleTest suites registered with CTest, optional
  component benchmarks, design documentation, optional Doxygen API
  documentation, and GitHub Actions workflows for build and test, Quality,
  and documentation checks.

The solver core, production rewriting and preprocessing, SAT integration,
theory procedures and combination, production model construction, solver CLI,
and installation support are planned. See the
[solver architecture proposal](docs/architecture.md).

## Target Scope

Dandelion targets quantifier-free formulas over these theories and their
combinations:

- Fixed-size bit-vectors (BV)
- Floating-point arithmetic (FP)
- Integer arithmetic, including integer difference logic (IDL), linear
  integer arithmetic (LIA), and nonlinear integer arithmetic (NIA)
- Extensional arrays
- Uninterpreted functions and sorts (UF)
- Algebraic datatypes (DT)

The target is one `check-sat` query per session. Incremental `push`/`pop`,
multiple `check-sat` queries, and general Real and String theories are outside
the current scope. SMT-LIB metadata strings are in scope.

The planned solver will report `sat` or `unsat` only when justified for the
original query, return `unknown` when it cannot decide an accepted query, and
report invalid or unsupported input explicitly.

## Requirements

- Validated Linux x86-64 toolchain with GCC 16.2.0 or Clang 23.1.5, both using
  libstdc++ 16.2.0, and Ninja 1.13.2. GCC is the primary development compiler.
- CMake 4.5 or newer and GMP 6.3.0 or newer.
- GoogleTest for tests, Google Benchmark for benchmarks, and Doxygen 1.17.0
  or newer for API documentation.
- Python 3.8 or newer, clang-format 23.1.5, and clang-tidy 23.1.5 for the
  corresponding developer checks. The POSIX tooling tests require all three.
- Git and Bash for the local hooks.

## Build and Test

From the repository root, select the compiler on the first configure of each
build directory:

```sh
CXX=g++ cmake --preset debug
cmake --build --preset debug
ctest --preset debug
```

The configure, build, and test presets share these names. Each configures
with tests enabled and uses `build/<preset>`.

| Preset | Configuration | ASan and UBSan | Intended compiler |
| --- | --- | --- | --- |
| `debug` | Debug | On | GCC |
| `release` | Release | Off | GCC or Clang |
| `relwithdebinfo` | RelWithDebInfo | Off | GCC or Clang |
| `clang-debug` | Debug | Off | Clang (`CXX=clang++`) |
| `gcc-asan-ubsan` | Debug | On | GCC (`CXX=g++`) |

CMake defaults to `DANDELION_DEPENDENCY_MODE=EXISTING`. Use
`-DDANDELION_BUILD_TESTS=OFF` for a library-only build or
`-DDANDELION_BUILD_TOOLING_TESTS=OFF` to omit the developer-tool tests.
See the [CMake build guide](docs/cmake.md) for dependency sources and options.

## Benchmarks

Enable Google Benchmark in a separate Release build. The numeric, Node, and
frontend benchmark targets are `dandelion_util_benchmarks`,
`dandelion_node_benchmarks`, and `dandelion_frontend_benchmarks`.

```sh
CXX=g++ cmake --preset release -B build/benchmarks \
  -DDANDELION_BUILD_BENCHMARKS=ON -DDANDELION_BUILD_TESTS=OFF
cmake --build build/benchmarks
build/benchmarks/benchmarks/util/dandelion_util_benchmarks --benchmark_dry_run
```

Use the [benchmark guide](docs/benchmarks/README.md) for workloads, data
requirements, and reproducible measurements.

## Documentation

- [Architecture proposal](docs/architecture.md) and
  [development principles](docs/development-principles.md)
- [Numeric values](docs/util/README.md), [Node](docs/node/README.md), and
  [frontend](docs/frontend/README.md)
- [Testing strategy](docs/testing/README.md), [CMake build guide](docs/cmake.md),
  and [C++ style and tooling](docs/cpp-style.md)

Configure with `-DDANDELION_BUILD_DOCS=ON` and build the `dandelion_docs`
target to generate `docs/html/index.html` in the build directory.

## Contributing

Follow [AGENTS.md](AGENTS.md) for test-first development, documentation, and
local checks. Enable the repository hooks for this clone:

```sh
git config --local core.hooksPath .githooks
```

The pre-commit hook checks staged whitespace. The pre-push hook checks
whitespace for each branch update and C++ formatting for each distinct
pushed branch tip in a temporary checkout.
Before pushing a PR, run the CI Quality checks across its range and check
uncommitted edits:

```sh
MERGE_BASE=$(git merge-base origin/main HEAD) &&
  git diff --check "$MERGE_BASE" HEAD -- . ':!LICENSES/**' ':!*.patch'
python3 scripts/check_cpp.py format-check
git diff --check
```

## License

Dandelion is licensed under the [MIT License](LICENSE). Third-party components
have their own license terms. See [Third-party licenses](THIRD_PARTY_LICENSES.md).

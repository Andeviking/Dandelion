# C++ Style and Developer Tools

Status: implemented development infrastructure, validated on 2026-11-16. The
repository has Node, frontend, and toolchain tests. The solver remains future
work.
See [cmake.md](cmake.md) for build policy and future solver integration.

## Style baseline

Use the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html)
as the primary reference for formatting and naming, with these project choices:

- Use C++23. GCC is the primary compiler; Clang is the second compiler.
- Prefer C++ modules and `import std` where verified. Google's language-version
  and module restrictions do not override these choices.
- Use two-space indentation, spaces rather than tabs, and an 80-column limit.
- Use descriptive `snake_case` filenames: `.cc` for ordinary sources, `.h` for
  headers, and `.cppm` for module interfaces. Other supported C++ extensions are
  checked too. Name project modules under `dandelion` and use namespace
  `dandelion`, with lowercase nested names.
- Keep source, comments, tests, scripts, configuration, and documentation in
  English.

This adoption does not settle independent design decisions such as exceptions,
RTTI, ownership abstractions, allocation, or library selection. The scaffold
does not add flags to disable exceptions or RTTI. Make such decisions explicitly
with correctness and architecture evidence.

| Entity | Convention | Example |
| --- | --- | --- |
| Types, aliases, enums, type template parameters | PascalCase | `TermParser`, `TermKind` |
| Ordinary functions and methods | PascalCase | `ParseTerm()` |
| Variables and parameters | snake_case | `token_count` |
| Class data members, including mutable static members | snake_case with a trailing underscore | `token_count_` |
| Struct data members | snake_case | `token_count` |
| Constants whose values are fixed for the program, with static storage duration | k followed by PascalCase | `kMaxSize` |
| Enumerators | k followed by PascalCase | `TermKind::kBitVector` |
| Namespaces | snake_case | `dandelion::toolchain` |
| Necessary macros | DANDELION_ followed by uppercase words | `DANDELION_MAX_SIZE` |

Accessor and mutator names may use `token_count()` and `set_token_count()`.
Standard-compatible interfaces may use names such as `value_type` and `begin()`.
Automatic constants may use ordinary variable names or `k` names when their
values are fixed across calls. A runtime-dependent local `const` is not made a
program-wide constant just by its type.

## API documentation

For public declarations in C++ module interfaces, place Doxygen comments
directly above the declaration. Start with `/// @brief` and a concise
description of the declaration's purpose. Use a blank `///` line before
details about preconditions, ownership, lifetime, or errors when callers
need them. Use `@param` and `@return` when they clarify the contract. Use
ordinary `//` comments for implementation details. Build the documentation
with the [Doxygen target](cmake.md#doxygen).

## Formatting

`.clang-format` starts from Google, with C++23 parsing. The
reproducible baseline is **clang-format 23.1.5**. The entry script rejects other
versions; a tool path can be supplied with `--clang-format`.

Run from the repository root:

```sh
python3 scripts/check_cpp.py format-check
python3 scripts/check_cpp.py format
python3 scripts/check_cpp.py format-check
```

The check uses `--dry-run --Werror` and fails for formatting violations. The
format action edits owned files in place, including untracked sources; review
the diff. The regression suite demonstrates a failing `.cppm`, a successful
format operation, and repeatable checks without further changes.

Intentional departures from Google's formatter defaults:

- Include sorting is disabled, and include blocks are preserved. Audit include
  dependencies before reordering them manually; some headers depend on order.
- Using-declaration sorting is disabled to preserve declaration order.

Brace insertion/removal, parenthesis removal, semicolon removal, and qualifier
reordering are explicitly disabled. Formatting is not a substitute for checking
the meaning of an edit.

## Naming checks and their limits

`.clang-tidy` enables only `readability-identifier-naming` and
treats its diagnostics as errors. It does not enable `modernize-*`,
`readability-*`, or `cppcoreguidelines-*` as groups. Compiler parse errors also
fail a check. Clang-tidy, its Clang compilation database, and the compiler used
to build its BMIs use the validated **23.1.5** baseline.

```sh
clang-tidy --verify-config
python3 scripts/check_cpp.py tidy --build-dir build/clang-debug
ctest --preset clang-debug -L tooling --no-tests=error
```

The tidy entry verifies that every selected translation unit has exactly one
ordinary compilation command and that its compiler is Clang 23.1.5. It rejects
a GCC database and ambiguous ordinary commands. CMake may also generate Clang
`--precompile` commands for synthetic BMI variants; the entry script excludes
these identified synthetic commands and passes the ordinary commands to
clang-tidy through a temporary filtered database. It first builds the Clang
tree, so CMake's module response files, standard-library BMIs, and project BMIs
exist, then checks every owned unit directly, including `.cppm` interfaces,
module implementations, and consumers. It does not rewrite GCC flags into a
guessed Clang command.

The [identifier-naming check documentation](https://clang.llvm.org/extra/clang-tidy/checks/readability/identifier-naming.html)
describes syntactic categories, not a full implementation of the Google guide.
In the tested tool:

- `Member` covers non-static fields of both classes and structs. `ClassMember`
  covers mutable static fields of both. Both default to the class convention
  with a trailing underscore. The tool cannot express the class/struct split
  accurately; annotate each legitimate struct field as below and review its
  spelling. Access level does not change this rule.
- Ordinary methods and functions require PascalCase. The tool cannot identify
  accessor semantics or whether a name implements a standard interface. Use a
  single-declaration exception for these cases, not a regex that accepts all
  snake_case methods or aliases. Only the exact free-function name `main` has a
  configured name exemption; names such as `test_main` are still checked.
- Namespace/static/class constants and enumerators receive `k` names. Local
  constants accept snake_case or the anchored `k[A-Z][a-zA-Z0-9]*` pattern. The
  latter does not exempt mutable variables. The tool cannot prove that a local
  value is fixed across calls, so review the choice of constant-style names.

Use a check-specific suppression on the immediately preceding line, with a
reason and one declaration on the annotated line:

```cpp
class TermParser {
 public:
  // NOLINTNEXTLINE(readability-identifier-naming): Google-style accessor.
  int token_count() const { return token_count_; }

 private:
  int token_count_ = 0;
};

struct TermInfo {
  // NOLINTNEXTLINE(readability-identifier-naming): Struct field, snake_case.
  int token_count;
};

class TermRange {
 public:
  // NOLINTNEXTLINE(readability-identifier-naming): Standard container interface.
  using value_type = int;
  // NOLINTNEXTLINE(readability-identifier-naming): Standard range interface.
  int* begin();
};
```

Do not suppress an entire file or all naming checks for a class. These
annotations transfer that declaration's naming check to review, including its
case; they do not validate the declaration automatically. The suite verifies
that these annotated declarations pass, their unannotated forms are rejected,
and adjacent ordinary-method and field violations still fail.

Review also covers name clarity, abbreviations, semantic accessor eligibility,
the necessity of a macro, concepts and naming categories not configured here,
and inherited/interface names the checker may skip. This is deliberately not a
claim that clang-tidy fully checks Google style.

## Source ownership and check scope

`scripts/check_cpp.py` discovers C++ files under `src/`,
`include/`, `tests/`, `tools/`, and `benchmarks/`. Add new owned source roots to
that list when needed. It recognizes `.cc`, `.cpp`, `.cxx`, `.c++`, `.C`, `.cppm`, `.ixx`,
`.mpp`, `.h`, `.hh`, `.hpp`, `.hxx`, `.inc`, `.ipp`, and `.tpp` (header suffixes
also accept uppercase spelling).

Build trees, `CMakeFiles`, `_deps`, `third_party`, `third-party`, `vendor`,
`external`, and `generated` directories are excluded. Directory symlinks and
symlinked source files are not followed. Keep generated sources in a build or
`generated` directory, rather than mixing them with owned handwritten files.
An empty source selection fails with exit code 2 instead of reporting success.
Formatting always checks the benchmark sources. A naming check reports that it
skips them when its CMake build explicitly disables benchmarks; configure the
Clang build with `-DDANDELION_BUILD_BENCHMARKS=ON` to include them. With the
option enabled, a missing benchmark compilation command remains an error.

Formatting covers headers directly. Tidy checks owned headers when parsed by a
selected translation unit, using an exact-path header filter computed from the
owned file list. It does not prove coverage of an unreferenced header; ensure
new headers have a compiled consumer. Standard-library and dependency source
entries in `compile_commands.json` are never selected as project units, and
system headers are not part of the naming gate. The tool can still summarize
suppressed diagnostics from imported library code.

## Build and runtime checks

Validated tools: Linux x86-64, GCC 16.2.0, Clang 23.1.5, libstdc++ 16.2.0,
CMake 4.5.0, Ninja 1.13.2, and Python 3.12.13 for the GoogleTest tooling
suite. Use CMake 4.5 or newer and a module-capable Ninja toolchain. Python 3.8
or newer runs the check scripts.
Clang with libc++, other platforms, and other compiler versions have not been
validated by this scaffold.

The shared presets do not set `CMAKE_CXX_COMPILER`, `CXX`, personal paths, or
toolchain files. Select the compiler on the **first** configure. The names below
describe the intended configuration; they do not override a selected toolchain.
Never reuse one tree for different compilers, standard libraries, or sanitizer
settings. Personal settings belong in ignored `CMakeUserPresets.json`.
The project requires GMP 6.3.0 or newer, and tests require GoogleTest.
Configuration defaults to existing dependencies and fails with a diagnostic if
a required dependency is unavailable. See [the dependency
policy](cmake.md#51-dependency-sources-and-network-policy) for user-provided
packages or sources and for explicitly enabling a project-local download.
The tool regression suite additionally requires POSIX and the specified Python,
clang-format, and clang-tidy versions; disable only that suite with
`-DDANDELION_BUILD_TOOLING_TESTS=OFF` if these tools are unavailable.

```sh
CXX=g++ cmake --preset debug -D DANDELION_WARNINGS_AS_ERRORS=ON
cmake --build --preset debug
ctest --preset debug

CXX=clang++ cmake --preset clang-debug -D DANDELION_WARNINGS_AS_ERRORS=ON
cmake --build --preset clang-debug
ctest --preset clang-debug

CXX=g++ cmake --preset release
cmake --build --preset release
ctest --preset release

CXX=g++ cmake --preset relwithdebinfo
cmake --build --preset relwithdebinfo
ctest --preset relwithdebinfo

CXX=g++ cmake --preset gcc-asan-ubsan -D DANDELION_WARNINGS_AS_ERRORS=ON
cmake --build --preset gcc-asan-ubsan --verbose
ctest --preset gcc-asan-ubsan
```

The project test targets include `dandelion_test_main`,
`dandelion_toolchain_module`, `dandelion_toolchain_smoke`, and, when enabled,
`dandelion_tooling_tests`. The node target and node tests are described in
[node acceptance](testing/node-acceptance.md). The GoogleTest smoke tests import `std` and a named
module, exercise C++23 `std::to_underlying`, verify the GMP runtime version,
and check exact large-integer and rational arithmetic even with `NDEBUG`. There
is no solver executable or backend dependency.
`DANDELION_BUILD_TESTS` defaults to ON for a top-level build and OFF as a
subproject. With tests disabled, the Node and util libraries build
independently of GoogleTest and omit test-peer hooks. Util provides the GMP
dependency for both libraries.

Project warnings are `-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion
-Wshadow` for GCC/Clang. `DANDELION_WARNINGS_AS_ERRORS` defaults to OFF. The
target helper requires an explicit owned source list and attaches warning
options to those sources in the target's directory. This source-local exception
to the usual target-option approach is necessary because CMake 4.5 copies
target compile options to synthesized standard-library module targets. Neither
project warnings nor `-Werror` are imposed on those targets or dependencies.

For a newly configured, top-level, single-configuration GCC Debug build,
`DANDELION_ENABLE_SANITIZERS` defaults to ON; it defaults to OFF otherwise. The
`debug` and `gcc-asan-ubsan` presets explicitly enable it, while `release`,
`relwithdebinfo`, and `clang-debug` explicitly disable it. A configure-time
option can override these defaults. The `debug` preset does not select a
compiler: on the validated machine, use GCC for it and `clang-debug` for the
uninstrumented Clang build.

Enabling the option adds `-fsanitize=address,undefined
-fno-sanitize-recover=undefined -fno-omit-frame-pointer` at compilation and
`-fsanitize=address,undefined -fno-sanitize-recover=undefined` at linking for
selected project targets. UBSan errors terminate the program with a failure
status, so a diagnostic does not silently pass CTest. CMake also prepares
standard-library BMIs with compatible compile options. Sanitizers only check
code paths that actually run.

On the validated machine, GCC's sanitizer module test and a standalone
allocation test compile, link, and run. Small intentional heap overflow and
signed overflow probes produce ASan and UBSan reports respectively. Clang's
standalone ASan+UBSan link fails because `libclang_rt.asan_static.a`,
`libclang_rt.asan.a`, and `libclang_rt.asan_cxx.a` are missing. The sanitizer
option remains available for a suitably provisioned Clang toolchain, but there
is no passing local Clang sanitizer configuration. No tools or runtimes were
installed to work around this limitation.

## Acceptance evidence

The GoogleTest cases in `tests/tooling/cpp_tools_test.cc` run the actual format
and tidy tools. They cover configuration validity, the narrow enabled check set,
23 negative naming cases, legitimate local exceptions, adjacent unsuppressed
violations, module formatting, formatting idempotence, preserved include order
and qualifiers/braces, excluded sources, empty selections, synthetic versus
duplicate compilation-database entries, and offline GMP resolution errors.

The project module interface and consumer were also given temporary naming
violations during acceptance: direct Clang-database checks rejected both with
exit code 1. The valid sources were restored and rebuilt. Successful module
builds and CTest runs validate GCC and Clang independently. Fresh isolated
copies were also configured and built with both compilers: the next build had
no work, an implementation-only edit rebuilt only that implementation object,
and an interface edit rebuilt the interface, implementation, and consumer.
The edited copies passed their runtime tests. The GCC Release and
RelWithDebInfo presets also built and ran successfully. Compiler flags and
the final link command were inspected for sanitizer and warning isolation;
subproject tests confirmed that tests and development options default to OFF
and that the caller's compiler choice is retained.
These are small infrastructure checks, not solver correctness tests or
performance measurements. CI, SAT dependency adapters, project installation,
and the parser and solver are still future work.

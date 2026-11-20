# Dandelion CMake Build Standards and Design

This document records Dandelion's build-system design, CMake development standards, and performance validation methods for both human developers and agents. Research date: 2026-11-16.

The numeric util, Node, and frontend libraries, development scaffold, GMP,
GoogleTest, and optional Google Benchmark integration are implemented. A solver
executable, SAT backend, and installation support remain future work. See
[C++ style and developer tools](cpp-style.md) for executable checks, exact
commands, verified tool versions, and current limitations. Future target
names, build options, other dependency mechanisms, and solver test commands
are proposals unless explicitly identified as implemented.

## Reading guide

- For the current setup and its constraints, see [goals and initial decisions](#1-goals-and-initial-decisions), [project and build directories](#2-project-and-build-directories), and the [build and runtime commands](cpp-style.md#build-and-runtime-checks).
- For CMake changes, see [target-based standards](#3-target-based-cmake-standards) and [C++ modules and `import std`](#4-c-modules-and-import-std).
- For dependency, documentation, and test integration, see [external dependencies and SAT backends](#5-external-dependencies-and-sat-backends), [Doxygen](#doxygen), and [GoogleTest and CTest](#6-googletest-and-ctest).
- For build performance or future design choices, see [build speed and resource use](#7-build-speed-and-resource-use), [measurement and acceptance](#8-measurement-and-acceptance), and [open decisions](#9-open-decisions).

Sections contain both implemented behavior and future proposals; apply the
status of each item as described rather than assuming every example is present.

## 1. Goals and Initial Decisions

| Topic | Current design |
| --- | --- |
| Build system | Use CMake to describe the build graph and Ninja by default for development. |
| CMake baseline | Initially require CMake 4.5 or newer for standard-library module support; lowering the minimum version requires separate evaluation and testing. |
| Development toolchain | Linux x86-64, GCC 16.2.0 as primary compiler and Clang 23.1.5 as second compiler, both with libstdc++ 16.2.0, CMake 4.5.0, and Ninja 1.13.2 are validated by the scaffold. Other combinations require validation. |
| Language | C++23; favor standard modules and `import std`, subject to toolchain validation and measured benefits. |
| Project organization | Use targets to describe sources, dependencies, and usage requirements. Start with a small number of targets with clear responsibilities and split them as the project grows. |
| Default build | Use out-of-source builds with separate directories for build types, compilers, and important configurations. |
| Testing | Use GoogleTest for all project tests, with CTest for test discovery and execution. The existing toolchain and tooling tests use this integration. |
| External dependencies | Require GMP 6.3.0 or newer. Support preinstalled dependencies. Source acquisition must be controlled and version-pinned, with clear diagnostics when a dependency is missing. |
| Performance principle | Measure configure time, clean builds, incremental builds, resource use, and solver runtime separately. Optimizations must be supported by reproducible measurements. |

The build system must describe dependencies correctly and clearly so tools can parallelize work and rebuild only what is necessary. Shorter CMake code does not automatically make compilation faster; modules, compiler caches, unity builds, and link-time optimization each have costs and limitations.

## 2. Project and Build Directories

The suggested direction is shown below; create files and directories as they become necessary:

```text
Dandelion/
├── CMakeLists.txt             # project, options, subdirectories, and top-level wiring
├── CMakePresets.json          # development configurations shared by the repository
├── cmake/                     # dependency and toolchain helpers when complexity warrants them
├── src/                       # solver sources and targets
├── tests/                     # tests and targets
├── benchmarks/                # opt-in component performance workloads
├── subprojects/               # optionally downloaded dependency sources
└── build/                     # local generated files; not committed
    ├── debug/                # primary compiler, ASan + UBSan
    ├── release/
    ├── relwithdebinfo/
    ├── clang-debug/          # separate Clang BMIs; sanitizers disabled locally
    └── gcc-asan-ubsan/       # explicit GCC sanitizer tree
```

- Start the top-level file with `cmake_minimum_required(VERSION 4.5)` and `project(...)`; enable only the languages the project actually needs. The root currently enables CXX. GoogleTest's source project also enables C, so source builds require a usable C compiler.
- Build outside the source tree; do not generate object files, module BMIs, or third-party build products in source directories.
- Each build directory should correspond to a fixed generator, compiler, target platform, and major build configuration. Use a new directory or explicitly reconfigure when changing compilers or ABI-related settings; do not mix incompatible caches or BMIs.
- Keep shared settings in `CMakePresets.json`; put personal paths, parallelism, and local toolchain overrides in an uncommitted `CMakeUserPresets.json` or on the command line. Do not write personal absolute paths such as `/home/zhangx/...` into project configuration. [CMake Presets manual](https://cmake.org/cmake/help/v4.5/manual/cmake-presets.7.html)
- For the single-configuration Ninja generator, the scaffold provides `debug`, `release`, `relwithdebinfo`, `clang-debug`, `clang-coverage`, and `gcc-asan-ubsan` presets with matching build/test presets. `clang-fuzz` provides matching configure and build presets. Each configuration uses a separate `binaryDir`.
- Shared presets may use a hidden Ninja base inherited by other configurations, with matching build and test presets for common configurations. Users select the compiler through a personal preset, a toolchain file, or the environment at the first configuration; shared presets should not force an absolute path to a local GCC installation.
- The shared presets explicitly enable `CMAKE_EXPORT_COMPILE_COMMANDS`. In a fresh direct Ninja configuration with the validated CMake 4.5 toolchain, the root cache assignment can leave this option empty and no database is generated. Until fixed, pass `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` when a direct build needs the database. A future top-level default should enable it when unspecified while respecting an explicit user or parent-project `OFF`; do not use `FORCE` to override that choice. Keep the database in the build directory.
- Make tests easy to enable during development. When included as a subproject, avoid enabling unrelated tests and dependencies by default. Test targets, data, and dependency acquisition should be controllable. GoogleTest and CTest have distinct roles described in Section 6.

### 2.1 Component Boundaries

The following dependency directions describe the full project's build responsibilities, not existing target names or a requirement to create empty libraries:

```text
command-line driver -> frontend, solver
frontend            -> shared expressions
solver              -> shared expressions, SAT adapter
SAT adapter         -> selected SAT backend(s)
component tests     -> relevant project components, GoogleTest
```

Shared expressions include nodes, sorts, declarations, and their management; they do not depend on the frontend or solver. The frontend resolves input and constructs expressions without requiring SAT. The solver owns rewriting, theory coordination, and model construction; term-to-SAT-literal mapping belongs with the solving logic. The driver combines command handling, solving, and output. A small common library is useful only when there is actual shared functionality. See [node design](node/README.md) and [solver architecture](architecture.md) for their respective responsibilities.

Use a small number of responsibility-based targets, with multiple source directories and C++ modules inside a target where appropriate. A library is not required for every theory, directory, or module. STATIC libraries are a reasonable default for reusable internal components; OBJECT libraries merit evaluation when the same objects must form multiple artifacts with compatible options. Verify usage requirements, PIC, sanitizer settings, and duplicate object inclusion rather than treating object reuse as automatically safe or faster.

The `dandelion_frontend` target exports `dandelion.frontend` and links
publicly to `dandelion_node`. Node links publicly to `dandelion_util`,
which exports `dandelion.util.numeric` and owns the private GMP dependency.
Phase 4B migrated the frontend and the benchmark workloads to the typed
Node APIs. With `DANDELION_BUILD_TESTS=OFF`, the frontend, Node, and util
targets build without GoogleTest or SAT discovery. Test builds discover
the frontend suite under label `frontend` and build `dandelion_frontend_probe`
for opt-in corpus validation. Any future SAT discovery belongs to its own
build selection. [Frontend acceptance](frontend/implementation.md)

`DANDELION_BUILD_BENCHMARKS=ON` adds `dandelion_util_benchmarks`,
`dandelion_node_benchmarks`, and `dandelion_frontend_benchmarks`. The
Phase 5 workloads measure standalone numerics, typed Node interning, and
benchmark-local DAG folding. Phase 6 uses these targets to measure the
optimized numeric and Node paths. The Node and frontend workload libraries
also build when tests are enabled with benchmarks disabled.
[Benchmark integration](testing/benchmark.md)

## 3. Target-Based CMake Standards

### 3.1 Responsibilities and Dependencies

- Each target should represent a clear build artifact or reusable usage requirements. Split targets as boundaries, reuse, independent testing, or incremental-build needs become clear; do not mechanically create a library for every source file or theory.
- Manage target properties with `target_sources()`, `target_include_directories()`, `target_compile_features()`, `target_compile_definitions()`, `target_compile_options()`, `target_link_options()`, and `target_link_libraries()`.
- Default to `PRIVATE`; use `PUBLIC` or `INTERFACE` only when consumers actually need the dependency's usage requirements. Propagate the C++ standard and dependencies exposed by public interfaces accurately. CMake may still propagate a static library's private link dependency to the final link target as a link-only requirement; this does not require making all compile requirements `PUBLIC`. [CMake target usage requirements](https://cmake.org/cmake/help/v4.5/command/target_link_libraries.html)
- Express dependencies through CMake targets where possible, rather than handwritten `-l...` flags, global link directories, or assumed library paths. For an upstream library without a CMake package, wrap it in an adapter that provides an imported target with explicit locations and usage requirements.
- Avoid directory-wide `include_directories()`, `link_directories()`, `add_definitions()`, or global `CMAKE_CXX_FLAGS` that leak project policy into third-party code and unrelated targets. When toolchain-wide flags are necessary, let the user or toolchain configuration provide them and document the reason.
- Keep target dependencies consistent with module imports; express actual link dependencies through `target_link_libraries()`. Do not overuse `add_dependencies()` merely to force ordering, which can needlessly reduce parallelism. Explicitly describe genuine prerequisites for generated files or external projects.
- Set the required C++ standard and extension policy on project-owned targets, for example with `target_compile_features(... cxx_std_23)` and `CXX_EXTENSIONS OFF`. Do not rely on GCC's default language version or hardcode `-std=...` into general compile options.

### 3.2 Files and Scripts

- List project sources and module interfaces explicitly; do not use `file(GLOB ...)` to collect source files. Maintain one source list per group and reuse it when registering sources and applying the owned-source policy, rather than maintaining independent copies. C++ module file sets are distinct from ordinary source properties; do not assume querying only a target's `SOURCES` covers its module interfaces. [CMake `file(GLOB)` documentation](https://cmake.org/cmake/help/v4.5/command/file.html)
- Use lowercase CMake commands and clear target and variable names. Prefix project-specific cache options with `DANDELION_`, and give them descriptions and reasonable defaults.
- Do not use `CACHE ... FORCE` to override user settings without good reason; project code must not override the user's selected compiler, generator, or installation prefix.
- Introduce CMake functions or helper modules only when they provide useful reuse. Avoid hard-to-trace macros, deep wrappers, and complex automatic discovery early in the project.
- Apply warning options to project-owned targets. Make `-Werror` controllable; do not propagate project `-Werror` or private warning flags to third-party code or downstream consumers.
- In the CMake 4.5 scaffold, `dandelion_configure_target()` takes an explicit owned `SOURCES` list and attaches warnings, libFuzzer instrumentation, and source coverage to those sources in the target's directory. CMake 4.5 copies target compile options into synthesized standard-library module targets. C++ standard requirements and ASan+UBSan compatibility options remain target-based.
- Source properties are directory-scoped. If the same source is compiled into multiple targets in that directory with different policies, verify that warning settings do not leak between them. Avoid compiling project implementation sources again in each test merely to bypass a target boundary.
- An instrumented static library contains instrumented objects but does not perform the final executable link. Ensure consumers receive the required sanitizer runtime link options, for example through appropriately scoped target usage requirements or an explicit final-link policy. Apply project instrumentation compile options privately to selected targets; CMake may also carry compatible options into synthesized standard-library BMIs. Verify consumers both with and without their own instrumentation. The current smoke executable also applies the helper, so its passing link does not establish that an arbitrary consumer receives those options. [CMake link options](https://cmake.org/cmake/help/v4.5/command/target_link_options.html)
- Use `BUILD_INTERFACE` and `INSTALL_INTERFACE` to distinguish public paths in build and install trees. Implement and test exports when installation and a CMake package are actually provided; do not embed machine-specific dependency paths in interfaces.

## 4. C++ Modules and `import std`

### 4.1 Versions and Limitations

The CMake 4.5 [release notes](https://cmake.org/cmake/help/v4.5/release/4.5.html) and [modules manual](https://cmake.org/cmake/help/v4.5/manual/cmake-cxxmodules.7.html) state that C++20 and newer code with module scanning enabled can use `import std` with supported combinations of compiler, standard library, and generator. CMake scans sources, collates module dependencies, generates Ninja dynamic dependencies and module maps, and orders BMI compilation. A dependency chain can delay its consumers, while independent branches may compile concurrently. Measure scan, BMI, and compile costs on actual project code.

- The project uses C++23 as its baseline; do not depend on the compiler's default standard version.
- CMake 4.5's `import std` workflow does not require the `CMAKE_EXPERIMENTAL_CXX_IMPORT_STD` UUID examples intended for older CMake releases. Do not rely on the older `CMAKE_CXX_COMPILER_IMPORT_STD` detection logic. Configure according to the CMake 4.5 documentation and verify with an actual build.
- The CMake manual limits `import std` support by compiler, standard library, and generator. The feature currently supports Ninja generators, which require Ninja 1.11 or newer. The manual lists GCC 15 and newer on Linux, but Dandelion's first actual validation used GCC 16.2.
- Available CMake support for standard-library modules does not mean every compiler module feature is mature: GCC 16 still describes its C++ modules support as experimental. Preserve minimal reproductions of compiler bugs and evaluate upgrades or local alternatives. [GCC 16 release notes](https://gcc.gnu.org/gcc-16/changes.html)
- CMake's current module support does not cover header units; do not treat `import <header>` as equivalent to the validated `import std` path. Non-modular third-party libraries can still be used through their conventional headers and link interfaces.
- Use `CMAKE_CXX_STDLIB_MODULES_JSON` to specify a metadata path only when the compiler cannot discover it automatically; do not hardcode a local metadata path into the repository. [Variable documentation](https://cmake.org/cmake/help/v4.5/variable/CMAKE_CXX_STDLIB_MODULES_JSON.html)
- CMake 4.4 and newer can synthesize BMI targets for consumer-compatible settings. Different relevant options may produce multiple BMI variants even in one build tree; equivalent consumers may share a variant. Keep deliberately shared settings consistent without removing options required for correctness. BMIs are build artifacts tied to compiler, standard library, configuration, and compatible options, not generally portable library artifacts.

### 4.2 Target Organization Example

This illustrative solver-organization example is not an implemented solver target. The actual acceptance targets are `dandelion_toolchain_module` and `dandelion_toolchain_smoke` in `tests/toolchain/`. The example places a module interface in a `CXX_MODULES` file set, adds an implementation unit as an ordinary source, and propagates module usage requirements through target dependencies:

```cmake
cmake_minimum_required(VERSION 4.5)
project(DandelionModuleExample LANGUAGES CXX)

add_library(core STATIC)
target_sources(core
  PUBLIC FILE_SET CXX_MODULES FILES math.cppm
  PRIVATE impl.cc
)
target_compile_features(core PUBLIC cxx_std_23)
set_target_properties(core PROPERTIES CXX_EXTENSIONS OFF)

add_executable(example main.cc)
target_link_libraries(example PRIVATE core)
set_target_properties(example PROPERTIES CXX_EXTENSIONS OFF)
```

Here, `math.cppm` can declare `export module dandelion.math;` and a public function; `impl.cc` defines `module dandelion.math;` and implements that function; `main.cc` uses it through `import dandelion.math;`. The implementation or consumer can use `import std;` as needed. These example filenames are illustrative; see [cpp-style.md](cpp-style.md) for project conventions.

A `PUBLIC` module file set makes its modules available to target consumers; it does not by itself promise a stable external API. A `PRIVATE` module cannot be imported by another target or by a `PUBLIC` module interface in the same target. Implementation units may import private modules. In contrast, listing an ordinary implementation source as `PUBLIC` or `INTERFACE` gives it to consumers as a source and can compile it repeatedly. Choose visibility according to actual use, including internal tests. [CMake module visibility](https://cmake.org/cmake/help/v4.5/manual/cmake-cxxmodules.7.html#module-visibility)

Keep CMake's dependency scanning enabled for sources that use modules or `import std`; do not disable `CXX_SCAN_FOR_MODULES` globally just to save scan time. Disabling scanning may be evaluated separately for independent targets confirmed not to use modules. Scanned sources are excluded from CMake unity builds. [CMake module scanning rules](https://cmake.org/cmake/help/v4.5/manual/cmake-cxxmodules.7.html)

### 4.3 Minimal Local Validation

On 2026-11-16, a temporary project containing a static-library module, implementation unit, executable, and `import std` was validated with GCC 16.2.0, CMake 4.5.0, Ninja 1.13.2, and Linux x86-64:

1. Configuration, build, and execution succeeded without setting the older CMake experimental UUID.
2. A repeated build reported `ninja: no work to do`.
3. Changing only the module implementation unit recompiled that unit and relinked without recompiling the importing `main`.
4. Changing the public module interface caused the related implementation and consumer to recompile.

This is a smoke test of correctness and the dependency graph, not a performance comparison on the actual project. Module-interface changes can expand rebuilds, and long chains of module dependencies can limit parallelism. Keep interfaces clear and stable; avoid unnecessary serial dependencies introduced merely to increase the number of modules.

The development scaffold also builds and runs its module interface, implementation, consumer, and `import std` with both GCC 16.2.0 and Clang 23.1.5, in separate trees. Clang-tidy 23.1.5 uses the Clang compilation database after BMIs and module maps are built and directly checks all three owned units. GCC ASan+UBSan is separately validated. The local Clang ASan runtime is missing, so its sanitizer link does not pass; do not infer sanitizer support from a successful ordinary Clang module build.

Small isolated GCC and Clang builds have also confirmed no-op rebuilds and the implementation-versus-interface rebuild behavior above. Separate CMake 4.5 probes rejected invalid private-module imports, demonstrated repeated compilation of an ordinary `PUBLIC` source, and produced different `std` BMI variants for consumers with different optimization options. These results establish behavior for those small examples, not performance or complete compatibility for the future solver.

Build success and runtime library discovery are separate checks. In a local non-login environment, the GCC-built smoke executable failed to find a required `GLIBCXX` version until the existing runtime environment was supplied. Verify test execution and GoogleTest discovery in the environment that will actually run them, including CI; do not hardcode a developer's library paths into shared configuration.

## 5. External Dependencies and SAT Backends

### 5.1 Dependency Sources and Network Policy

Dependency selection and the SAT abstraction belong to solver architecture decisions. CMake is responsible for providing reproducible and replaceable ways to build the selected dependencies.

The scaffold implements the development, documentation, GMP, GoogleTest, and Google Benchmark options below. SAT backend and IPO options remain proposals.

| Option | Status, meaning, and default |
| --- | --- |
| `DANDELION_BUILD_TESTS` | Implemented: ON for top-level development, OFF as a subproject; the caller may override it. |
| `DANDELION_BUILD_TOOLING_TESTS` | Implemented: ON at the top level, OFF as a subproject; effective only when tests are enabled. Tooling tests require POSIX, Python 3.8+, clang-format 23.1.5, and clang-tidy 23.1.5. |
| `DANDELION_BUILD_BENCHMARKS` | Implemented: OFF by default; enable component benchmark targets and their Google Benchmark dependency. |
| `DANDELION_BUILD_FUZZERS` | OFF by default. Build the frontend session fuzzer with Clang and libFuzzer. |
| `DANDELION_BUILD_DOCS` | OFF by default. Enable the `dandelion_docs` target with Doxygen 1.17.0 or newer and optional Graphviz `dot`. |
| `DANDELION_WARNINGS_AS_ERRORS` | Implemented: OFF by default; only explicitly listed owned sources receive `-Werror`. |
| `DANDELION_ENABLE_SANITIZERS` | Implemented: ON by default for newly configured top-level single-configuration GCC Debug builds, OFF otherwise. The `debug` and `gcc-asan-ubsan` presets enable ASan+UBSan; `release`, `relwithdebinfo`, and `clang-debug` disable them. Users may override the option. |
| `DANDELION_ENABLE_COVERAGE` | OFF by default. Instrument project sources with Clang source-based coverage and link the profile runtime. |
| `DANDELION_DEPENDENCY_MODE` | Implemented for GMP, GoogleTest, and Google Benchmark: `EXISTING` or `FETCH`, default `EXISTING`. Existing dependencies are preferred in either mode; `FETCH` permits acquisition when needed. The cache retains the choice until changed. |
| `GMP_ROOT` | Optional prefix of an existing GMP installation. |
| `GMP_INCLUDE_DIR`, `GMP_LIBRARY` | Optional explicit GMP header directory and library file; set both together. |
| `DANDELION_GMP_SOURCE_DIR` | Optional clean, standalone GMP source tree, version 6.3.0 or newer. |
| `DANDELION_GMP_ARCHIVE` | Optional local official GMP 6.3.0 `.tar.xz` archive, verified with the pinned SHA-256. |
| `DANDELION_GMP_BUILD_JOBS` | Positive number of nested GNU Make jobs for a source build; defaults to 1. |
| `DANDELION_GTEST_SOURCE_DIR` | Implemented: optional path to a standalone, user-provided GoogleTest source tree. |
| `DANDELION_BENCHMARK_SOURCE_DIR` | Implemented: optional path to a standalone, user-provided Google Benchmark source tree. |
| `DANDELION_SUBPROJECTS_DIR` | Project-managed dependency source directory, default `<source>/subprojects`; may be moved to a writable location. |
| `DANDELION_ENABLE_IPO` | Proposed: disabled by default. When implemented and enabled, check toolchain support and enable it only for selected project targets. |

Decide how to enable each SAT backend, specify library paths, and report errors when the first backend is selected. System packages, source caches, and download mechanisms differ, so do not assume a single CMake offline option controls every external build.

1. **Local/system sources**: Allow users to provide installed libraries and header paths. Use `find_package()` and imported targets when a reliable CMake package exists. If a required dependency is missing, clearly explain what is missing and how to specify its location; do not silently select the wrong version.
2. **Source builds**: Allow users to explicitly choose to fetch and build pinned versions. Pin Git dependencies to commits and archives to fixed URLs and checksums. Record the version, retrieval method, necessary patches, and license. Do not treat a moving default branch as a reproducible version.
3. **Offline use and updates**: Default configuration should not access the network unexpectedly. Distinguish initial downloads, updates, and offline reuse of existing sources. Support pre-prepared dependencies without network access; do not fetch the newest version on every reconfiguration.
4. **Isolation**: Keep third-party sources and build directories in the build tree or user-selected directories. Do not accidentally commit generated libraries, object files, or downloaded sources. Project-wide settings must not inadvertently change third-party compile options, warnings, or tests.
5. **Upgrades and distribution**: Check the upstream API, version, license, and required build options when upgrading. Preserve required copyright and license information for distributed versions.

CMake recommends mechanisms such as `find_package()`, `FetchContent`, and dependency providers to accommodate different sources, provided the chosen mechanism fits the upstream project. Give each dependency one build owner and make its target available to all consumers; do not compile the same dependency independently for every test. `FETCHCONTENT_UPDATES_DISCONNECTED` does not prevent an initial download, and `FETCHCONTENT_FULLY_DISCONNECTED` assumes content is already populated. Ensure a truly offline configuration does not enter a download path. [CMake dependency guide](https://cmake.org/cmake/help/latest/guide/using-dependencies/), [FetchContent controls](https://cmake.org/cmake/help/v4.5/module/FetchContent.html#variables)

#### GMP

GMP **6.3.0 or newer is required**, including when project tests are disabled. `cmake/DandelionGMP.cmake` provides `GMP::GMP` for consumers. It uses a parent-provided target first, then explicitly supplied clean source or a local archive, explicit installed paths, an installed `pkg-config` package, other discoverable installed paths, and finally a project-managed source. An invalid explicit selection fails instead of silently replacing it. A non-imported parent-provided `GMP::GMP` target must declare `GMP_VERSION` of at least 6.3.0; its actual headers are checked when the GMP smoke test is compiled.

For an installed library, CMake checks the header version and compiles and links a public API introduced in GMP 6.3.0; native configurations also execute a check of the loaded runtime version. Cross-compilation skips that runtime check, so it must be validated on the target. Header, library, ABI, and runtime search paths must refer to compatible builds. If no compatible GMP exists, `EXISTING` fails with instructions; `FETCH` may retrieve the official 6.3.0 archive. The archive SHA-256 is pinned in the helper. A provided archive is verified with the same hash and works offline; a clean source directory may provide a newer version. GMP source trees configured in place are rejected rather than modified.

GMP uses Autoconf and GNU Make. The managed source is staged and atomically published under `DANDELION_SUBPROJECTS_DIR` with a lock; each CMake build tree uses its own GMP build and private install prefix. Source trees are treated as immutable by the external-project stamps: after editing a user-provided tree, use a fresh build directory. Source builds currently require a native single-configuration build and a C compiler; cross or multi-configuration builds should supply a compatible installed GMP. GMP's native configure may select CPU-specific optimizations, so its output is not automatically portable to other machines. The fallback builds the C library as static PIC and does not build `gmpxx`; the choice of a C++ wrapper is separate. It does not install to a system or user prefix. External source code does not inherit Dandelion's sanitizer or warning policy. Nested GNU Make parallelism is controlled by `DANDELION_GMP_BUILD_JOBS`, not Ninja's `-j`.

GMP 6.3.0's configure tests assume pre-C23 C semantics; GCC 15 and later default to C23. For GMP 6.3.x with GNU and Clang C compilers, the source build selects `-std=gnu17` while Dandelion remains C++23. This has been checked with the local GCC 16 and Clang 23 toolchains. See the [GMP configure report](https://gmplib.org/list-archives/gmp-bugs/2025-December/005607.html) and [GCC 15 changes](https://gcc.gnu.org/gcc-15/changes.html).

```sh
# Use a locally installed GMP without network access.
cmake --preset debug -DGMP_ROOT=/path/to/gmp-prefix

# Alternatively, prepare and build the pinned GMP source locally.
cmake --preset debug -DDANDELION_GMP_ARCHIVE=/path/to/gmp-6.3.0.tar.xz \
  -DDANDELION_GMP_BUILD_JOBS=16

# Allow downloads for missing dependencies when explicitly desired.
cmake --preset debug -DDANDELION_DEPENDENCY_MODE=FETCH
```

GMP 6.3.0 is available under LGPL 3.0-or-later or GPL 2.0-or-later; distribution must follow the selected license. [GMP copying conditions](https://gmplib.org/manual/Copying)

#### GoogleTest

When tests are enabled, `cmake/DandelionGoogleTest.cmake` resolves GoogleTest in this order: a parent-provided `GTest::gtest` or `GTest::gtest_main` target; an explicitly supplied source tree; an installed package found with `find_package(GTest)`; the project-managed source; and, when available, the standard `/usr/src/googletest` source tree. `GTest_ROOT`, `GTest_DIR`, and `CMAKE_PREFIX_PATH` can guide package discovery. Installed packages must be compatible with the selected compiler, standard library, and ABI. A parent-provided target is not replaced. Explicit source paths must point to a standalone GoogleTest source root; LLVM's modified internal copy is not such a root.

When no usable source or package is found, `EXISTING` fails with instructions. `FETCH` downloads GoogleTest 1.18.0 from the official repository, pinned to commit `063de7e9578f82b369302001269680b4b1553359` and checked against the SHA-256 in the helper. It stages and publishes sources under `DANDELION_SUBPROJECTS_DIR`, using a lock for concurrent configurations; a completed managed copy is reused offline. An unexpected existing directory or provenance marker is reported rather than overwritten. Each CMake build tree compiles its own dependency objects. The integration disables upstream GoogleMock, tests, and installation for the embedded copy without forcing parent cache options. No `cmake --install` into user or system locations is performed. GoogleTest 1.18.0 is distributed under the BSD 3-Clause License; check the actual selected dependency when distributing a build.

For example, if no local GoogleTest is available, explicitly allow the project-local download while configuring:

```sh
cmake --preset debug -DDANDELION_DEPENDENCY_MODE=FETCH
cmake --build --preset debug
ctest --preset debug
```

To return that build tree to the offline policy, reconfigure with `-DDANDELION_DEPENDENCY_MODE=EXISTING`. To use prepared sources instead, set `-DDANDELION_GTEST_SOURCE_DIR=/path/to/googletest`. With `DANDELION_BUILD_TESTS=OFF`, GoogleTest is not searched for, downloaded, or built. This policy concerns software dependencies; the separate dataset policy remains in force.

#### Google Benchmark

Google Benchmark is required only when `DANDELION_BUILD_BENCHMARKS=ON`.
`cmake/DandelionBenchmark.cmake` first uses a
parent-provided `benchmark::benchmark` target, then an explicit
`DANDELION_BENCHMARK_SOURCE_DIR`, an installed CMake package of version 1.10.0
or newer, and finally a project-managed source with a matching provenance marker. User-provided targets
and source trees must provide a compatible API and ABI. Explicit invalid sources
and unexpected managed sources fail instead of being replaced.

When the dependency is missing, `EXISTING` reports how to provide it. `FETCH`
permits download of the official Google Benchmark 1.10.0 archive at commit
`a6b4a9f00b36c19af9997d68a1cf6e0b5b3d032c`, checked against the SHA-256 in the
helper. Source acquisition is locked, staged and published in
`DANDELION_SUBPROJECTS_DIR`; each build tree builds its own library. Embedded
upstream tests, downloads, and installation are disabled. Nothing is installed
into the user or system environment. The pinned release is under the Apache 2.0
license; check the actual selected dependency before distribution.
The provenance marker records the acquired commit; it does not detect later
edits to files in the managed source tree.

The Node replay library also builds with project tests enabled and has
no Google Benchmark dependency. Corpus capture and performance measurements
are explicit commands. See [component benchmark usage](benchmarks/README.md)
for the tests-off Release build, workload selection, and diagnostic checks.

#### Doxygen

With `DANDELION_BUILD_DOCS=ON`, CMake requires Doxygen 1.17.0 or newer and
enables diagrams if it finds Graphviz `dot`. Make the tools available on
`PATH` before configuring. The `dandelion_docs` target runs on explicit
request and generates HTML from the public C++ module file sets of
`dandelion_util`, `dandelion_node`, and `dandelion_frontend`, the Markdown
under `docs/`, and `AGENTS.md`, which those pages reference. It uses
the [solver architecture](architecture.md) as the landing page. See the
[API documentation convention](cpp-style.md#api-documentation) for public
module comments.

```sh
cmake -S . -B build/docs -G Ninja -DDANDELION_BUILD_DOCS=ON \
  -DDANDELION_BUILD_TESTS=OFF -DDANDELION_ENABLE_SANITIZERS=OFF
cmake --build build/docs --target dandelion_docs
test -f build/docs/docs/html/index.html
```

The documentation acceptance check requires this build to exit successfully
with zero Doxygen warnings. The target applies `WARN_AS_ERROR = FAIL_ON_WARNINGS`
and regenerates the pages on each invocation without compiling the libraries.
Verify that a default configuration works without Doxygen and that the
documentation build succeeds with or without `dot`.

### 5.2 How CaDiCaL and Kissat Are Built

Do not assume every third-party library becomes a usable target through `FetchContent_MakeAvailable()`. The examined CaDiCaL `rel-3.0.1` and Kissat `rel-4.0.2` use `configure` and Make to produce `libcadical.a` and `libkissat.a`, respectively. These versions are research examples, not selected project dependencies. Their configure scripts rely on a source/build relative layout and write proxy makefiles or links in the source tree; protect user-provided source with a build-owned staging copy or another verified integration approach. Follow and recheck the instructions of the version actually selected. [CaDiCaL configure, rel-3.0.1](https://github.com/arminbiere/cadical/blob/rel-3.0.1/configure), [Kissat configure, rel-4.0.2](https://github.com/arminbiere/kissat/blob/rel-4.0.2/configure)

Provide a Dandelion adapter for each integrated backend:

- Higher-level code should depend on a stable SAT adapter interface and its target, without propagating source locations or handwritten link flags.
- When users supply a prebuilt library, verify that the headers and library are available and create an imported target. For source builds, evaluate `ExternalProject_Add()` or another explicit integration method.
- For Ninja, declare actual `BUILD_BYPRODUCTS`, such as generated libraries, as well as build order and include directories. Distinguish artifacts built with incompatible toolchains, configurations, or ABI options. A download step alone does not make a library available. [ExternalProject documentation](https://cmake.org/cmake/help/v4.5/module/ExternalProject.html)
- Ninja builds the main project, but upstream may require GNU Make; do not assume `CMAKE_MAKE_PROGRAM` is a Make executable usable by upstream. Control nested build parallelism so Ninja and the sub-build do not independently multiply concurrency without bounds.
- Explicitly pass and verify relevant settings, including compiler, target architecture, build type, static/shared linking, PIC, and sanitizers. Validate cross-compilation and ABI compatibility separately against upstream capabilities. Follow any special build-layout requirements of the pinned upstream version.
- `ExternalProject` normally tracks steps and timestamps. When users may modify external source, design update and rebuild behavior explicitly. `BUILD_ALWAYS` can be useful when the sub-build must check its own dependencies, but measure its no-op build cost rather than enabling it by default.

Do not assume CaDiCaL and Kissat expose identical interfaces or capabilities. Even a single user query can require several SAT calls, added constraints, variable restoration, or search-time propagation. Use the selected version's API and tests to express required capabilities and report unavailable functionality clearly. The number of enabled backends and the default backend remain open until solver requirements and performance measurements are available.

## 6. GoogleTest and CTest

GoogleTest is the required framework for all project test suites, including unit, component, integration, tooling, and corpus regression tests. CTest is the build system's test discovery and execution tool; it can run GoogleTest tests and is not an alternative assertion framework. Helper scripts and external programs may be used by GoogleTest cases where needed. [Testing strategy](testing/README.md), [CMake GoogleTest module](https://cmake.org/cmake/help/v4.5/module/GoogleTest.html)

The Node and util static libraries build independently of tests and propagate
sanitizer runtime link requirements to their consumers. Enabling tests adds
`dandelion_node_tests` and `dandelion_util_tests`, with labels `node` and
`util`. The util suite runs the isolated GMP failure helper.
[Node acceptance](testing/node-acceptance.md)

The scaffold builds a shared GoogleTest main target, a module and GMP smoke executable, and, when enabled, the developer-tool test executable. The latter invokes the real developer tools and a small Python source-discovery helper; assertions and test cases are in GoogleTest. GoogleTest dependencies do not receive the project's warning or sanitizer compile policy. When an existing prebuilt GoogleTest or GMP is not instrumented, sanitizer coverage does not include its internals.

Group test sources into executables around meaningful components rather than one executable per case or a single mandatory binary for all tests. Link only the tested components and the appropriate GoogleTest target. Frontend and expression tests must not require SAT or solver dependencies when their contracts do not use them. Test selection, source lists, and dependency discovery must preserve this separation during configuration as well as compilation.

The current executables use `gtest_discover_tests()` with `PRE_TEST` and the
`node`, `util`, `frontend`, `benchmark-infrastructure`, `toolchain`, and
`tooling` labels. Discovery executes each binary at test time and requires
its runtime dependencies. `POST_BUILD` is another option where suitable. If
many short cases make individual CTest process startup costly, measure a
grouped registration that still runs GoogleTest, accepting coarser CTest
reporting. A CTest label filters the tests selected for a run.

Corpus regression tests use GoogleTest as well, with helper runners where useful. Make their selection and resource limits explicit, and keep them out of the default small test set through actual registration or configuration controls, not labels alone. Follow the [local dataset policy](testing/README.md#local-benchmark-data), including metadata-safe reads and safe copies for external solvers; do not download datasets without explicit user permission. Missing required inputs must be reported, while unavailable optional inputs must not be recorded as passing tests.

### Clang fuzzing and source coverage

The `clang-fuzz` preset enables `DANDELION_BUILD_FUZZERS` and disables
GoogleTest by default. The frontend session harness reads length-delimited
inputs through `Session::FromText` and stops at EOF or the first diagnostic.
Its sources and the util, Node, and frontend libraries receive
`-fsanitize=fuzzer-no-link`. The harness executable links with
`-fsanitize=fuzzer`. Other consumers of the instrumented libraries link the
sanitizer coverage callbacks through the Clang driver and supply their own
entry point.

```bash
CXX=clang++ cmake --preset clang-fuzz
cmake --build --preset clang-fuzz
corpus=$(mktemp -d build/clang-fuzz/corpus.XXXXXX)
cp tests/benchmark/fixtures/capture{,_theories}.smt2 "$corpus/"
build/clang-fuzz/tests/fuzz/dandelion_frontend_session_fuzzer \
  -seed=1 -runs=256 -max_len=4096 -timeout=5 \
  -artifact_prefix=build/clang-fuzz/ "$corpus"
```

The `clang-coverage` preset enables source coverage with GoogleTest. Use
`llvm-profdata` and `llvm-cov` from the same LLVM release as Clang. For a
report from the frontend suite, run the following commands.

```sh
CXX=clang++ cmake --preset clang-coverage
cmake --build --preset clang-coverage
profiles=$(mktemp -d build/clang-coverage/profiles.XXXXXX)
LLVM_PROFILE_FILE="$PWD/$profiles/%m-%p.profraw" ctest --preset clang-coverage -L frontend
llvm-profdata merge -sparse "$profiles"/*.profraw -o "$profiles/coverage.profdata"
llvm-cov report build/clang-coverage/tests/frontend/dandelion_frontend_tests \
  -instr-profile="$profiles/coverage.profdata"
```

## 7. Build Speed and Resource Use

### 7.1 Optimize the Dependency Graph First

First ensure incremental builds are correct, then optimize build time:

- Separate private implementations from stable interfaces; avoid exposing frequently changed implementation details to many consumers. C++ modules also require attention to interface changes and module dependency depth.
- Avoid unnecessary target dependencies and code-generation prerequisites to preserve parallelism. Do not sacrifice project clarity simply to increase the number of targets.
- Use `add_custom_command(OUTPUT ...)` for generated code, accurately declaring inputs, generator tools, outputs, necessary `BYPRODUCTS`, and dependencies; use `DEPFILE` and `VERBATIM` where needed. Generate into the build tree and avoid rewriting unchanged content. If unchanged output timestamps would repeatedly rerun a generator, use a tracked stamp and declared byproducts, and verify recovery of deleted outputs. Generated module sources must exist before dependency scanning.
- Add dependencies only to targets that consume generated files. Do not replace incrementally tracked output rules with global custom targets that run every time.
- Use `ninja -d explain` to identify rebuild causes, `ninja -t graph` to inspect dependencies, and `cmake --build ... --verbose` to inspect actual commands. Use CMake profiling to diagnose unusually slow configuration.
- Allow developers and CI to choose parallelism based on CPU and memory limits rather than hardcoding it for one machine. Consider Ninja job pools when measurements show excessive resource use by specific compilations, linking, or generators; verify whether the selected pools cover synthetic BMI targets and external build steps. An external Make sub-build can itself run multiple jobs inside one Ninja job, so its concurrency needs an explicit budget. [CMake Ninja job pools](https://cmake.org/cmake/help/v4.5/prop_gbl/JOB_POOLS.html)

### 7.2 Caches, PCH, and Unity Builds

- A compiler cache is an optional optimization, not a prerequisite for a correct build. The October 2026 [ccache manual](https://ccache.dev/manual/4.14.2.html) explicitly states that standard C++20 modules are unsupported; do not confuse its limited support for Clang's non-standard modules with support for standard C++ modules.
- The [sccache upstream documentation](https://github.com/mozilla/sccache#c20-modules) currently lists partial C++20 named-module support for Clang only; GCC and MSVC modules are not yet supported. Do not assume it is effective or correct for GCC 16 and CMake module builds.
- Caches may still help third-party sources that do not use standard modules. Verify cache hit rates and result correctness for the specific compile commands. Re-measure when future versions improve; do not weaken checks to hide problems.
- Treat precompiled headers (PCH) and unity builds as target-specific experiments. They may reduce repeated parsing, but may also increase memory, expand rebuilds, or expose naming conflicts. CMake excludes module-scanned sources from unity builds. Test with the current toolchain and real code instead of enabling these globally by default.
- BMIs/CMIs are intermediate artifacts tied to the toolchain and build configuration. Do not assume they can be shared across compilers, versions, standard libraries, or incompatible options. Include standard-library module costs in measurements when separate build directories generate them independently.

### 7.3 Build Types and Solver Runtime Performance

Fast compilation and fast solving are different goals:

- `Debug` is intended for debugging, `Release` for optimized execution, and `RelWithDebInfo` for optimization with debug information. Check generated commands for actual compiler options. Release-like configurations usually define `NDEBUG`, so tests must not rely solely on `assert`, which may be disabled.
- Enable ASan+UBSan by default for the GCC `debug` preset; use separate build directories for different instrumentation settings. `release` and `relwithdebinfo` leave sanitizers disabled. The local `clang-debug` preset also leaves them disabled because the Clang sanitizer runtime is not installed. Apply compile options to selected project targets and include the corresponding options in the final link. Undefined-behavior diagnostics terminate with a failure status. Do not compare sanitizer runtime directly with production performance benchmarks.
- `debug` and `gcc-asan-ubsan` currently use separate trees and have overlapping intended GCC sanitizer configurations. Keep the existing presets functional; whether two trees should remain distinct is an open configuration and disk-use question.
- IPO/LTO may improve solver performance but can significantly increase link time and memory use. Enable it only for explicitly selected configurations or targets, use `CheckIPOSupported`, and measure both build cost and runtime benefit. [CMake IPO check](https://cmake.org/cmake/help/v4.5/module/CheckIPOSupported.html)
- `-march=native`, PGO, and alternate linkers are environment-dependent optimization experiments, not defaults for portable builds. Record PGO training data, compiler, training procedure, and validation procedure; do not present training-set gains as generalization.
- Do not assume `-O3`, LTO, CPU-specific instructions, or a particular linker is always fastest. Compare correctness, runtime, and memory on representative workloads across the project's supported theories and combinations, and preserve the build configuration. Follow the [dataset policy](testing/README.md#local-benchmark-data).

## 8. Measurement and Acceptance

The development scaffold is available. Establish a solver baseline before comparing optimizations on real solver code. Run tests in isolated temporary workspaces or dedicated build directories so developers' active files are not modified.

| Scenario | What to observe |
| --- | --- |
| Initial configure | Configuration time; dependency provenance; behavior with existing sources and no network; whether the requested compilation database exists. |
| Frontend-only configuration | Whether unrelated SAT dependencies are neither required nor built, and selected GoogleTest cases can run. |
| Clean full build | Wall time, CPU time, total peak memory, disk use, and costs of module scanning, BMIs, compilation, archives, and linking. |
| Rebuild without changes | Whether there is truly no work; otherwise identify targets, generators, discovery steps, or external builds incorrectly considered out of date. |
| Change one implementation file | Which sources recompile and which libraries or executables relink. |
| Change a public or module interface | Which consumers are affected, the number of recompilations, and elapsed time. |
| Change a generated input or remove an output | Whether the necessary generation, scanning, and dependent rebuilds occur without unrelated work or competing generators. |
| Change build type or toolchain | Whether build directories are isolated and incompatible BMIs/object files are correctly regenerated. |
| Change parallelism | Wall time, CPU use, and total build memory, including external sub-builds. |
| Instrument a static library | Whether a consumer receives the required runtime link options and executes correctly. |
| Enable optional optimizations | Record build time, memory, cache hits, and solver benchmark performance separately. |

When comparing results, record the source commit and relevant uncommitted changes or hashes, dependency versions, actual tool paths and versions, standard library, hardware and resource limits, build type, compile and link options, and parallelism. Record cache state, dependency acquisition, machine load, and other environmental differences. Repeat runs and report a distribution or median; do not claim a speedup based on one small sample. Build performance can be assessed without solver benchmark datasets. Large experiments require an explicit user request.

Measure the entire concurrent build process rather than treating one process's maximum RSS as the aggregate peak. Where an isolated cgroup is available, its `memory.peak` includes the cgroup's accounted memory, which may include file cache; otherwise, sampled process-tree RSS or PSS is approximate and has sharing and sampling limitations. Record the measurement method and its limitations.

Acceptance for the future solver build includes successful clean configurations and builds with the supported compilers, runnable GoogleTest suites through CTest, no rebuild when nothing changes, correct implementation and interface change propagation, frontend-only configuration, correct sanitizer linkage, clear missing-dependency errors, and offline configuration with pre-existing dependencies. Verify explicit compilation-database overrides and parent-project choices. The scaffold's GoogleTest and developer-tool evidence is described in [cpp-style.md](cpp-style.md); the standalone node configuration and sanitizer link propagation are now verified in [node acceptance](testing/node-acceptance.md). The [frontend acceptance record](frontend/implementation.md) covers standalone and downstream module builds. SAT dependencies and solver performance remain future work.

## 9. Open Decisions

1. Determine actual target boundaries for expressions, frontend, SAT adapter, solver, tests, and executable as their responsibilities become concrete.
2. Decide which SAT capabilities are required, the first backend, optional backend combinations, and exact SAT dependency versions.
3. Determine which internal modules require cross-target visibility and whether a stable external library interface will be provided.
4. Measure module boundaries, incremental builds, build parallelism, sanitizer configurations, and BMI costs on representative code. Decide whether the overlapping GCC sanitizer presets need separate trees.
5. Evaluate caches, PCH, LTO, PGO, linkers, and CPU-specific options with appropriate correctness and performance measurements.

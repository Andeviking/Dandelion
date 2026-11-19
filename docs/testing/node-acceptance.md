# First-version node acceptance

Status: **implemented and verified**, updated 2026-11-19. This is expression-system
acceptance, not parser, arithmetic evaluation or solver acceptance. The
[feature inventory](node-inventory.md) records every publishable Kind, sort,
declaration category, applicable dimensions and deferred frontend checks.

## Node rectification verification

The [rectification regression inventory](node.md#6-node-rectification-regressions)
defines the updated Node contract. Targeted verification builds the Node library,
Node tests, and benchmark tests without frontend consumers. Frontend callers
using the earlier count or index types require the subsequent frontend change.

The first test-first GCC build rejected a 64-bit datatype slot passed to
the previous 32-bit interface. The first full target run identified an
obsolete declaration footprint oracle. A record now contains three 64-bit
IDs, a 16-byte string view, an 8-byte datatype pointer, and a 1-byte kind.
The independent ABI size is therefore 56 bytes, exceeding the previous
48-byte cap. The revised cap still allows exactly one declaration record
per empty-domain declaration, and the test still requires zero domain spans.


The operator matrix now runs its width and index checks for 48 applicable
cases. Every builtin still has independent structure, arity, and payload
checks. The declaration category test uses fixture-owned signatures for
its successful and failing applications.

Verification on 2026-11-19 used GCC 16.2.0, Clang 23.1.5, GMP 6.3.0, and
existing dependencies. The targets were \`dandelion_node\`,
\`dandelion_node_tests\`, \`dandelion_node_workload_tests\`, and
\`dandelion_benchmark_integration_tests\`. CTest selected the \`node\` and
\`benchmark-infrastructure\` labels.

| Configuration | Node passed | Benchmark passed | Skipped | Failed |
| --- | ---: | ---: | ---: | ---: |
| GCC Debug with ASan and UBSan | 468 | 21 | 4 | 0 |
| Clean Clang Debug with Google Benchmark enabled | 468 | 25 | 4 | 0 |

The four skipped cases need opt-in reference or real-workload inputs.
The GCC Release configuration with tests disabled built the Node library
and the benchmark executable. Its JSON dry run returned two benchmark entries
with zero errors or skips. The project formatter check passed for all 65 C++
files.

The original first-version acceptance record follows.

## Implemented components

`dandelion_node` is a C++23 static library with public `dandelion.numeric` and
`dandelion.node` modules and one private `dandelion.node:internal` partition.
Shared internal types are defined once in that partition. GMP is a private
link dependency; no SAT backend is found or linked.

The library represents and checks all 108 specified node kinds: six dedicated
literal/application kinds and 102 builtin kinds. This includes Bool, exact
unbounded Int and exponentiation, all selected BV operations and exact indices,
all selected FP operations and conversions, symbolic rounding modes and
concrete rational-to-FP payloads, arrays and constant arrays, uninterpreted
sorts/functions, and inductive datatypes. All eight sort and six declaration
categories have actual tests, including their supported combinations.

The implementation includes canonical small/GMP integers and normalized
rationals with read-only views; independent literal and index payloads; sort
and declaration stores; concrete declaration instances; productive recursive,
mutually recursive and parameterized datatype transactions; a weak flat unique
table with cached hashes, linear probing, growth and backward-shift deletion;
owning handles and borrowed ranges; checked reference/identity/capacity limits;
immediate iterative reclamation; and an owning iterative event-based DAG walker.
No generic table, collector or allocator plugin framework was introduced.

## Test-first record and independent expectations

The initial worktree had build scaffolding and no node implementation. The
inventory and an initial 445-case GoogleTest suite were written before
production sources. The initial build failed at the missing `dandelion.node`
module, as expected. Tests were not disabled or replaced with fake interfaces.
Boundary, category, failure and integration-foundation review added 21 more
cases and strengthened assertions, resulting in 466 discovered node tests.

Expectations come from the documented signatures and identity/lifetime
contracts, hand-derived small FP bit classifications, exact decimal values,
and an independent reference map for table survivors. All 102 builtin Kinds
have four actual named parameterized cases, checked against the discovered
CTest names. Other cases cover literals, all sort/declaration categories,
transaction rollback, every project allocation checkpoint, reference prefixes,
last IDs and capacity limits, repeated edges, ownership after facade destruction,
20,000-deep DAGs, 5,000-deep finite recipes and exact walker event order.

The Rational assignment regression was observed failing before its fix: a
second C++ allocation failure left a new numerator with the old denominator.
Copy-and-move assignment preserves the destination and normalized-value
invariant. A separate failing move regression exposed 0/0 in moved-from
Rationals and in a published FP-conversion payload; custom allocation-free
moves now leave the source at 0/1. Small and GMP moves are tested under
allocation-failure control, including move assignment and self-move. Numeric
and manager-creation fault-control tests also initially failed to compile until their test-only controls were implemented. GMP limb
OOM is checked separately by a bounded subprocess requiring SIGABRT. It is
never counted as a recoverable C++ exception.

Initial test-source corrections bound temporary owners for the full expression
because Borrow on rvalues is deleted by contract. Compiler-warning and naming
corrections changed no expected results. No case was disabled, deleted or
weakened to obtain passing implementation results.

## Required verification results

The selected environment is Linux x86-64, GCC 16.2.0, Clang 23.1.5,
libstdc++ 16.2.0, GMP 6.3.0, GoogleTest 1.18.0, CMake 4.5.0 and Ninja 1.13.2.
All configurations use existing dependencies and project warnings as errors.

| Configuration | Node tests | Existing applicable tests | Result |
| --- | --- | --- | --- |
| GCC Debug, ASan + UBSan | 466 | 4 toolchain + 37 tooling | 507 passed |
| Clang Debug, sanitizers off | 466 | 4 toolchain + 37 tooling | 507 passed |
| GCC Release, NDEBUG | 466 | 4 toolchain + 37 tooling | 507 passed |
| GCC RelWithDebInfo, NDEBUG | 466 | 4 toolchain + 37 tooling | 507 passed |

Every run used CTest with `--no-tests=error`. JSON discovery under label `node`
finds exactly the same 466 names in all four builds; no skipped or disabled
case counts as a pass. Compilation commands confirm ASan/UBSan on the GCC
Debug node sources and NDEBUG in both release configurations. UBSan terminates
on errors. GMP and GoogleTest dependency internals are not instrumented.

`python3 scripts/check_cpp.py format-check` passes with clang-format 23.1.5
on all 35 owned C++ files. The Clang compilation-database tidy entry passes
with clang-tidy 23.1.5 on all 34 owned translation units, including public
interfaces, the private partition, implementations, test peer, helper and
consumers. Standard range names, heterogeneous lookup aliases and legitimate
struct fields use only the documented single-declaration naming exceptions.

Separate GCC and Clang product-consumer builds with tests disabled compile,
link and run. Their archives contain neither NodeTestPeer nor NumericTestPeer;
GoogleTest and SAT dependencies are absent. The GCC consumer itself has no
sanitizer compile options, while its final link receives the required runtime
options from the instrumented static library. It also receives GMP. Test-enabled
and test-disabled module artifacts are in separate build trees.

Commands used for the required matrix, after selecting each compiler on its
first configuration:

```sh
cmake --preset debug -DDANDELION_DEPENDENCY_MODE=EXISTING -DDANDELION_WARNINGS_AS_ERRORS=ON
cmake --build --preset debug -j 4
ctest --preset debug --no-tests=error -j 4

cmake --preset clang-debug -DDANDELION_DEPENDENCY_MODE=EXISTING -DDANDELION_WARNINGS_AS_ERRORS=ON
cmake --build --preset clang-debug -j 4
ctest --preset clang-debug --no-tests=error -j 4

cmake --preset release -DDANDELION_DEPENDENCY_MODE=EXISTING -DDANDELION_WARNINGS_AS_ERRORS=ON
cmake --build --preset release -j 4
ctest --preset release --no-tests=error -j 4

cmake --preset relwithdebinfo -DDANDELION_DEPENDENCY_MODE=EXISTING -DDANDELION_WARNINGS_AS_ERRORS=ON
cmake --build --preset relwithdebinfo -j 4
ctest --preset relwithdebinfo --no-tests=error -j 4

python3 scripts/check_cpp.py format-check
python3 scripts/check_cpp.py tidy --build-dir build/clang-debug
```

## Implementation clarifications and environment limits

The selected semantic scope and identity/lifetime/error contracts are retained.
Concrete read-only query names and walker event fields are now specified in
[design.md](../node/design.md). Owning state-plus-ID handles share a type-safe
template. Datatype commit stages a missing Bool sort for testers inside the
same transaction, so failure consumes no visible sort/declaration/group IDs.
Standalone numeric and pre-state creation controls are test-only and
thread-local; ordinary manager controls remain per state.

GCC 16.2 crashed internally while consuming defaulted module equality functions
for Node and later SortView. Explicit comparisons preserve the same pointer or
state-plus-ID identity and compile on both toolchains. The initial backtraces
and the failing Rational regression are retained with the raw evidence.
No semantic exception, narrowed theory scope or undocumented accepted input
was added to work around these issues. No performance advantage is claimed.

The optional Clang ASan/UBSan prerequisite probe **failed to link** because
`libclang_rt.asan_static.a`, `libclang_rt.asan.a` and `libclang_rt.asan_cxx.a`
are missing. Clang node tests with sanitizers were **not executed**; the required
Clang Debug run is uninstrumented and passed. No runtime was installed.

An additional no-change build check found a filesystem limitation: truncating
an already empty file does not advance its mtime here. A bounded independent
probe reproduced this with nanosecond timestamps. Ninja 1.13.2 StartEdge uses
such an empty `.ninja_lock` file for command-start timestamps, producing stale
recorded mtimes and extra recompilation/relinking passes. This is not reported
as a successful first no-op build. `ninja -C build/<preset> -t restat`, followed
by dependency metadata refresh, made all four subsequent builds report
`ninja: no work to do`. No installed tool or filesystem setting was changed.
This manual build-directory workaround may be needed after further builds
on this filesystem.

GMP's default limb allocation remains process-fatal. C++ allocation checkpoints
cover potentially allocating project operations and enclosing container work;
they may fire even when spare capacity avoids a real allocation. They do not
promise recovery from arbitrary GMP OOM or replace its global allocator.

## Deferred frontend integration

The node API tests verify declaration/formal identities, shared actual-argument
edges, concrete signatures and the typed testers/selectors/ite structures a
frontend can build. They do not execute a parser or substitution engine.
Source `let` simultaneity, definition capture/shadowing, match parsing and
exhaustiveness/duplicate-pattern order, :named closedness and command rollback,
qualified overload resolution, source occurrences, aliases, logic/profile
restrictions and parsing roundtrips remain frontend acceptance items.
Rewriter, SAT backend, theory procedures and model solving were outside this
task and are not claimed as implemented.

No dataset was accessed or downloaded, no dependency was fetched, and no large
benchmark or performance experiment was run. Existing uncommitted work was
preserved. No commit, push or PR was created.

## Local evidence

Raw logs and machine-specific fixtures are under the ignored
`build/node-evidence/` directory. Key artifacts include:

- `test-first-build.log`, `numeric-test-first.log`, `creation-test-first.log`;
- `gcc-defaulted-equality-ice.log`, `gcc-view-defaulted-equality-ice.log`,
  `rational-assignment-regression.log`, `rational-move-regression.log`;
- the four `*-all.log` CTest runs, `*-node-discovery.json`, and sorted
  `*-node-test-names.txt` manifests;
- `verification-summary.json`, `source-sha256.json`, `toolchain-and-flags.json`;
- `format-check.log`, `clang-tidy.log`, `clang-sanitizer-probe.log`;
- GCC/Clang consumer configure/build/run logs and archive/compile audits;
- `filesystem-mtime-probe.json`, `restat-verification.json`, and the original
  no-op build logs showing the extra work.

These artifacts record the local verification; the inventory and tests remain
the reviewable source of coverage obligations. Passing tests
and sanitizers are evidence, not a proof of correctness or solving capability.

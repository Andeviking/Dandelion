# Dandelion Testing Strategy

Status: **testing design for review**, not a description of completed solver
tests. GoogleTest integration, node tests, and small build-tool tests exist. The
solver and its solving test suites described here are not yet implemented.

This strategy applies to the complete scope in [AGENTS.md](../../AGENTS.md):
quantifier-free BV, FP, integer arithmetic, extensional arrays, UF, datatypes,
and their supported combinations. It complements the [solver architecture](../architecture.md)
and [build-system baseline](../cmake.md).

## Documentation organization

This file owns the common testing principles, result classifications, and
dataset policy. Each module-specific document describes the additional
behaviors, test cases, oracles, and coverage particular to that module; it
should refer to this strategy rather than duplicate it. The boundaries in the
documents are logical responsibilities, not required C++ classes or build
targets.

| Document | Responsibility |
| --- | --- |
| [frontend.md](frontend.md) | SMT-LIB parsing, names, typing, and representation of input. |
| [node.md](node.md) | Node, sort, declaration, unique-table, ownership, traversal, and frontend-boundary contracts. |
| [util.md](util.md) | Proposed exact Integer, BitVector, Rational, FP value, and numeric/node integration tests. |
| [benchmark.md](benchmark.md) | Optional Google Benchmark integration, dependency resolution, and Node workload smoke checks. |

Add a separate document for another module when its test design is specified.
Shared tests, such as theory combination and model validation, may have their
own documents; they are not a substitute for the individual theory tests. Do
not create empty documents merely to fill an outline.

## Principles

1. **Test observable contracts.** State the input, expected behavior, and why
   that expectation is valid. A test is valuable when it can distinguish a
   correct implementation from a plausible error; checking only that a call
   returns or reproducing the implementation's algorithm is not sufficient.
2. **Use independent evidence.** Combine small manually justified examples,
   reference implementations, invariants, and model or proof checks where
   appropriate. Each has limits, and agreement between tools is evidence rather
   than a mathematical proof.
3. **Cover both outcomes and boundaries.** Exercise valid cases, meaningful
   invalid cases, supported combinations, and explicit unsupported or
   incomplete behavior. A crash, timeout, skipped check, or unverified result
   must not silently count as a pass.
4. **Preserve the input's semantics.** The relevant result is that of the
   original query. Correct parsing, transformations, theory reasoning, and
   model recovery all contribute; a correct SAT backend alone is insufficient.
5. **Keep routine checks bounded and reproducible.** Small deterministic tests
   are suitable for ordinary development. Record seeds and resource limits for
   generated cases, and keep corpus runs explicit and bounded. Large experiments
   require an explicit user request.

## Test layers and tools

- **Unit tests** check a focused operation or invariant with a manually
  justified result. The project test framework is GoogleTest. Parameterized
  tests are useful where each case has a clear expected result.
- **Component tests** check interactions such as frontend-to-expression
  construction, a rewrite and its recovery information, or a theory procedure
  and its explanations.
- **Integration tests** use complete small inputs and check the applicable
  contract at component boundaries. A frontend test can succeed without any
  solving capability; it must not report a fabricated `sat` or `unsat`.
- **Corpus tests** exercise real local inputs and report coverage and failure
  categories separately. Corpus selection and test size are explicit, rather
  than an implicit full-dataset scan on every build.
- **Runtime instrumentation** such as ASan and UBSan complements these tests;
  it detects some defects in executed paths and does not prove correctness.

Register automated GoogleTest cases with CTest where applicable. Test binaries,
discovery, filtering, and dependencies must follow the
actual CMake integration. See [GoogleTest parameterized tests](https://google.github.io/googletest/advanced.html)
and [CMake's GoogleTest module](https://cmake.org/cmake/help/v4.5/module/GoogleTest.html).
Test checks must remain effective in release-like builds where `NDEBUG` can
disable ordinary C++ `assert`. The current integration and its dependencies are
documented in [cmake.md](../cmake.md#6-googletest-and-ctest).

### Clang instrumentation checks

- Run the ordinary Clang and GCC test suites with both optional
  instrumentation switches disabled.
- Check that the fuzz preset configures without GoogleTest and builds the
  frontend session fuzzer with libFuzzer. Inspect the compile database for
  instrumentation on the fuzzer and its project libraries. Check that
  standard-library modules and dependencies have no fuzz compile flags and
  that ordinary test executables can still link when tests are enabled.
- Smoke-test the harness with valid multi-command scripts, empty input, and
  malformed input including a NUL byte. Run a bounded mutation pass from
  the checked-in frontend fixtures with a fixed seed.
- In a coverage build, verify that project translation units have coverage
  flags, synthesized standard-library modules and dependencies have none,
  and final executable links include the profile runtime. Run CTest with
  distinct profile output paths, merge the profiles, and read a report
  with the matching LLVM tools.
- The tooling suite checks that naming skips fuzzer sources when disabled,
  checks them when enabled, and still rejects missing compile commands for
  unregistered sources when enabled.

## Choosing an oracle

| Evidence | Suitable use and limitation |
| --- | --- |
| Manually derived expectation | Small examples of exact types, operators, values, scope, results, and errors; derive the expectation independently of the code under test. |
| Properties and metamorphic relations | Check relations such as semantics-preserving renaming or transformations; establish the relation's preconditions and test meaningful cases. |
| Round-trip | Parse, serialize, and parse again, then compare the required semantics. Shared bugs or a lossy representation can still pass. |
| Differential test | Compare with an independent implementation on the common supported profile. Differences require investigation; another solver is not infallible. |
| Independent model validation | Check a satisfying interpretation against the original assertions. The checker must support the relevant theories and total interpretations. |
| Checked refutation or proof | Check that the claimed `unsat` follows from the original problem. A SAT proof alone does not validate frontend semantics, preprocessing, and theory lemmas. |

For a frontend with no solving capability, verify the resulting typed terms,
declarations, bindings, and command interpretation using the first four
methods. Where feasible, compare the meaning of the original and recovered
formula using a reference solver: under the same declarations and theory
semantics, a satisfiable exclusive-or demonstrates a discrepancy; an
unsatisfiable result is evidence of equivalence. An `unknown`, unsupported
feature, or failed comparison is inconclusive. Do not conflate syntactic
round-trip equality with semantic equivalence.

For a solver result, validate `sat` against the original assertions with a
compatible model checker where available. Check `unsat` with an appropriate
proof when available; otherwise use independent cross-checking and treat the
result as less strongly supported. A finite search that fails to find an NIA
model does not establish unbounded `unsat`. Tests do not require a complete
proof framework, but must not overstate what is verified.

## Coverage across the solver

The following are examples of essential responsibilities, not an exhaustive
test inventory or an implementation order:

| Area | Examples of behaviors to test |
| --- | --- |
| Frontend and expressions | Syntax, typed terms, declaration identity, scopes, exact literals, ownership, and well-defined unsupported-input handling. |
| Rewriting and preprocessing | Semantic preservation or the stated directional guarantee; composition of model recovery after elimination and lowering. |
| SAT and theory interface | Correct literal mappings, explanations, relevant reconstructed assignments, subsequent refinements, and backtracking when the selected integration uses it. |
| BV and FP | Small-width BV encodings and boundary cases; FP rounding, NaN, signed zero, core equality versus `fp.eq`, and rounding-mode domains. |
| Integer arithmetic | Exact unbounded arithmetic, integer feasibility, signed `div`/`mod` semantics, sound explanations, and `unknown` for unresolved incomplete cases. |
| Arrays, UF, and datatypes | Read-over-write and extensionality, congruence, constructor and tester semantics, exhaustiveness, cycles, and consistent model values. |
| Theory combination and models | Compatible shared interpretations, finite domains, generated witnesses, underspecified operations, total interpretations, and recovery of original assertions. |

Include cross-theory cases, not only isolated theory tests. For example, five
pairwise-distinct `(Array Bool Bool)` values are inconsistent, and an
arithmetic model's accidental equality between two shared terms is not by
itself a logical consequence. Expected results must be justified independently.
Completeness requirements remain those of the supported fragment in the
architecture; an incomplete algorithm must not turn a bounded failure into an
unjustified answer.

## Local benchmark data

The local `benchmark-2025` snapshot under `/pub/netdisk1/zhangx` is a target
workload for input compatibility. Select representative files from relevant
quantifier-free families and record their actual features; a directory or
`set-logic` name alone is not proof that every command or theory in a file is
within the supported input profile. The directory name is a local snapshot
label unless its upstream revision has been established.

Treat `/pub/netdisk1/zhangx` and everything under it as strictly read-only,
including metadata. Never create, modify, rename, delete, or change permissions
there. Reads and directory enumeration must also avoid changing access times:
on Linux, an appropriately authorized `O_NOATIME` open of both directories and
files can be used when the filesystem honors it. If the required guarantee
cannot be established, do not read or enumerate the data. Do not retry with a
metadata-changing read. Make any working copies, caches, manifests, minimized
cases, and outputs outside the dataset directory; external solvers must read the
safe copies rather than opening the source files themselves.

If a required dataset cannot be found, inform the user. Do not download, clone,
or otherwise retrieve a dataset without the user's explicit permission. A
missing optional corpus must be reported as unavailable or skipped, not as a
successful corpus run. Small project-authored fixtures can be stored with the
tests; large or copied datasets are not required for the default unit suite.

Record the selected relative paths and file hashes, snapshot identification,
relevant input profile, tool and configuration versions, seed if any, and time
or memory limits. Keep counts for passed, failed, unsupported, unavailable,
timed out, crashed, and inconclusive cases distinct. A status field in a
benchmark is an expected-result hint, not a proof. `:status unknown` imposes no
expected `sat` or `unsat` result; it does not require the solver to return
`unknown`. See [SMT-LIB 2.7](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-09-09.pdf).

## Changes and regressions

When fixing a correctness defect, preserve a focused regression case when it
captures the defect without unnecessary duplication. Keep a larger real-world
input's provenance and, if possible, an independently checked small reproducer.
Review the applicability and evidence of a test before promoting it into a
required check. Record unresolved disagreements and unavailable evidence rather
than converting them into passing tests.

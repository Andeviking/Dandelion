# Benchmark Integration Testing

Status: **test design for the Google Benchmark integration**. The benchmark
programs are performance workloads, not a replacement for GoogleTest.
This document applies [the common testing strategy](README.md) and the
test-first rules in [AGENTS.md](../../AGENTS.md).

## Dependency and build contracts

GoogleTest cases configure isolated CMake fixture projects with the actual
Dandelion root and resolver. The fixtures use a minimal synthetic benchmark
package or source tree and a parent-provided GMP target; they do not need
network access, compile the solver, or alter the real dependency cache. Each
case checks observable CMake configuration results, not the text of the
resolver implementation.

| Case | Required behavior and independent expectation |
| --- | --- |
| Disabled | With benchmarks disabled, an invalid explicit benchmark source and disabled package search do not prevent configuration; no benchmark target is created and no source directory is populated. |
| Parent target | A parent-provided `benchmark::benchmark` is reused even when a lower-priority source path is invalid. |
| Explicit source | An explicitly supplied source tree is added before package search. Its benchmark target is available and its upstream tests, downloads, and installation rules are disabled in the embedded scope. |
| Invalid explicit source | A missing or invalid explicitly selected tree fails with a diagnostic instead of silently falling back. |
| Installed package | A discoverable compatible CMake package is accepted when no parent target or explicit source is supplied. |
| Missing offline dependency | With no available dependency and `DANDELION_DEPENDENCY_MODE=EXISTING`, configuration fails with instructions to provide one or explicitly select `FETCH`; no managed source is created. |
| Managed source | A project-managed source with the pinned provenance marker is reused in offline mode; an invalid marker is rejected without altering the source. |

The explicit source and synthetic installed package identify their chosen path
in the configure output, making precedence independently observable. The parent
target and package cases must not create or replace a third-party target.
Fixture paths containing spaces are supported. The fixture package is not a
performance oracle and is never installed.

## Developer-tool scope

The formatting check must discover owned C++ files under `benchmarks/`, while
continuing to exclude third-party and generated sources. The naming check must
accept a build with benchmarks explicitly disabled without requiring their
compilation commands. When benchmarks are enabled, it must check those units
and reject a missing compilation command as it does for other owned sources.
GoogleTest fixtures exercise both configurations using a small Clang CMake
project and the actual developer-check script.

## Real library and workloads

In an explicitly enabled benchmark build, the actual pinned Google Benchmark
library is built and linked with the Node module. A GoogleTest case runs the
benchmark executable with `--benchmark_dry_run` and a temporary JSON output;
it requires both documented workloads to appear, with no error or skip. A
dry run checks only construction and reporting, not performance.

The `node/integer/hit` workload keeps an owning reference alive and verifies
that an identical construction resolves to the same node before timing. The
`node/integer/create_release` workload checks that the node is reclaimed and
a later construction obtains a new identity before timing. Each measured
iteration creates and releases its result, so memory does not grow with the
automatically selected iteration count. An unexpected factory error is
reported by the benchmark rather than counted as a valid measurement.

The project-required Node tests continue to verify value, identity, type,
and lifetime correctness. These workloads do not evaluate arithmetic, solve
queries, or infer solver performance. Real benchmark output, including
`error_occurred` and `skipped` fields, must be inspected; the benchmark
process exit status alone is insufficient.

## Acceptance

- Write and run the GoogleTest cases before adding the production resolver
  and workloads; initially missing targets and options may make them fail.
- Configure with benchmarks disabled and with a missing dependency in offline
  mode; verify the specified success or diagnostic without network access.
- With the user's explicit `FETCH` choice, acquire the pinned source in the
  project-managed directory, build the benchmark with tests and sanitizers
  disabled, and run its bounded dry run. Reconfigure offline to check reuse.
- Build and run the applicable tests with GCC and Clang, and run existing
  project tests appropriate to the change. Record unavailable checks and
  preserve all existing tests. No benchmark data set is required.

## Trace-derived workloads

Use hand-derived records in GoogleTest. The small records describe exact
Node API behavior and exercise the capture format and converter without a
corpus. They are diagnostic workloads.

| Boundary | Cases and independent expectations |
| --- | --- |
| Capture | A tiny SMT-LIB script contains an explicitly reused expression, an indexed extract, and separate assertions. In the opt-in reference run, inspect parse and solve phase boundaries, the extract payload, the hit's existing identity, ordered distinct roots, and definitions preceding references. A second run uses the reference parser alone and must retain the same ordered assertion roots with no preprocessing snapshot. Require the completion footer and successful subprocess status. Check a byte cap and forced timeout separately. |
| Conversion | Exercise BV extract order, exact large Int, FP signed zero and NaN, array qualifier, UF operator versus operands, and distinct datatype declarations and concrete instantiation. Source identity and ordered operands determine the expected Dandelion records. Count an unsupported internal kind and its dependent request. Reject absent roots, malformed operands, and incomplete traces. |
| Rational payload replay | Normalize an unreduced signed fraction to its exact numerator and positive denominator. Malformed payload, unknown declaration instance, and sort mismatch report their diagnostic categories and record line. |
| Replay | A graph with repeated child slots and an alias root gives the manually enumerated root, node, and edge event order. Verify ordered child sorts and indices, exact literals and declaration identities across small combined cases. |
| Ownership | Two distinct roots share a leaf. Temporary operand owners are gone after build. Dropping one root retains the shared leaf through the other; dropping the last root reclaims it. Use the test peer to count live nodes and reclamations. A mixed sequence repeats a factory while an alias holds it, drops the last owner, then creates the same structure with a new Dandelion ID. |
| Failures | Reject missing files, empty input, absent footer, extra records, undefined metadata or operands, bad payloads, unsupported kinds, incorrect expected types, and graph ownership leaks. Use an independent child that sleeps or exits by signal for process classification. |
| Reporting | A temporary Google Benchmark JSON fixture supplies finite iteration times, then a skip, an error and zero selections. Only the first is measurable. Run a small loaded workload with the actual Google Benchmark executable when enabled. Retain individual process samples and the reported units. |

Run all small tests with GCC and Clang. Verify formatter and naming checks
with benchmark sources enabled. Confirm that a tests-off Release binary
runs the bounded scenarios and that production runs never auto-select a
large workload. Capture overhead and real data acceptance are explicit
experiments. Record source safety, proofs of completeness, actual mapping
coverage, graph complexity, limits, raw results and failures.

The workload walk oracle uses the root occurrence ordinal on both root
events. The first draft used the node-event sentinel for those events.
The independent convention is specified in [Node query details](../node/design.md#9-implemented-query-details).
The conversion fixture includes allocation records for its datatype operator
IDs. The producer publishes those nodes before a resolved constructor record.
The opt-in capture check also uses a combined-theory parser fixture with a
parameterized datatype, an uninterpreted sort and function, a wide Int
literal, indexed BV, an array of FP, a closed rational FP conversion, NaN,
and a metadata string. The input snapshot must report every represented
theory. The parse-only command reports root observations without adding
long-lived root owners to the mixed workload.

An opt-in GoogleTest diagnostic accepts an explicitly selected generated
graph in `DANDELION_NODE_DIAGNOSTIC_WORKLOAD`. It checks unique-table
capacity before and after building a larger snapshot, verifies allocation
counts and root identities across repeated construction, and checks
reclamation with a partial root set and after the final release. It uses
the Node test peer. Record its counters independently of Release timings.

Check the RSS measurement path with a bounded child that touches a known
amount of memory. Require an RSS record from the child accounting tool.
Verify signal and timeout classification through that path as well.

For compact replay, use a graph that interleaves a literal payload, ordinary
and indexed operators, and a hold/drop pair. Use source metadata IDs above
32 bits. Check the surviving literal, parameter, child order, and shared node
identity. Check that replay storage is accounted independently from node
storage. Keep the recorded lookup, rational, datatype, and malformed workload
cases as compatibility oracles for the flat operand streams.

In an explicit memory diagnostic, check snapshots before replay, after
preparing metadata, at the live roots, after dropping part of the root set,
and after final release. For a graph whose two roots share a child, verify
live nodes and edges at each checkpoint. Requested bytes, container entries
and capacities, and resident pages are different measurements. For a fixed
test fixture, validate the first two against the manually counted graph.
Observe process RSS separately and avoid fixed RSS thresholds in GoogleTest.

### Recorded lookup checks

Seed two BV values and their sum. Issue five requests across two lookup
records, including repeated requests, and check the exact ordered slot
sequence. Every request must return the seeded identity, with three live
nodes and no additional allocation or reclamation after two passes. Use
the normal Node factories and the existing test peer. Test an undefined
or invalid lookup slot, an empty sequence or record, a nonconsecutive seed
slot, a seed record after lookups, and other ownership records in a lookup
file. Reject lookup records in the graph and mixed formats.

Run the benchmark through the normal workload and scenario selection with
an explicit small fixture. The preflight requires the manifest's seed and
lookup counts. A mismatched lookup count fails before any timing. Check
the reported iteration count and positive lookup throughput.

The cvc5 conversion fixture records a BV addition hit, a literal hit and
another addition hit in a later phase. Its seed contains the addition and
its two operand literals in definition order. An unused literal and the
assertion root stay outside the seed. The expected lookup slots are
`2 0 2`. Check an expression metadata hit and an unsupported expression
hit, and exclude a sort hit and rewrite-cache observation from the
expression hit count. A complete fixture with no mapped hits reports the
lookup workload unavailable and emits no lookup file.

### Large snapshot adapter checks

The optional cvc5 snapshot fixture reuses the two indexed-extract assertions
in `fixtures/capture.smt2`. Their reachable graph has six nodes, five edges,
two roots and depth three. The reference expression is shared, and the
extract retains indices 8 and 4 in that order. An enabled Dandelion benchmark
build inspects the generated workload through its usual loader and public
Node API. Compare the captured and replayed graph counts. Run separate cases
for an empty input, a repeated query and an unsupported command. An output
file limit must leave the capture failed without a measurable manifest.
The fixture with a ground definition also checks that the actual cvc5
assertion list can include the definition equation. Count that root and both
occurrences of the repeated user assertion.

## Frontend parser benchmarks

Write the following GoogleTest cases before implementing the
[frontend workload design](../benchmarks/frontend-workloads.md). Register
them with CTest. Exercise the workload module directly in a tests-enabled
build. Enable the benchmark target for CLI integration cases, including a
real pinned Google Benchmark dry run. Invoke the Python CLIs as subprocesses
from GoogleTest with bounded synthetic files in temporary directories. Use
controlled measurement children and host topology data for deterministic
scheduler tests. Keep timing and RSS magnitude out of pass criteria.

| Boundary | Fixture and independent checks |
| --- | --- |
| Build and built-in dry run | With benchmarks disabled, build the workload library in the tests-enabled configuration and verify that the executable and the Google Benchmark dependency are absent. With benchmarks enabled, list and dry-run the executable without `--input`. Require exactly `frontend/smoke/builtin` in the result JSON, one iteration and no error or skip. The built-in text has 45 bytes, 11 tokens, three commands, one assertion, no definitions, an 18-byte largest command, and one Bool leaf. |
| Loaded dry run and CLI | Dry-run a temporary SMT-LIB file at a path containing spaces. Require the six `frontend/parse/` cases `text`, `reader_64k`, `reader_4k`, `reader_irregular`, `phases`, and `teardown` when the filter is absent. Check one iteration and successful result records for each, and require all three phase counters to be present and finite in `phases`. Select a pair with the scenario CSV, then select one through a Google Benchmark filter. Reject missing option values, empty, duplicate, or unknown scenario names, and invalid `--inspect` combinations with nonzero exit and a usage diagnostic. |
| Inspect JSON | Run `--inspect` on each fixture below. Require one escaped JSON object on stdout, `schema_version` 1, `ok`, the exact declared logic, an empty diagnostic message and null code/span. Verify exact syntax counts, committed query counts, and the four graph fields. Compare each of the seven storage fields with `GetStorageStats()` on the corresponding live workload manager. Require live node and edge counts to cover the reachable graph. Exercise a large exact literal to check that `numeric_backing_bytes` accounts for its backing storage. Verify the typed invariants through the workload API alongside JSON counts. |
| Graph order and repeated slots | Inspect `(set-logic QF_SAT)`, `(declare-const a Bool)`, `(assert (not a))`, `(assert (and (not a) (not a)))`, and `(check-sat)`. The first root reaches the shared `not` before the longer path through the second root. Expect two root occurrences and `(nodes, edges, depth, max_fanout) = (3, 3, 3, 2)`. A control with only `set-logic` and `check-sat` has zero roots and all four graph fields zero. |
| Reader split equivalence | Build three byte fixtures with controlled offsets. Put a UTF-8 lead byte at offset 4,095 in a comment or quoted string, put `\r` at offset 65,535 followed by `\n`, and put a quoted `set-info` value longer than 8 KiB across more than one refill with a doubled quote and `;` inside it. Use a separate offset variant whose UTF-8 or CRLF pair straddles the irregular boundary after bytes `1 + 7 + 31`. Independently check offsets before parsing. Run text and all three readers, with one valid assertion and `check-sat` per fixture. Require equal committed metadata, typed assertion and source spans, all syntax/query/graph counts other than calls, and `reader_calls = 0` for text. With EOF consumed, fixed modes give the ceiling of bytes divided by chunk size plus one. Enumerate the irregular schedule to derive its callback count. A companion malformed UTF-8 or unfinished quoted token has the same code and source span for all adapters. |
| Structured failures | Use a missing file (`input_io`), an unclosed assertion (`syntax`, `kSyntax`), a well-formed `(push 1)` (`unsupported`, `kUnsupported`), EOF without a check (`invalid_input`), and a second `check-sat` (`invalid_input`, `kInvalidState`). Enable `:produce-models`, check, and request a model for a `model_dependent` result. Verify nonzero inspect exit, exact failure class, message and applicable span, syntax versus successful Apply counts, and absence of a solver reply. Include a diagnostic with embedded quotes and controls passed to `InspectToJson` to check exact round-trip of its string. A positive `exit` control stops at its closing delimiter even with malformed bytes after it. Dry-run a failing input and require the benchmark error record even when the benchmark framework returns zero. |
| `:named` rollback | Apply `(set-logic QF_LIA)`, `(assert (! true :named good))`, `(assert (and (! true :named first) (! false :named second) 9))`, and `(check-sat)` on separate LF-terminated lines. Expect `invalid_input`, `kInvalidInput`, and `Incompatible operand or result sort` at `9` (offsets `[108, 109)`, line 3, columns `[60, 61)`). Inspect reports three complete commands, two successful applies, one assertion, one annotation and one named term in the committed prefix, and zero checks. Through `ParsedQuery::session`, verify the sole name and annotation are `good`. In a direct test, append a valid command reusing `first` and `second` before the check and continue the session after the failed Apply. Both names can then commit, proving that the original failure left them available. |
| RSS lifecycle | For a deterministic memory sample with 2,048 input bytes, feed baseline 100, peak 150, retained 130, and post-teardown 120 KiB. Require transient 20 KiB, `Peak/Input = 75`, and `Retained/Input = 65`. Check a missing or malformed proc value, zero-byte input, and a retained sample above the kernel's reported high-water mark. Unavailable dependent values serialize as null, and a negative difference clamps to zero. On Linux, run the real sampling path and require positive values for available fields, consistent derived ratios, and a released session and query after teardown. Allow allocator retention and variation between OS sources. |
| Deterministic generation | Run `frontend_generate.py` twice with the same seed and `smoke` into different directories. Require byte-identical manifests, the same ordered IDs and paths, and files that match the recorded byte sizes and independently computed SHA-256. Require every matrix label and shape. Inspect each case for one check, the declared logic and its documented query structure. Check representative typed operations by family. Compare the skewed and balanced pair for equal leaf and operation counts and distinct depths, and the command-partition pair for different command counts and the expected extra conjunction node. Check that trivia padding preserves typed structure and moves the intended reader split. |
| Corpus staging and provenance | Stage two selected regular files from a temporary source in deliberately nonalphabetic order, with comments and blank lines in the selection. Require the original selection bytes, the same ordered relative source paths and content under `inputs/`, the snapshot/source evidence and independently checked selection and input hashes. For indexed selection, include a repeated path, an out-of-range file, and a valid unindexed file. Require deterministic selection within the requested count and size bounds per logic. Verify source bytes, names, timestamps and modes are preserved under the protected-source policy. Exercise absolute paths, traversal, duplicates in a selection file, symlinks in an intermediate directory and at a leaf, a nonregular file, missing selections, and a destination resolving into a protected-root stand-in. Simulate a denied or ineffective metadata-safe open and require an unavailable diagnostic with no unsafe retry. Require no published manifest on any staging failure. |
| Throughput and isolated scheduling | With a controlled topology of two NUMA nodes and SMT siblings, restrict the eligible CPU mask and use a barrier child to demonstrate actual overlap in `throughput`. Require the expected round-robin CPU order, one eligible logical CPU per physical core, the `--jobs` cap, and each child's observed affinity. With the same inputs, `isolated` launches at most one child at a time on a fixed CPU. Require one inspect invocation per case, a fresh timed child for each scenario/sample, and recorded mode, affinity, requested and effective concurrency. Check the single-slot fallback for missing physical topology and the classification of failed affinity setup. |
| Memory ledger and child failures | Request three cores with a budget that fits two per-child allowances. Hold two children at a barrier and require the third to wait. Check ordered reservation/release events, the high-water sum, the child's address-space limit, and prompt slot reuse after success, launch error, child error, timeout, and signal termination. Exercise cancellation with children in flight and verify group cleanup and zero outstanding reservations. Reject a per-child request larger than the total budget before launch. |
| Runner RSS and results | Run a small Linux sample through real GNU `time` and check the runner records positive child-peak KiB separately from inspect `VmHWM`. Use controlled child artifacts for exact parsing of `%M`, missing/malformed RSS, unsupported input, an error/skip in Google Benchmark JSON with exit 0, and malformed JSON. Supply fixed successful wall times and graph/byte/token counts and independently calculate medians and rates. Verify distinct outcome counts and nonzero run status for incomplete measurement, omit parse rates for teardown, and require a manifest hash mismatch to prevent case launch. |

### Exact inspect fixtures

Each fixture uses the literal lines shown, one LF after every line. In the
table, the syntax tuple is `(bytes, tokens, commands, max_command_bytes)`.
Every fixture has one applied check, every command applies successfully, and
text reports zero reader callbacks. Query counts give `(assertions,
definitions, user_functions, datatype_groups, named_terms, annotations,
roots)`. `user_sorts` and `metadata` are zero in all six. The graph tuple is
`(nodes, edges, depth, max_fanout)`.

| Fixture | Syntax | Query | Graph | Typed invariant |
| --- | --- | --- | --- | --- |
| Diamond `let` | `(126, 53, 4, 71)` | `(1, 0, 1, 0, 0, 0, 1)` | `(5, 7, 4, 2)` | Both parents use the same `not a` node and the same declared `a`. The root is equality over those parents. |
| Preserved `define-fun` | `(191, 71, 7, 41)` | `(1, 3, 1, 0, 0, 0, 4)` | `(10, 7, 3, 2)` | The assertion calls `f2` with `a`. The `f1` and `f2` bodies each contain two nested calls to their predecessor around their own formal. `f0`'s body is its distinct formal. Check each call's declaration against the retained definition table. |
| Parametric datatype `match` | `(174, 64, 5, 67)` | `(1, 0, 1, 1, 0, 0, 1)` | `(5, 5, 3, 3)` | The Bool `ite` has a `box` tester on the concrete `Box Bool`, a Bool selector application to the same `b`, and `false` fallback. Both tester and selector resolve to the declared group. |
| `:named` | `(95, 28, 5, 29)` | `(2, 0, 1, 0, 1, 1, 4)` | `(2, 1, 2, 1)` | Both assertions, the `n` entry and its annotation point to the same `not a` node. The annotation retains the source `:named n` attribute. |
| `QF_FP` exact rational | `(122, 51, 3, 91)` | `(1, 0, 0, 0, 0, 0, 1)` | `(3, 3, 3, 2)` | `(-5/4)/(5/2)` and `3/(-6)` both yield normalized `-1/2`. The two `fp.eq` slots share a `kFpFromRational` node of FP format `(8, 24)` whose sole child is `RNE`. |
| Nested `Array` | `(165, 52, 4, 83)` | `(1, 0, 1, 0, 0, 0, 1)` | `(9, 10, 6, 3)` | The constant row has sort `Array Bool Int`. The store preserves `Array Int (Array Bool Int)`. The ordered selections return the row sort and then Int. The two index occurrences of `0` share, as do the row value and comparison operand `1`. |

**Diamond `let`**

```smt2
(set-logic QF_SAT)
(declare-const a Bool)
(assert (let ((x (not a))) (let ((y (and x a)) (z (or x a))) (= y z))))
(check-sat)
```

**Preserved `define-fun`**

```smt2
(set-logic QF_LIA)
(declare-const a Int)
(define-fun f0 ((x Int)) Int x)
(define-fun f1 ((x Int)) Int (f0 (f0 x)))
(define-fun f2 ((x Int)) Int (f1 (f1 x)))
(assert (= (f2 a) a))
(check-sat)
```

**Parametric datatype `match`**

```smt2
(set-logic QF_DT)
(declare-datatypes ((Box 1)) ((par (T) ((empty) (box (value T))))))
(declare-const b (Box Bool))
(assert (match b (((box x) x) (empty false))))
(check-sat)
```

**`:named`**

```smt2
(set-logic QF_SAT)
(declare-const a Bool)
(assert (! (not a) :named n))
(assert n)
(check-sat)
```

**`QF_FP` exact rational**

```smt2
(set-logic QF_FP)
(assert (fp.eq ((_ to_fp 8 24) RNE (/ (- 1.25) (/ 5 2))) ((_ to_fp 8 24) RNE (/ 3 (- 6)))))
(check-sat)
```

**Nested `Array`**

```smt2
(set-logic QF_AUFLIA)
(declare-const a (Array Int (Array Bool Int)))
(assert (= (select (select (store a 0 ((as const (Array Bool Int)) 1)) 0) true) 1))
(check-sat)
```

Run the workload and integration tests with GCC and Clang where available,
alongside the existing `frontend` and `benchmark-infrastructure` CTest
cases. Check Linux `/proc`, GNU `time`, and affinity paths on a Linux host.
Report unavailable platform checks with the cause. Routine verification uses
the bounded synthetic fixtures and leaves the protected corpus untouched.

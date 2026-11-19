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

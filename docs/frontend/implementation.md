# Frontend implementation and acceptance record

**Status.** Phase 2 uses the fused `Session::ReadCommand` protocol
with incremental grammar validation and command-local semantic reductions.
The Phase 1 sections record the previous split reader.

The frontend uses the selected [input profile](logic-profile.md),
[command and ownership contracts](semantic-interfaces.md), and
[test inventory](../testing/frontend.md).

## Phase 1 implementation

`QueryContext` retains the semantic query. Sessions capture written
occurrences only when requested. Source capture and strict metadata
validation are independent. Model-enabled queries keep the frozen term
and sort namespace. Other queries batch-release alias roots at seal.

Nullary definitions and `:named` share one direct-body binding.
Parameterized definitions retain ordered formals and saturated calls.
Lexical dependency marks follow source occurrences in binder scopes.
Binder-free operands use the owning node stack without a parallel mark
for each operand. The name tables borrow canonical declaration spellings
or use stable pooled storage.

Free model declarations are rendered from their semantic ranks. The
printer memoizes concrete sort spellings and parameterized recipe matches.
Frontend inspection benchmarks enable source capture. Timed parsing
counts definition commands with capture disabled.

The [Phase 1 tests](../testing/frontend.md#phase-1-test-design) were added
before the frontend implementation. The first GCC build stopped at the
missing capture and query APIs. The literal tests use the specified Phase 1
width reduction for indexed BV numerals. The Node factories retain their
exact in-range value precondition. Model text expectations follow the
documented sort-head count and name-length rules. The seal fixture grows
the Node table to exercise detachment because Node retains its initial
capacity when releasing a small graph.

## Phase 2 implementation

The scanner reports online byte positions. Typed sessions and independent
syntax validators use the same iterative grammar frames. The typed reader
reduces terms, sorts and datatype fields while consuming tokens. It reuses
the owning term buffer and recycles completed source fragments. Opaque
metadata keeps only open grammar frames and the captured value when
requested.

Named aliases, datatype headers and declaration recipes remain private
until the complete grammar boundary. The first semantic failure stops
reduction and the grammar reader drains the remaining input. A complete
failed command permits recovery. Structural failures terminate the reader.
Source records and phase changes publish after successful preparation.
The driver maps fixed response tags to static strings and retains the
model printer's flush boundary for abstract name publication.

The Clang test-first build reported the missing
`Session::ReadCommand` and `NodeManager::CheckBuiltinOperands` members.
The Phase 2 coordinate fixture uses byte column 21 for the written
significand index, counting the characters before it directly. The
datatype fixture includes a free sort in the previous user-sort range
because datatype groups have a separate query range. The gate fixture
supplies a model provider for a successful `sat` with model production
enabled. The old forced page-widening case now checks live frame and
operand growth while preserving its DAG, spelling and span assertions.

## Phase 2 implementation sequence

The [fused `ReadCommand` design](design.md#phase-2-fused-command-reading)
uses the [Phase 2 frontend tests](../testing/frontend.md#phase-2-test-design)
and [Node consuming tests](../testing/node.md#phase-2-consuming-construction)
as its test-first gates. The Phase 1 results concern the split protocol.

1. Add the scoped GoogleTest cases before implementing the behavior.
   Keep the split path available during the additive migration so its
   existing tests continue to run. Record missing-interface failures and
   preserve the existing coverage during the switch.
2. Add the checked Node move-in path, with shared key/rank access for
   owning and borrowed inputs. Verify it independently against the
   borrowed factories, including retains, collisions and publication
   rollback. The frontend and Node continue to build without solving or
   a SAT dependency.
3. In the scanner and grammar, introduce online positions and a shared
   iterative frame machine. Establish the syntax-only adapter with the
   existing grammar-without-logic fixtures before wiring semantic
   reductions. Keep Phase 1 paging and its promotion hooks confined to
   the migration path while its test coverage is still required.
4. Wire sort, term, datatype and request reducers to the fused command
   stack. Keep operand slots contiguous with separate location metadata.
   Separate inline scope from the sparse overlay, and move publication
   preparation to the completed grammar boundary. Verify failure drain,
   user Reader inspection and rollback before switching the default.
5. Dispatch `Session::Run` through the same reader with private result
   context. Map response tags to static text and preserve the model
   printer's separate staged output lifecycle. Migrate parser-only tools
   and benchmark callers to `ReadCommand`, using `SyntaxValidator` where
   callers require only shape validation.
6. Run the existing acceptance configurations and the Phase 2 cases.
   After the scoped tests pass, retire the pending-syntax API and
   storage, preserving grammar and semantic regression coverage.
   Record observed status and compare Phase 2 performance using the
   [fused workload measurements](../benchmarks/frontend-workloads.md#phase-2-fused-measurement).

The architecture review traced a staged alias through a Reader callback,
late grammar failure and post-close preparation failure. This requires
private early bindings and reverse-append rollback only after the last
read. It also traced a malformed result-dependent request and an
abandoned rational candidate. Both require syntax transitions independent
of semantic success. The ownership review traced repeated child slots
through a hit, miss and failed retain, and separated command commit from
the flush boundary for output bindings. The consuming path transfers each
owner only after all fallible preparation.

## Historical baseline source and ownership (Phase 1)

This section describes the retired split protocol and its original representation;
the current interface is `ReadCommand` and `SyntaxValidator` as described above.

The public C++ module exposes a session whose pending syntax belongs to that
session. `NextSyntax` supplies the public `CommandHeader` with the kind and
span. `Apply` consumes the pending command, including when elaboration
fails. A syntax error ends
the syntactic stream. An independent parser client can inspect the committed
prefix after a failed application. Execution follows immediate-exit.

The scanner borrows contiguous input. The reader adapter owns a reusable
window and copies token spellings into one command arena. The syntax arena
uses preorder expression IDs, separate ordered child intervals, and token
ranges. Grammar, elaboration, model rendering, and failure cleanup use flat
storage and explicit work stacks. Byte spans address the original input.
Quoted data follows the Unicode printable character definition in §3.1
of the pinned SMT-LIB release, including non-ASCII scalars above U+00FF.
[SMT-LIB 2.7](https://smt-lib.org/papers/smt-lib-reference-v2.7-r2026-09-09.pdf)

The sealed query retains the name tables needed by later inspection, in
addition to the original user signature, terms and definitions. Inspection
can instantiate new concrete sorts from those tables without mutating the
sealed signature. The driver attaches the query to the returned provider
before publishing a result that requires model inspection. The provider
supplies canonical keys and typed descriptions of its values. The frontend
validates the boundary and owns textual identities and output ordering.

## Historical test-first staging and observations

These observations record the initial test-first stages and the fixes made
during baseline implementation.

- The initial GCC 16.2.0 Release build of the Node tests failed at the new
  datatype reason, recipe ID, radix, rational, and rounding calls because
  those interfaces were absent. With the interfaces implemented, GCC
  Release discovered and passed 496 Node cases, with zero skips.
- The independent (2,3) rounding fixture includes a virtual upper endpoint
  at exactly 4. That endpoint is infinity for nearest and outward rounding,
  while directed inward rounding selects the largest finite value, 3.5.
  Its neighbor oracle clamps the inward choice at that boundary.
- FE-LEX reports the opening quote of the truncated token at byte 16,
  line 2, column 9. The prefix consists of six comment bytes, CRLF, two
  indentation bytes, and six bytes of command prefix.
- The nested rational-shadowing control uses a bound binary minus with one
  written operand. The documented rank rule then determines an invalid
  application before the child could be rejected as an inactive Int numeral
  under QF_UFFP.
- The initial command test executable reached the linker with
  `SessionState::ApplySemantic` absent. In that build GCC 16.2.0 also
  required out-of-line equality for the two exported source-coordinate
  records. Its module mangler crashed on their imported defaulted friends.

## Baseline elaboration test inventory

The `Registered/LogicProfile` cases enumerate the profile table and check
both declaration spellings, definitions, family admission and availability.
They check the initial equality with the retained definition call and
the Boolean sort of each assertion. `CheckedRanks/OperatorSpelling` checks
operator kind, ordered operands, result sort information and exact indices
for the tabulated spellings. Its shared negatives exercise an empty
application where illegal and a replacement of the first operand with
a wrong sort. Separate tests cover selected width and format mismatches,
qualifier errors, literals and special ranks. Baseline refinements check exact
per-profile root counts and kinds, rank-specific arity and operand
negatives, array index and element checks, asymmetric FP conversion formats,
and Core equality versus `fp.eq` for signed zero.
[Case-level traceability](../testing/frontend.md#case-level-traceability)

The binding, name, sort, and datatype suites cover the FE-BIND,
FE-NAME, FE-DT, FE-MATCH, and FE-SORT cases, including failed publication
and finite scale cases. Test-only counters measure written defined calls,
matched recipe pairs, and visited source occurrences.

The model and driver tests use a synchronous controlled provider. They
check the committed prefix, open-reader dispatch, model availability,
short writes, flush, same-model abstract-value keys and export provenance.
Independent finite tables check array keys and FP rounding. The small
integer oracle interprets the written definition environments and checks
the printed model's sharing and binder identities by reparsing it.
The `FrontendModel` resource cases inject C++ allocation failure while
formatting a wide-exponent FP conversion before the first record and after
an earlier record has been written. The large format itself stays within
the Node representation. Case `StoreOnExposedArrayPreservesExtensionalKey`
uses the two-entry array oracle for write and readback. Deep alias and match
cases check iterative construction and teardown.

The first integrated GCC run found a reserved-name classification error.
Only the generated abstract-name family can be model-dependent, including
under quoting. Other reserved names have the same invalid-input outcome
with or without a model. The canonical ordinal parser now enforces that
rule for both syntax elaboration and same-model lookup.

The model scope regression exposed a difference between a checked Node
rank and an admitted model signature. A provider could construct a sort
with an existing spelling and a different declaration identity. The
export and programmatic evaluation boundaries now check constructor
identity and active ranks. The driver's already elaborated value requests
go directly to provider evaluation.

The [frontend design](design.md#build-and-acceptance-commands) gives
the remaining compiler, discovery, standalone build, and dependency checks.
The [bounded corpus procedure](design.md#verified-local-copies-and-evidence-limits)
gives the source and integrity requirements for opt-in real-script checks.

## Library entry points

Import `dandelion.frontend` and link against the static target
`dandelion_frontend`.

- For typed parsing, open a `Session` with `FromText` or `FromReader`.
  Call `ReadCommand` until EOF, `exit` or a diagnostic. The typed check command owns the
  sealed `QueryContext`, including semantic declaration ranks, ordered
  assertions, datatype groups, and parameterized definition templates.
  Enable `ParseOptions::capture_source_records` to read occurrence history
  through `Session::GetSourceRecords()`. Prefix and source history views
  borrow their owners until mutation.
- For grammar-only reading, open a `SyntaxValidator` with `FromText` or
  `FromReader` and call `ReadNext` for each command header.
- For protocol execution, pass an `Engine` callback and `ResponseSink` to
  `Session::Run`. The callback receives the sealed query by `const QueryContext&`
  and a copy of the session options.
  `Run` attaches an eligible provider to a `ModelOwner` before publishing its
  result. A sink returns the number of bytes it accepted and can provide a
  flush callback. Diagnostics returned by callbacks use message text whose
  storage remains live through the returned result.
- For subsequent inspection of an owned result, use `ForModelText` or
  `ForModelReader`. They share the sealed signature and model-local name
  registry. Their options overloads support canonicalization, fresh source
  capture, and strict metadata. Query and manager access follows Node's
  single-thread contract.

The [profile table](logic-profile.md) defines supported inputs. The
[semantic interfaces](semantic-interfaces.md) describe result and model
obligations in detail.

## Baseline implementation evidence

The lexer separates quoted names from grammar keywords and validates UTF-8
before resolving names. The parser checks all written grammar roles against a
flat command arena. Elaboration visits source operands in order and uses a
rollback log for names and records that become visible within a command.
Temporary scope heads point into indexed records. A defined application
retains its internal declaration and actuals, and the query retains the body
and formals. Source-level dependencies govern closedness of `:named`.

Sort alias recipes share repeated parameters and children by identity. Their
instantiation memo is scoped to one argument environment. Constructor
inference additionally memoizes successfully checked recipe and actual sort
pairs. Model rendering indexes aliases by recipe height and reuses recipe
matches across the concrete sort DAG for each response. A rendered alias
must preserve ordered parameter bindings, including repeated parameters.
The builder owns recursion and productivity checks. The frontend
maps nested recursive fields to unsupported and malformed declarations to
invalid input. Checked exact radix and rational operations preserve the
NodeManager's 32-bit width bounds.

Model responses render typed values through a model-owned registry. The
printer prepares storage for new abstract names before writing the closing
delimiter. After a successful write and any required flush, it publishes
the staged names through node transfers and moves into reserved storage.
The printer validates original function ranks and dependencies, emits nested
`let` bindings for shared subterms, and assigns binder names against the
active user and theory names. The exact rational rounding bridge handles
the model-export case where a later declaration shadows the rational
notation. Its residue comparison and output packing are tested independently
on a small FP domain.

## Reference decisions

- Local source review used Z3 5.3.3 (`430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e`),
  cvc5 1.4.2 (`282d5e0c84baf49c780704c532f87ba66d8b513d`), and Bitwuzla
  0.9.1 source (`8d1eb01093ae54d9b4586456b69c3bf31000a4c2`). Their
  command boundaries and different treatment of definitions informed the
  separation between grammar, semantic publication and result handling.
  Dandelion retains typed definitions with the query so parsing can stay
  proportional to the written graph on the repeated-composition fixtures.
  [Pinned source and earlier probes](topic-research.md#evidence-used-throughout)
- Yices 2.6.4 uses table actions with separate parser and term stacks and a
  token queue for selected source echoes. Dandelion uses an explicit stack
  with a command-owned flat arena and attaches semantic publication to a
  separate call. [Pinned Yices parser](https://github.com/SRI-CSL/yices2/blob/9f786bd49d7558bf451b54a09bb6f6fdf1b23a70/src/frontend/smt2/smt2_parser.c)
- SMTInterpol couples a generated parser to a Script interface, flushes
  protocol responses, and supports common subexpression binding for selected
  output. Dandelion's model printer uses typed Node identities and the
  original query signature to assign capture-safe names and order nested let
  bindings. [Pinned SMTInterpol source](https://github.com/ultimate-pa/smtinterpol/blob/f65e98f05203f1d2801bf5bd121766bfcd6451ec/SMTInterpol/src/de/uni_freiburg/informatik/ultimate/smtinterpol/smtlib2/ParseEnvironment.java)

The implementation uses project-authored code and the existing GMP dependency.
No reference source is linked into the frontend or the default tests.

## Build and test evidence

### Verified Phase 2 run

GCC 16.2.0 and Clang 23.1.5 built with warnings as errors using
CMake 4.5.0 and Ninja 1.13.2. The Debug GCC build enabled ASan and
UBSan.

| Build | Discovered | Passed | Failed | Skipped |
| --- | ---: | ---: | ---: | ---: |
| `build/debug` | 884 | 880 | 0 | 4 |
| `build/clang-debug` | 891 | 887 | 0 | 4 |
| `build/benchmark-clang` | 891 | 887 | 0 | 4 |

All 17 Phase 2 frontend cases and the owning-operand validation case
passed in each configuration. The skips were opt-in benchmark probes
for a reference capture, large snapshot, workload probes and graph
lifetime. The Release benchmark executable passed all three pipeline
checks. The Python differential-runner unit tests passed all 14 cases.
Formatting covered 76 C++ files, and clang-tidy checked 73 translation
units. The GCC Release frontend library also built with tests and
benchmarks disabled.

### Verified Phase 1 run

On 2026-11-19, all 286 frontend tests passed in each full suite.
GCC 16.2.0 built the Debug suite with ASan and UBSan. Clang 23.1.5
built the Debug and benchmark-enabled Release suites.

| Build directory | Discovered | Passed | Failed | Skipped |
| --- | ---: | ---: | ---: | ---: |
| `build/debug` | 858 | 854 | 0 | 4 |
| `build/clang-debug` | 865 | 861 | 0 | 4 |
| `build/benchmark-clang` | 865 | 861 | 0 | 4 |

The four skips in each suite were opt-in reference and workload tests.
The frontend benchmark pipeline passed generator smoke inspection,
synthetic corpus safety, and concurrent runner checks using the Clang
Release benchmark executable. The Release library built with testing
disabled contains no allocation-checkpoint or fault-injection symbols.
These checks establish behavior on the covered fixtures.

### Verified frontend baseline run

On 2026-11-19, the CTest logs recorded the following baseline runs
before the post-fix cleanup.

| Configuration | CTest discovered | Passed | Failed | Skipped |
| --- | ---: | ---: | ---: | ---: |
| GCC Release | 782 | 778 | 0 | 4 |
| Clang Debug | 782 | 778 | 0 | 4 |

Both runs passed the 242 frontend tests. The four full-suite skips were
opt-in benchmark tests. A separate GCC Debug run with ASan and UBSan
passed all 753 tests selected for execution, including the 242 frontend
tests.

### Historical 761-test baseline

Verified on Linux x86-64 with CMake 4.5.0, Ninja 1.13.2, GMP 6.3.0 from the
local package installation, and existing GoogleTest 1.18.0 source under
`subprojects`. All builds used C++23, EXISTING dependency mode,
`DANDELION_WARNINGS_AS_ERRORS=ON`, and separate BMI trees.
Release used `-O3 -DNDEBUG`. RelWithDebInfo used `-O2 -g -DNDEBUG`.
The Debug sanitizer build used
`-fsanitize=address,undefined`, `-fno-sanitize-recover=undefined`, and
`-fno-omit-frame-pointer` for project sources.

| Configuration | CTest discovered | Passed | Failed | Skipped |
| --- | ---: | ---: | ---: | ---: |
| GCC 16.2.0 Release | 761 | 757 | 0 | 4 |
| GCC 16.2.0 RelWithDebInfo | 761 | 757 | 0 | 4 |
| Clang 23.1.5 Debug | 761 | 757 | 0 | 4 |
| GCC 16.2.0 Debug with ASan and UBSan | 761 | 757 | 0 | 4 |

In each tree, the label runs discovered and passed all 199 frontend and 496
Node tests with zero skips. Frontend discovery includes 45 profile cases,
103 operator spelling cases, and the command, numeric, datatype, visibility,
resource, and controlled-provider fixtures. The Node count includes eight
new cases for recipe identity, datatype diagnostics and exact numeric
helpers. The four full-suite skips are the opt-in reference capture,
large-snapshot adapter, real graph probes, and graph-lifetime probes in the
existing benchmark suite. The bounded frontend corpus check below ran
separately. The local Clang ASan link probe reported missing compiler-rt
ASan libraries, so the sanitizer suite used GCC.

`format-check` checked 65 source files with clang-format 23.1.5. The
Clang compilation-database check passed on 59 translation units with
clang-tidy 23.1.5, including the public and private frontend module units.
The optional benchmark sources were outside the naming run because the
build disabled the benchmark target.

The tests-off GCC Release configuration built the frontend archive with
Node and the standard library modules. The final downstream link used GMP
through Node. The Ninja dependency graph contained neither SAT nor
GoogleTest targets. A separate parent project's C++23 consumer compiled
without sanitizer flags, linked ASan and UBSan through the frontend usage
requirements, and executed successfully. The four test trees reported no
Ninja work on immediate rebuild. In an isolated source copy, editing
`terms.cc` rebuilt its object, the archive and the executable link.
Adding an exported declaration to `frontend.cppm` rebuilt the module,
frontend objects and the consumer translation unit.

## Six-script corpus and differential check

The original protected snapshot was unavailable for metadata-safe reading.
The mount is NFSv4.2 with relatime and the original tree was left unopened
and unenumerated. The opt-in runner used the tracked safe Z3 checkout copy
with the 2025 non-incremental release provenance recorded in its manifest.
Every path was checked for symlinks. The bytes, lengths, SHA-256 hashes and
Git blobs matched the pinned manifest and revision. No dataset acquisition
was performed. [Manifest and exact paths](design.md#verified-local-copies-and-evidence-limits)

| Safe script family | Commands | Assertion roots | cvc5 parse | Z3 and cvc5 XOR | Additional reference checks |
| --- | ---: | ---: | --- | --- | --- |
| QF_BV | 29 | 3 | Accepted | Both unsat | Bitwuzla accepted. Yices XOR unsat. |
| QF_NIA | 12 | 1 | Accepted | Both unsat | BV-only reference slice skipped. |
| QF_DT | 13 | 1 | Accepted | Both unsat | BV-only reference slice skipped. |
| QF_UFDT | 247 | 236 | Accepted | Both unsat | BV-only reference slice skipped. |
| QF_AUFBV | 185 | 121 | Accepted | Both unsat | Bitwuzla accepted. Yices XOR unsat. |
| QF_ABV | 13 | 5 | Accepted | Both unsat | Bitwuzla accepted. Yices XOR unsat. |

The frontend parsed all six scripts and checked their typed counts. The
comparison probe preserved the original signatures, recovered each typed
assertion, and emitted the disjunction of all per-root XORs without adding
the original assertions as assumptions. It also recovered the 17 nullary
definitions in the BV script under fresh names. Z3 5.3.3 and cvc5 1.4.2
returned clean `unsat` for all six probes covering 367 roots. Yices 2.6.4
revision `9f786bd49d7558bf451b54a09bb6f6fdf1b23a70` covered the three BV
probes. Bitwuzla executable 0.7.0 parsed the three BV inputs. The unchanged
and perturbed BV controls returned `unsat` and `sat` respectively in Z3,
cvc5 and Yices. The probe uses an independent print inventory for this
bounded slice.

There were zero typed parse failures, unsupported inputs, or inconclusive
results among the six copies. Three Bitwuzla and three Yices invocations
were skipped by the selected reference profiles. Direct snapshot
verification was unavailable. The safe sample leaves gaps for real FP,
IDL/LIA/UFIDL and parameterized datatype match workloads. Synthetic tests
cover the corresponding frontend mechanisms. This evidence concerns input
meaning and frontend protocol behavior. Full-corpus acceptance and
production model correctness require their own validation.

Run the opt-in regression with the Release probe described above and all
applicable reference solvers. The runner records executable versions,
arguments, SHA-256, stdout, stderr and comparisons in its new output directory.
`report.json` contains each check's status and the disposition counts.
Exit 0 requires complete evidence, including successful sample XOR
comparisons and oracle controls. A failed check returns 1. Unavailable,
unsupported, timed-out, crashed or inconclusive checks return 2 when no
check failed. Reference checks outside their applicable profile are skipped.
External runs use a 5 s wall timeout, 3 s soft and 4 s hard CPU bounds,
768 MiB address space and 1 MiB output-file limits.
[Regression runner](../../tests/frontend/benchmark_regression.py)

```sh
cmake --build build/release --target dandelion_frontend_probe
smt_root=$(realpath ..)
evidence=$(mktemp -d /tmp/dandelion-frontend-corpus.XXXXXX)
python3 tests/frontend/benchmark_regression.py \
  --z3-source "$smt_root/z3" --out "$evidence/results" \
  --probe build/release/tests/frontend/dandelion_frontend_probe \
  --z3 "$smt_root/z3/build-5.3.3-static/z3" \
  --cvc5 "$smt_root/cvc5/build-1.4.2-static/bin/cvc5" \
  --bitwuzla "$(command -v bitwuzla)" --yices "$smt_root/solvers_bin/yices2"
```

## Bounded scale check

Five fresh GCC Release parse-only processes per workload ran on an AMD
EPYC 7763 with input piped from memory. A wall interval around
`subprocess.run` includes process startup, input reading, syntax,
elaboration, and process teardown. RSS was collected by `/usr/bin/time`
as per-process peak. Each probe reported one assertion and three commands.

| Generated workload | Counts | Median wall time in ms | Max RSS in KiB |
| --- | --- | --- | --- |
| `distinct` over consecutive Int numerals starting at zero | 128 / 1,024 / 4,096 | 3.38 / 3.23 / 4.40 | 4,448 / 4,576 / 5,216 |
| Nested `not` over `false` | 128 / 1,024 / 4,096 | 2.74 / 3.61 / 6.65 | 4,412 / 4,700 / 6,960 |

The measurements characterize these small process runs, including startup
and both C++ API calls per command. The deterministic definition,
closure, and recipe case counters exercise sharing at their own boundaries.

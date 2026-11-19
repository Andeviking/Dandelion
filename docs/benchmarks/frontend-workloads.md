# Frontend Parser Benchmark Design

**Status.** Proposed targets, workloads, and tooling. The suite measures how
the implemented frontend turns SMT-LIB bytes into a typed query. It uses the
single-query [frontend protocol](../frontend/semantic-interfaces.md) and its
[logic profiles](../frontend/logic-profile.md). Implement against the
[test-first design](../testing/benchmark.md#frontend-parser-benchmarks).

## Targets and workload API

Place both targets under `benchmarks/frontend/`. Build the STATIC C++23
library `dandelion_frontend_workload` when tests or benchmarks are enabled.
It exports `dandelion.frontend_workload` and links `dandelion_frontend`
publicly. Build `dandelion_frontend_benchmarks` only with
`DANDELION_BUILD_BENCHMARKS=ON`. The executable links
`dandelion_frontend_workload` and `benchmark::benchmark` privately. Workload
tests can link the library independently of Google Benchmark. Apply the
[common CMake target configuration](../cmake.md#3-target-based-cmake-standards).

The module exports the following interface in `dandelion::frontend_bench`.
Count bytes, graph sizes, and nanoseconds with unsigned 64-bit fields.

```cpp
enum class ReaderMode { kMemory64K, kMemory4K, kMemoryIrregular };

struct MemorySnapshot {
  std::optional<std::uint64_t> baseline_rss_kib;
  std::optional<std::uint64_t> peak_rss_kib;
  std::optional<std::uint64_t> retained_rss_kib;
  std::optional<std::uint64_t> post_teardown_rss_kib;
  std::optional<std::uint64_t> transient_rss_kib;
};

struct ParseStats {
  std::uint64_t bytes = 0;
  std::uint64_t commands = 0;
  std::uint64_t assertions = 0;
  std::uint64_t definitions = 0;
  std::uint64_t max_command_bytes = 0;
  std::uint64_t reader_calls = 0;
  std::uint64_t next_syntax_ns = 0;
  std::uint64_t apply_ns = 0;
  std::uint64_t teardown_ns = 0;
};

struct ParsedQuery {
  std::string status = "ok";
  std::string diagnostic_message;
  std::optional<frontend::DiagnosticCode> diagnostic_code;
  std::optional<frontend::SourceSpan> diagnostic_span;
  ParseStats stats;
  std::uint64_t applied_commands = 0;
  std::uint64_t syntax_end_offset = 0;
  std::shared_ptr<const frontend::QueryContext> query;
  std::optional<frontend::Session> session;

  void Teardown(bool measure_phase = false);
};

struct InspectReport : ParseStats, MemorySnapshot {
  std::string status = "ok";
  std::string diagnostic_message;
  std::optional<frontend::DiagnosticCode> diagnostic_code;
  std::optional<frontend::SourceSpan> diagnostic_span;
  std::string logic;
  std::uint64_t tokens = 0;
  std::uint64_t applied_commands = 0;
  std::uint64_t checks = 0;
  std::uint64_t user_sorts = 0;
  std::uint64_t user_functions = 0;
  std::uint64_t datatype_groups = 0;
  std::uint64_t named_terms = 0;
  std::uint64_t annotations = 0;
  std::uint64_t metadata = 0;
  std::uint64_t roots = 0;
  std::uint64_t nodes = 0;
  std::uint64_t edges = 0;
  std::uint64_t depth = 0;
  std::uint64_t max_fanout = 0;
  std::optional<std::size_t> live_nodes;
  std::optional<std::size_t> live_edges;
  std::optional<std::size_t> node_bytes;
  std::optional<std::size_t> numeric_backing_bytes;
  std::optional<std::size_t> metadata_bytes;
  std::optional<std::size_t> table_bytes;
  std::optional<std::size_t> peak_table_bytes;
  std::optional<double> peak_input_ratio;
  std::optional<double> retained_input_ratio;
};

std::expected<std::string, std::error_code> LoadFile(
    const std::filesystem::path& path);
ParsedQuery ParseFromText(std::string_view input, bool measure_phases = false);
ParsedQuery ParseFromReader(std::string_view input, ReaderMode mode,
                           bool measure_phases = false);
InspectReport Inspect(std::string_view input,
                      std::optional<ReaderMode> mode = std::nullopt);
std::string InspectToJson(const InspectReport& report);
MemorySnapshot ReadProcessMemory();
```

`LoadFile` reads a local file in binary mode into an owned buffer. It reports
I/O and buffer size or allocation errors to its caller. Keep the buffer alive
until `ParsedQuery::Teardown` or destruction because both adapters borrow its
bytes. A sealed `query` owns its records independently. `ParsedQuery` is
move-only and retains the session for inspection of either the sealed query or
the committed prefix. Calls to `Teardown` release the session and the query
owner. A second call leaves the observation from the first release intact.
Any caller-held query copies must also be dropped for a teardown measurement.

Reader scenarios copy from the same immutable byte buffer used by the text
scenario. The fixed modes cap each copy at 65,536 and 4,096 bytes respectively.
The irregular mode cycles through `1, 7, 31, 257, 4093, 17, 8191` bytes, starting
at 1 in each session. Each copy is further capped by the destination size and
remaining source bytes. A zero return marks EOF. Count all reader callbacks,
including EOF when requested. Text has zero callbacks.

### Parse and inspection boundaries

1. Create a fresh `frontend::Session`. Alternate `NextSyntax` and `Apply`
   and keep the `QueryContext` returned by `check-sat`. Process through EOF
   or `exit`, whichever terminates the session first. This exercises trailing
   syntax and the single-query state checks. Require one successfully applied
   check for a successful workload. Consume command descriptors within the
   loop and discard response text. A valid command that requires a model or
   check result ends the workload as `model_dependent`. Use the phase and
   option checks in `Apply` before classifying a typed `get-model`, `get-value`
   or `:reason-unknown` request. No engine is invoked.
2. `bytes` is the full source buffer size. `commands` counts complete
   `NextSyntax` command headers, including a header whose `Apply` fails.
   `max_command_bytes` is the largest half-open command span, including
   internal trivia and excluding leading and trailing trivia between commands.
   `syntax_end_offset` is the end of the last complete header.
   `applied_commands` counts successful `Apply` returns. `assertions` and
   `definitions` count committed assertion occurrences and retained term
   definitions (`define-fun` and `define-const`). `define-sort` contributes
   to the command counts. Copy the first diagnostic's code, message, and
   source span while its storage is valid. Stop on that failure and use
   `GetPrefix()` for the still-committed records. A missing check at normal
   termination is `invalid_input` with a benchmark diagnostic.
3. `Inspect` uses the text adapter by default and enables phase timing. Sample
   the retained RSS and manager storage at the parse boundary, before copying
   graph roots or allocating traversal state. For query counts, use the
   corresponding `QueryContext` spans, with `checks` set to one when the
   sealed query exists. Take `metadata` from the session so it includes all
   committed `set-info` occurrences, including pre-check failure prefixes.
   Obtain storage via `NodeManager::Attach(query.GetManager())` or the same
   operation on the prefix, then `GetStorageStats()`.
4. Count `tokens` in an untimed, constant-space pass over the bytes through
   `syntax_end_offset`, using the already accepted lexical boundary. Count
   each parenthesis and each atom, keyword, numeral, literal, quoted symbol,
   or string as one token. Skip whitespace and `;` line comments outside
   quoted data. Treat a doubled quote within a string as part of that token.
   Count the offending command's tokens when syntax succeeded and `Apply`
   failed. The pass supplies a measurement denominator without a second
   semantic parse or timing instrumentation in the scanner.
5. Use one iterative `DagWalker` over the concatenation of assertion terms,
   definition bodies, named terms, and annotation occurrence terms, in their
   respective recorded orders. `roots` counts occurrences, including repeated
   roots. Count one `kNodeEnter` per unique node and one `kEdgeEnter` per
   ordered child slot, including repeated edges to a shared child. Root
   events contribute no edges. Compute each node's longest path to a leaf
   on `kNodeExit` from already computed child depths. A leaf has depth one.
   Graph `depth` is the maximum root depth, and `max_fanout` is the maximum
   child-slot count. Both are zero for an empty graph. The walk takes work
   proportional to reachable nodes, edges, and root occurrences. Definition
   applications retain their declaration and actuals. Bodies enter the walk
   as separate roots. Sorts and declaration metadata have their own manager
   accounting. [DAG traversal](../node/implementation.md#7-dag-traversal-and-caches)
6. Release walker, roots, and attached manager before teardown. Copy the
   report data before dropping the session and query. The caller owns the
   resident input buffer throughout `Inspect`.
   If manager or traversal inspection fails, keep the parse diagnostic when
   there is one and otherwise report `measurement_error`. No incomplete
   graph counts qualify for a throughput result.

`InspectToJson` emits one JSON object with `schema_version` set to 1. Use the
C++ field names as top-level keys. Populate the storage fields from
`NodeStorageStats`. Unavailable values use JSON `null`. Status values are
`ok`, `syntax`, `invalid_input`, `unsupported`, `model_dependent`, `input_io`,
`resource`, and `measurement_error`. A frontend `kSyntax` diagnostic maps to
`syntax`, `kInvalidInput` and `kInvalidState` map to `invalid_input`, and
unexpected failures of the measurement path map to `measurement_error`.
Use enumerator names for diagnostic codes. A span has `source`, `begin`, and
`end` fields, with a byte `offset` and one-based `line` and `column` per
endpoint. An absent code or span is `null`, and the successful diagnostic
message is empty.
Emit strings with JSON escaping and finite numbers or `null`. Storage totals
describe the live manager, which may include allocations outside the reachable
term graph.
[Node storage accounting](../node/implementation.md#4-unique-table-and-structural-hash)

## Executable and measurement

The executable accepts `--input <path>`, `--inspect`, and `--scenario <csv>`
alongside Google Benchmark options. Options may appear before or after the
Google Benchmark options. With `--input` and no scenario filter, register
all six `frontend/parse/<scenario>` cases. Reject missing values, empty or
unknown scenario names, duplicates, and invalid mode combinations. Without
`--input`, register only `frontend/smoke/builtin` using the text adapter for
`(set-logic QF_SAT)`, `(assert true)`, and `(check-sat)`, each followed by LF.
`--inspect` requires `--input` and bypasses the benchmark loop. Load the file
once and pass its bytes to `Inspect`. If loading fails, report its diagnostic
in the same JSON format. Omit the inspection scenario filter for text, or
select one of `text`, `reader_64k`, `reader_4k`, and `reader_irregular` to
inspect that adapter. Inspection writes a single
report to stdout and returns zero for `ok` and nonzero for a failed report.
CLI usage errors go to stderr and return nonzero. In timed mode an input or
parse error marks the benchmark as failed with a diagnostic and excludes
that sample from throughput calculation.

| Scenario | Timed operation |
| --- | --- |
| `text` | Fresh text session, parse and type the resident input, then release frontend state. |
| `reader_64k` | Same lifecycle using `kMemory64K`. |
| `reader_4k` | Same lifecycle using `kMemory4K`. |
| `reader_irregular` | Same lifecycle using `kMemoryIrregular`. |
| `phases` | Text lifecycle with separate accumulated wall nanoseconds for `NextSyntax`, `Apply`, and teardown. |
| `teardown` | Release retained text-parse state. Create and validate fresh state with Google Benchmark timing paused before each iteration. |

Load input once per process outside the benchmark loop. Report Google
Benchmark wall and CPU time per iteration. The end-to-end scope includes
session creation and destruction from resident bytes. In `phases`, measure
each call with a monotonic clock, including the terminal `NextSyntax` call,
and report per-iteration means `next_syntax_ns`, `apply_ns`, and `teardown_ns`.
Setup, loop overhead, and clock instrumentation leave a residual in the
end-to-end time. Untimed phase fields in `ParseStats` remain zero. Use the
unprofiled adapter scenarios for rate comparisons and the `phases` scenario
for attribution. The runner joins successful inspection counts with wall
seconds per iteration to compute input MiB/s, lexical tokens/s, and unique
reachable nodes/s for the four adapters. A teardown-only run has no parsing
rate. The runner also records whole-child elapsed and CPU times, whose scope
includes load and process overhead. Use separate Release, tests-off,
sanitizers-off builds for measurements and retain raw samples and settings.

Run a fresh inspect child per input to observe the four RSS boundaries on
Linux. `ReadProcessMemory` extracts `VmRSS` and `VmHWM` in KiB from
`/proc/self/status`, populating `retained_rss_kib` and `peak_rss_kib` for that
call. It leaves missing or malformed fields unavailable. `Inspect` uses
those samples to assemble the lifecycle.

| Field | Sampling boundary |
| --- | --- |
| `baseline_rss_kib` | `VmRSS` with input bytes resident, before session construction. |
| `peak_rss_kib` | `VmHWM` immediately after parsing. This is the process high-water mark through that point. |
| `retained_rss_kib` | `VmRSS` at the same boundary with source bytes, session, and typed query or committed prefix still owned. |
| `post_teardown_rss_kib` | `VmRSS` after releasing the frontend owners and inspection scratch, with input bytes still resident and before JSON serialization. |
| `transient_rss_kib` | `max(0, peak_rss_kib - retained_rss_kib)` when both measurements are available. |

Compute `Peak/Input` as `peak_rss_kib * 1024 / bytes`. Compute
`Retained/Input` from `retained_rss_kib` in the same formula. Missing
values or zero input yield `null`. Allocator retention can persist after
teardown. Record the Node storage breakdown at retention, with
`peak_table_bytes` including overlapping buffers during table growth. Do not
add that peak to the retained components. The runner captures independent OS
peak RSS with GNU `time` `%M` for each child and labels it separately. In
inspection children that whole-process peak can include traversal scratch and
serialization. For comparable parser peaks use the same scenario, lifetime,
and execution mode across runs.

## Generated workloads

Generate a representative workload for each exact label in the matrix.
Each row exercises its written theory features in typed terms. The suite
covers these labels and a separate set of shape variations, with selected
crossings where valid. [Frontend signatures and compatibility extensions](../frontend/logic-profile.md)

| Family | Logic labels | Exercises |
| --- | --- | --- |
| Core | `QF_SAT` | Bool constants, connectives, equality, `ite`, and local binders. |
| BV | `QF_BV`, `QF_UFBV` | Multiple widths, indexed extract and extension, concat, arithmetic, and BV-valued UF in `QF_UFBV`. |
| FP | `QF_FP`, `QF_UFFP`, `QF_BVFP` | Formats, rounding modes, specials and exact rational-to-FP conversion. Include FP-valued UF or standalone BV operations under the corresponding profile. |
| Int | `QF_IDL`, `QF_LIA`, `QF_NIA`, `QF_EIA`, `QF_UFIDL` | Difference constraints, affine terms, nonlinear products, exact large numerals, and UF feeding the restricted binary Int subsignature in `QF_UFIDL`. Exercise `**` in `QF_EIA`. |
| Arrays | `QF_AX`, `QF_ABV`, `QF_AUFLIA` | Select, store, constant and nested arrays over admitted free, BV, and Int sorts respectively. |
| UF | `QF_UF` | Free sort constructors and applications with repeated and distinct actuals. |
| Datatypes | `QF_DT`, `QF_UFDT`, `QF_UFBVDT` | Inductive declarations, parametric constructors, selectors, testers, and matches. Add UF consumers or BV fields under the corresponding profile. |
| Mixed | `QF_AUFBVFP`, `QF_UFFPDTNIRA`, `ALL` | Array/BV/FP/UF terms, datatypes with FP and Int fields consumed by UF, and combinations including nested arrays and BV/Int conversion. |

| Shape | Construction and comparison |
| --- | --- |
| `deep_chain` | Nest unary operations over one leaf to vary depth with bounded fanout. |
| `skewed_vs_balanced` | Pair binary trees with the same ordered leaves and operation count. Compare a spine against balanced nesting. |
| `wide_flat` | Use a single wide application with distinct children. Vary operand count independently of nesting depth. |
| `diamond_dag` | At each level build two different parents of a shared child and combine them. Use `let` to encode sharing with linear written size. |
| `sharing_mechanisms` | Compare repeated spelling, `let`, named aliases, and retained `define-fun` calls. Keep separately recorded input and typed graph sizes since definition calls and bodies add nodes. |
| `shadowing_symtab` | Vary declared symbol count and lexical shadow depth. Include parallel `let` RHS lookups and repeated references to outer bindings. |
| `command_partition` | Compare one command containing many assertions' worth of terms with many short `assert` commands over the same ordered terms. Record the extra conjunction root and changed syntax span sizes. |
| `payload_trivia_boundary` | Vary exact literal size, long quoted metadata, UTF-8, CRLF, and comments around 4 KiB and 64 KiB read boundaries. Include pairs padded with trivia to move a split while preserving the typed query. |

`benchmarks/frontend_generate.py --tier <tier> --seed <n> --output <dir>`
writes deterministic SMT-LIB cases and `generated_manifest.json`. The manifest
has `schema` 1, `tier`, `seed`, and ordered `cases`. Each case records a stable
`case` ID, `logic`, `theory`, `shape`, variant, named scale parameters,
`relative_path`, `bytes`, `sha256`, `max_command_bytes`, `expected_commands`,
`expected_assertions`, and `expected_definitions`. Keep manifest order, input
encoding, and random choices stable for a given generator revision and seed
across worker counts. Exclude timestamps and absolute output paths from
deterministic output.
Use these initial scales for the primary shape knob and record actual sizes.

| Tier | Primary knob and use |
| --- | --- |
| `smoke` | 8 terms, levels, or commands per shape knob. Include a bounded long-token boundary fixture. Suitable for routine checks. |
| `light` | 128 units for interactive measurement. |
| `medium` | 4,096 units for routine Release experiments. |
| `heavy` | 65,536 units for explicitly selected stress runs. |

Scale depth, width, counts, or payload separately according to the shape.
Record secondary knobs and units whenever a family uses a different mapping.
Shared inputs must stay compact as depth increases, and larger cases should
be emitted incrementally so generation can stage them without retaining the
whole suite in memory. Include one `check-sat` per positive fixture.

## Local corpus staging

Use `/pub/netdisk2/zhangx/dandelion-frontend-bench/` for generated cases,
corpus working copies, and run outputs. In
`benchmarks/frontend_corpus.py --source <dir> --output <dir>`,
the source is a metadata-safe local `non-incremental` root or a verified safe
copy with the same relative layout. The target snapshot is `benchmark-2025`
under `/pub/netdisk1/zhangx`. If the source is unavailable or its access-time
guarantee cannot be established, report that condition and use a separately
available safe copy when provided. Source access is limited to an optional
index and the selected paths. [Local dataset policy](../testing/README.md#local-benchmark-data)

For an explicit `--selection <file>`, read relative `.smt2` paths one per
line in execution order. Ignore blank lines and whole-line `#` comments.
Otherwise select from `benchmarks.json` using bounded `--per-logic`,
`--min-bytes`, `--max-bytes`, and `--seed` settings. Deduplicate indexed paths,
apply the size range, and choose reproducibly within each logic. Never search
outside the index for candidates. `corpus_selection.txt` records the
selection used. For an explicit selection, copy the supplied bytes. Supply
`--snapshot <id>` and `--source-evidence <text>`
identifying the local snapshot or the evidence for the safe copy. Stream
selected regular files into `inputs/` preserving relative paths. Reject
absolute paths, `..`, duplicates in explicit selections, symlink components,
and nonregular files. Walk source components with directory-relative,
no-follow opens. For protected sources, require metadata-safe directory and
file reads, including `O_NOATIME` on a filesystem that honors it. An unsafe
read is an unavailable input, including on a mount without the guarantee.
Reject output paths under or resolving into the protected tree. All temporary
files and publication occur under the destination. Publish
`corpus_manifest.json` only after verifying every selection. Use `schema` 1
and ordered `cases` with `case`, `relative_path`, original source path, `bytes`,
and `sha256`. Include snapshot and source evidence, the selection hash, and
the indexed selection settings when used. A missing or invalid selection leaves a
diagnostic and no runnable manifest. Keep unsupported SMT-LIB cases visible
in run results. A path component or `set-logic` label alone does not establish
the theory features or supported profile of a case.

## Batch runner and isolation

`benchmarks/frontend_run.py` takes `--build-dir`, repeatable `--manifest`,
`--output`, `--mode throughput|isolated`, `--jobs`, `--scenario <csv>`,
`--samples`, `--timeout` (seconds per child), `--memory-mib` (per child), and
`--memory-budget-mib` (all concurrent reservations). Require a fresh output
directory outside the protected tree. Use `--jobs` as an upper bound and
default to the number of eligible physical cores. Resolve staged paths within
their manifest directories and verify size and hash before launching the
first child for that case. Use a fresh `--inspect` child once per case and
fresh timed children per scenario and sample. Evaluate child status and both
JSON schemas, including Google Benchmark error and skip fields. Keep an
inspection failure and the associated unmeasured samples in the results.

- Intersect online logical CPUs with `os.sched_getaffinity(0)`. Read Linux
  topology and choose one eligible logical CPU per physical core using the
  SMT sibling groups. Spread consecutive slots across NUMA nodes by taking
  one selected core per node in turn. Each child inherits a one-CPU mask set
  with `os.sched_setaffinity` before input load. Pinning allows normal first
  touch on the selected node. Record the CPU and NUMA node for each sample.
  With missing physical topology, report the fallback and use one eligible
  CPU. If affinity setup fails, record the launch failure.
- `throughput` schedules independent children across the selected slots.
  Report suite elapsed time and batch throughput with the effective
  concurrency and retain the individual sample timings. `isolated` runs one
  child at a time on a fixed eligible core, including inspection, with no
  other suite children in flight. Report modes with each result.
- Use a parent-owned memory ledger together with a per-child address-space
  limit. Reserve the requested allowance before launching and release it
  only after child and launcher cleanup. Queue work until both a core slot
  and a reservation are available. Reject an allowance larger than the
  total budget up front. Default the total budget to one per-child allowance
  per effective CPU slot if unspecified. The ledger limits configured
  concurrent allowances. Record reservation and release events for diagnosis.
- Reuse bounded Linux process execution and GNU `time` measurement where
  suitable. Use separate single-threaded launcher processes for setup that
  uses a pre-exec hook. Apply per-child deadlines and terminate and reap the
  child group on timeout or cancellation. Distinguish terminal failure,
  unsupported input, model-dependent input, timeout, crash, resource limit,
  and unavailable or malformed measurement. Retain stderr and any usable RSS
  on failure, and clear reservations on every terminal path.

Keep the inspect JSON, raw Google Benchmark JSON, stdout, stderr, GNU `time`
record, commands, selected affinity, toolchain and build settings, manifest
hashes, and limits. Write `results.json` with per-sample outcomes and a
`report.md` with counts and medians of successful samples by case, scenario,
and mode. Preserve separate status counts so a partial run is visible. The
runner exits nonzero when any requested measurement fails or is unavailable.

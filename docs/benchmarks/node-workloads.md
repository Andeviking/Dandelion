# Trace-derived Node workloads

The unit of measurement is the Dandelion Node API. The reference solver
provides the workload. Neither program's elapsed time is a substitute for
measuring the other. See the [Node contracts](../node/README.md), the
[dataset policy](../testing/README.md#local-benchmark-data), and the
[acceptance cases](../testing/benchmark.md#trace-derived-workloads).

## Capture

Use cvc5 at `282d5e0c84baf49c780704c532f87ba66d8b513d` in a private source
and build tree, with one nonincremental SMT-LIB query and the default
single-solver driver. Copy only explicitly selected files from a verified
metadata-safe source. Capture a complete bounded reference invocation and
keep stderr, the answer, the input hash, the executable hash, the source
revision, the capture revision, and all arguments and limits. A timeout,
abnormal exit, incomplete footer, or trace cap prevents generation of
workloads. An explicit parser-only invocation yields an input snapshot.
A preprocessing-only invocation additionally observes work through
preprocessing.

cvc5's NodeBuilder has fresh, pool-hit and pool-miss paths. Its constant
factory probes the same pool using a temporary key. Track the outcome before
the request result is returned, with new definitions emitted just once.
Log the assigned type separately, using the normal type attribute write,
and read literal and index payloads from existing storage. The trace holds
IDs and scalar payloads. Recording does not build a Node or ask for a type.
Track the transition to zero references and actual collection separately.
They can be separated by many requests or by a resurrection. Variable name
cache reuse and other higher-level factories may bypass NodeBuilder. They
are distinct from structural interning. AIG, SAT and proof records are
outside these hooks. Any ordinary expression built to support them still
appears by its actual expression kind.

Use phase scopes for parsing, command execution, preprocessing, rewriting
and solving. Rewriting carries its enclosing phase. Record rewrite steps and
cache observations at the generic rewriter's stack and cache sites, not as
observations of every traversal inside every theory. Record the input
assertions and the assertion pipeline's post-preprocessing roots in order,
including duplicate slots. Input roots are the assertion expressions the
reference parser or command interface exposes. Definition expansion may
occur later. Report kind, type and size histograms and counts by phase,
including operations excluded from replay. The type histogram uses the final
recorded type attribute of each ID. Actual reclamation within the manager
destructor is assigned to teardown.

Bitwuzla 0.9.1 interns term and value nodes in its own unique table
(`src/node/node_unique_table.cpp`), handles type and indices differently, and
collects zero-ref graphs through its NodeManager work list. Z3's AST manager
has separate expression, sort and declaration ASTs
(`src/ast/ast.cpp`). This design selects cvc5 as the capture back end because
one instrumented source can exercise the entire theory profile. Reference
IDs, structural pool outcomes, and collection order are observations. The
Dandelion public factories, unique table and reference counts determine
replay outcomes. [cvc5 Node builder](https://github.com/cvc5/cvc5/blob/282d5e0c84baf49c780704c532f87ba66d8b513d/src/expr/node_builder.cpp),
[Bitwuzla Node manager](https://github.com/bitwuzla/bitwuzla/blob/8d1eb01093ae54d9b4586456b69c3bf31000a4c2/src/node/node_manager.cpp),
[Z3 AST manager](https://github.com/Z3Prover/z3/blob/430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e/src/ast/ast.cpp)

## Conversion and ownership

Use a streaming, versioned text trace with a footer containing the event
count. A creation definition carries its ID, kind, operand count, ordered
operands, a separate operator ID if applicable, and exact literal/index
payloads. `hit` refers to a prior, unreclaimed ID. Each definition is
immutable. A datatype group record names the actual sort constructor and
constructor/tester/selector identities. Prefer typed Dandelion factory
operations over parsing a reference solver's printed SMT terms.

Map Bool, BV, FP, rounding mode, Int, arrays, UF and datatype recipes at
the level of declared identities and exact values. Function and datatype
operator nodes become declaration references, never ordinary argument
edges. With FP use the packed bits (Dandelion canonicalizes NaN) and the
format separately. Use unbounded decimal for integers and indices and an
explicit numerator and denominator for the closed rational argument to
`to_fp`. Preserve n-ary order. Accept exact structure only when the
Dandelion operation can represent it. For example, a reference n-ary concat
has no single Dandelion factory request and a shared datatype selector can
lack the identity of any one user declaration. Report these and internally
introduced Real terms as exclusions with reason and count. Check the
constructed sort against the captured or independently inferred result
sort. A missing operand, malformed event, or corrupted definition is a
data error. A graph with an unsupported reachable node is unsupported in
its entirety.

Build snapshots in iterative operand-first order. Store a temporary owning
slot for each result through its last operand use or root acquisition.
Explicitly hold each root occurrence. Repeat construction by issuing those
same requests with the first graph's roots still held. Traverse all roots
through the public DagWalker. For partial release, hold the latter half
of the ordered root occurrences while dropping the first half. Offer this
workload only when two or more roots were recorded. For final release drop
all remaining graph owners and let the manager reclaim the graph.

For mixed replay preserve the order of the mapped reference requests and
ordered operands. Count all excluded operations and dependencies. Use
temporary operand owners through the last mapped use of a result version.
Support a new request for the same reference ID with a new version. Root
and cache events create independent owning slots. Parser-only root
observations supply the snapshot and count as observations in mixed replay.
Cache results retained
in cvc5's rewrite attributes can be released on overwrite or when the key
is collected. This translates an explicit cache owner boundary and does
not map the underlying reference counts or prescribe when Dandelion frees
anything. Report the reconstructed temporary lifetimes and coverage
separately from directly observed ownership. Mixed workloads end by
dropping outstanding benchmark-owned slots.

## Workload file version 1

Each whitespace-delimited line is one record. IDs in each metadata space
are positive. Node slots are zero-based and dense. Names are diagnostic
hints derived from IDs. Numbers in value payloads and operator indices are
decimal strings. A file begins with `dandelion-node 1 graph`,
`dandelion-node 1 mixed`, or `dandelion-node 1 lookup`. Metadata follows in
dependency order.

| Record | Fields |
| --- | --- |
| `sort` | ID followed by `bool`, `int`, `rm`, `bv width`, `fp exponent significand`, `array index element`, or `apply constructor arg...`. |
| `symbol` | Sort constructor ID and arity. |
| `group` | Pairs of new sort constructor ID and parameter count, in datatype slot order. |
| `recipe` | Builder-local ID and `concrete sort`, `param datatype-slot parameter-index`, `array index-recipe element-recipe`, `apply constructor recipe...`, or `recursive datatype-slot recipe...`. |
| `ctor` | Datatype slot, constructor declaration ID, tester declaration ID, and ordered pairs of selector declaration ID and field recipe ID. |
| `commit` | Publish the open datatype group. |
| `decl` | Function declaration ID, `function` or `internal`, result sort, and domain sorts in order. |
| `inst` | Declaration instance ID, declaration ID, and concrete sort arguments in order. |

The body contains `make slot result-sort kind n child-slot... payload...`,
`hold destination source`, `drop slot`, and, for a snapshot, a final
`roots slot...` in occurrence order. Kinds use the public Dandelion Kind
names. Values take one token (0/1, exact integer, unsigned packed bits, or
RoundingMode ordinal). FP `nan` is also an accepted literal payload.
`kApply` takes an instance ID. Builtin plans carry their indices.
`kArrayConst` uses the result sort as qualifier and `kFpFromRational`
has two indices followed by numerator and denominator. An `end` record
terminates the file. Empty work, dead operands, duplicate live destinations,
trailing input and missing terminators fail validation.

### Recorded lookup sequences

A lookup workload measures factory hits on a populated unique table. Supply
one `make` per retained recipe, assigning consecutive slots starting at zero
in operand-first order. Keep the seed nodes owned for the full run. After the
last `make`, write one or more `lookup slot...` records with at least one
slot per record. Every slot denotes its retained construction recipe. Replay
the sequence in order, including repetitions, by invoking the ordinary factory
with the recipe's operands and payload. Discard each returned owner immediately.
End with `end`. The seed's ownership is implicit in this format.

The `node/workload/lookup` benchmark measures the ordered factory requests
and returned handle lifetimes. Setup builds the seed outside the measurement.
`--inspect` verifies the seed sorts and the identity of every lookup result.
The workload manifest records the seed request count and the lookup count
separately. Use `benchmarks/run.py --workload lookup --scenario lookup` with a
case manifest whose top-level `lookup` entry names `lookup.node`.

The `repeat` scenario in a graph workload repeats one
topological construction pass with the original graph retained. A parser hit
trace provides the order and multiplicity lost in a graph snapshot. The
lookup workload holds the seed nodes for the full run. A live parser's table
can grow and reclaim nodes between requests. Compare the same workload and
factory boundary when measuring two implementations.

The ordered cvc5 capture generates a lookup workload from expression
unique-table hits in the captured phases. Preserve the order and multiplicity
of the mapped hits. Seed each mapped hit key and its transitive operands in
definition order. Keep independent counts of expression hits, mapped hits,
metadata hits and excluded hits by phase. Report reasons for unmapped
expression hits. Sort probes and rewrite-cache observations remain in the
reference statistics. A capture with zero mapped expression hits has an
unavailable lookup workload.

## Large input snapshots

Large Bool and BV inputs can use a compact snapshot adapter with the pinned
cvc5 parser and public expression API. It reads one explicitly selected safe
copy. Parse all commands through EOF and invoke the supported declarations,
definitions, assertions and metadata commands. Observe a single `check-sat`
command and leave search unexecuted. Capture the cvc5 assertion-list roots
in order with their duplicate occurrences. Count any additional roots from
ground definitions separately from the assertion commands. The adapter supports closed
Bool/BV terms, including zero-arity declared symbols. Report unsupported
commands, sorts, arities, operators and payloads. Definitions that remain
as unsupported application terms fail the snapshot.

Walk cvc5's reachable expression DAG once. Use an explicit DFS stack, a
reference-ID to dense-index map and flat vectors of definitions and edges.
Emit children before parents and keep their original order. Retain exact
literal values, indexed operands and declaration identity. An edge and
each root occurrence account for one future use of the target. During
emission, decrement that count when the planned operation consumes an
operand. Release the temporary slot on its last use and reuse freed slot
indices. Acquire one owner per root occurrence. The cvc5 AST remains live
through the snapshot because the solver holds the assertions. The adapter
uses space proportional to the graph and streams the workload text to disk.
It avoids retaining a chronological parse log and a second interpreted list
of output operations.

Record the input and generated file hashes, graph size, depth, maximum
fanout, BV widths, peak temporary and root slots, parser call count and
the time spent in parser calls. The parser interval includes its lexer
and term construction. Assertion command execution is recorded separately.
A successful child exit and generated footer are required before publishing
the capture manifest. Apply explicit wall, address-space, input-size and
output-size bounds. The same Release runner consumes the generated snapshot.
Use the ordered trace adapter when measuring request churn, reference cache
observations or solve-stage behavior.

## Measurement and limits

Keep dataset handling and capture outside the C++ runner. Parse the
generated workload once per process. The reader stores contiguous instructions
and separate child, exact parameter, rational, and metadata streams.
Recorded lookup slots use 32 bits while they fit, with a native-width
representation for larger slots. The version 1 file format and request
order are independent of these in-memory layouts. Preflight separately and require
successful factory calls, result sorts, and a live-root check. Build
manager metadata outside the timer. Construct, repeat, lookup, walk,
release-partial, release-final, and mixed are explicit scenarios. Include the
owning slot operations needed for each scenario. Walk consumes one postorder
visit per node and counts every child slot. The release scenarios pass the
selected root prefix or all roots to the manager's batch release operation.
Exclude manager destruction and
fixture setup. The runner starts a fresh Linux process for every scenario
and sample. Use Google Benchmark's per-iteration real and CPU times.
Use GNU time's `%M` for the benchmark child peak RSS, including loaded
workload, metadata, runtime libraries and preparatory work. Record wall
time to completion and CPU usage from the runner's wait for the launcher.
This launcher accounting can include startup overhead. A diagnosis with
the Node test peer is independent of the production timings.

`dandelion_node_benchmarks --workload PATH --inspect-memory` emits one JSON
record. It reports workload storage and current RSS after loading the replay,
preparing manager metadata, replaying the graph or trace, and destroying the
manager. Graphs with multiple roots also report the graph after partial
release and optional table compaction, followed by final release. The
metadata and compaction durations are outside the timed benchmark.

Requested bytes describe the workload and replay arrays, node blocks,
significant GMP backing, and known metadata records. Captured sort and
instance IDs are resolved to dense replay ordinals during loading. Lookup
and construction use those ordinals to read borrowed views. The manager
hash indexes report entry and bucket counts. Allocator padding, GMP reserve,
process pages, and hash container implementation overhead contribute to RSS.
The table peak accounts for simultaneous old and replacement arrays. The
stage RSS is available on Linux via `/proc/self/smaps_rollup`; GNU time's
process peak captures transient usage between stages.

Select by named case, snapshot/trace, and declared scale. Record measured
graph nodes, edges, roots, depth, maximum fanout, request counts and slot
counts. Select larger candidates by these metrics after capture. Declare
timeouts, address-space and log limits up front. A capture, map, or
benchmark failure remains an outcome in the report. Require nonempty
Google Benchmark iteration rows without a skip or error. Report each
sample and medians by case and scenario. Keep fixtures and integer
microbenchmarks visibly marked as diagnostics. No CTest or normal build
loads the corpus.

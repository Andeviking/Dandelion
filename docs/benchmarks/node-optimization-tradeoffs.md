# Node Optimization Tradeoffs

This record covers the four rounds of Dandelion Node optimizations measured
on 2026-11-18 and the follow-up review of read APIs. Use it to identify which
stage or data distribution can explain a regression and to locate the
original measurements. The current Node contracts and layout are documented
in the [Node design](../node/design.md) and [implementation](../node/implementation.md).

## Where to start a regression investigation

| Symptom | First comparison to reproduce |
| --- | --- |
| Cold construction got slower | Separate misses, table growth, node allocation, and metadata preparation. Compare the same unique node and edge counts. |
| Lookup got slower with a live graph | Replay the same hit sequence on the same seed. Check payload shape, actual table occupancy, probe distribution, and the proportions of literals and applications. |
| Traversal or rewriting got slower | Match the consumer's work. Compare event traversal and postorder only for consumers that can use either. Check deep stacks, shared subgraphs, and sparse IDs. |
| Partial release uses more memory | Read peak RSS during the call and inspect table bytes after it. Record surviving nodes and whether the two slot arrays overlapped during a rebuild. |
| RSS remains high after release | Compare live node bytes, index bytes, RSS immediately after release, and RSS after manager destruction. Allocation order can affect residency. |
| Function signatures got slower to read | Read `DeclView::GetDomain()` and `DeclInstanceView::GetDomain()` with setup excluded. Include empty and nonempty domains, ordered and shuffled access, and working sets that cross the last-level cache. |

## Measurement scope

Round comparisons use the baseline saved for that round. Focused runs use
GCC 16.2 Release builds, matching captures and pinned cores. Repeated runs
report the median of the per-process wall-time measurements. Each round's
capture and binary hashes identify the exact pair. Breadth medians describe
the distribution of per-case changes and give equal weight to each case.
Retests are recorded alongside initial results when the two differ.

NodeManager replay has distinct boundaries for cold construction, repeated
construction with roots held, lookup hits, mixed replay, traversal, and
release. The construction and lookup timers exclude fixture and metadata
setup. Peak RSS includes the loaded fixture, setup, allocator, and runtime.
Requested node, metadata, and table bytes come from the manager. RSS
snapshots at a lifecycle boundary measure current residency. The SMT-LIB
parser and solver have their own timing boundaries.
[Benchmark guide](README.md) · [Workload definitions](node-workloads.md)

## Round 1 · Interning, allocation alignment, and traversal state

| Adopted change | Evidence and tradeoff |
| --- | --- |
| Stream the structural hash and finalize it once | In the hash-only comparison, lookup fell from 62.55 to 48.79 ms on `mcm/184` and from 1466.63 to 1153.82 ms on SVCOMP Server. Cold construction fell 6.8% to 13.3% on the three focused graphs. Full key equality resolves collisions. |
| Reuse the vacant slot found during lookup when capacity stays fixed | The seed build's separate insertion searches fell from 1,593,162 to 18 on Ridecore and from 2,841,986 to 18 on ASP. Mean probe length and the p95 probe bucket stayed close to the control. The timed construction stage ranged from a 2.6% gain on ASP to a 2.5% slowdown on Ridecore. A resize requires finding the slot again. |
| Use ordinary allocation alignment for supported layouts | Construction improved 2.3% to 3.7% in the isolated stage. Lookup changes ranged from a 0.6% improvement to a 1.2% slowdown. Static alignment checks guard the allocation contract. |
| Cache scalar sort IDs and share datatype recipe evaluation within an instantiation | A generated shared-recipe fixture with depth 128, 192 fields, and 64 instances took 139.10 to 7.14 ms in the complete inspection process. The memo is scoped to the current instantiation, where the type arguments are fixed. |
| Group traversal marks in sparse words | Visit storage grows with the number of occupied ID groups. The round's combined event traversal improved from 804.94 to 626.52 ms on Ridecore and from 1463.97 to 1008.53 ms on ASP. Event traversal still accounts for every root and edge occurrence. |

The combined build reduced Ridecore lookup from 8.028 to 6.233 s and
Arbiter mixed replay from 334.57 to 244.26 ms. It passed 58 captured workload
preflights. The wall-time results for the combined build include the
interactions among its changes.

| Scratch candidate | Measured tradeoff and disposition |
| --- | --- |
| Bounded small-block cache | Ridecore partial and final release slowed 3.3% and 2.8%. Construction and Arbiter mixed time moved by about 1% or less except `mcm/184` construction at +1.3%. Ordinary allocation was retained. |
| `std::pmr` pool | Ridecore construction slowed 11.8%, final release slowed 47.2%, and Arbiter mixed replay slowed 39.7%. Ordinary allocation was retained. |

## Round 2 · Smaller node storage and a denser replay harness

The accepted implementation reduced the node header to 48 bytes, kept a
wide child-count representation for large fanout, and stored word-sized
bit-vector and floating-point payloads inline. Ordinary declaration instances
reuse their declaration signatures. Parameterized datatype instances keep
substituted signatures. The weak table can release empty storage and shrink
at an explicit compaction boundary. The replay harness separately moved
metadata lookups to dense ordinals and uses 32-bit lookup slots when the
recorded slots fit. That harness work contributes to the process-level
memory numbers.

| Workload | Focused timing | Peak process RSS | Requested storage observation |
| --- | --- | --- | --- |
| Ridecore construction | 563.51 → 489.73 ms | 841.5 → 485.9 MiB | Node blocks 125.78 → 99.36 MiB, manager metadata 52.32 → 27.77 MiB |
| Ridecore lookup | 6.251 → 5.718 s | 1040.3 → 603.1 MiB | The smaller replay representation contributes to this RSS reduction. |
| ASP construction | 904.46 → 796.27 ms | 1756.9 → 1011.5 MiB | Node blocks 219.22 → 175.86 MiB, manager metadata 204.08 → 108.98 MiB |
| Arbiter mixed | 249.41 → 215.04 ms | 566.4 → 270.4 MiB | The replay and manager storage both contribute to process RSS. |

Requested storage compares the older and accepted cores with the compact
replay harness. On the 83-case breadth scan, median time changes were
−8.7% for construction, −9.1% for lookup, and −4.5% for mixed replay.
Five-sample release retests found final release +11.7% on `mcm/184` and
+4.1% on ASP. Ridecore partial release improved 4.7%. Its explicit
compaction reduced the slot array from 32 to 16 MiB while retaining
830,545 nodes.

| Scratch candidate | Measured tradeoff and disposition |
| --- | --- |
| Grouped probes with fingerprints | Ridecore construction improved 533.75 → 388.47 ms and peak RSS fell 495.7 → 467.2 MiB. Lookup slowed 6.322 → 6.629 s and final release slowed 151.99 → 179.88 ms. Linear probing was retained. |
| Reclaimable pages | ASP final release improved 311.42 → 266.64 ms, while `mcm/184` construction peak RSS grew 108.2 → 116.8 MiB. The diagnostic held 8 MiB of committed pages with 256 survivors carrying 16 KiB of node blocks. Ordinary per-node allocation was retained for graphs with independent lifetimes. |
| Packed edges on reclaimable pages | Ridecore construction peak RSS fell 489.0 → 478.9 MiB. Construction slowed 490.92 → 527.23 ms and final release slowed 140.67 → 199.08 ms. Ordered pointer edges were retained. |

## Round 3 · Exact-hit validation, declaration storage, and batch consumers

| Adopted change | Evidence and tradeoff |
| --- | --- |
| Probe the complete borrowed input key before type inference | Ridecore's 54,176,846 recorded hits took 5.626 → 3.908 s. A hit proves identity of the same immutable inputs. A miss still checks operands and computes the result sort. Cold construction retests moved in both directions. A five-sample `mcm/184` retest was 84.48 → 91.24 ms. |
| Store declaration records densely and keep names and domains in stable blocks | Ridecore manager metadata fell 27.77 → 19.63 MiB and ASP fell 108.98 → 78.07 MiB. Arbiter mixed metadata grew 3.96 → 4.56 MiB while process peak RSS stayed near 270 MiB. View stability and failed publication require backing storage to outlive record-vector growth. |
| Offer postorder visitation for once-per-node consumers | Counting the same Ridecore nodes and edges took 619.71 → 104.23 ms. Across eight graphs the median time change was −88.2%. The speedup applies to consumers that can use once-per-node postorder. The event API still provides per-edge events and subtree skipping. |
| Offer batch root release with survivor-aware index cleanup | Ridecore full release took 151.49 → 36.02 ms. Half-root release took 83.32 → 70.07 ms and immediately reduced the table from 32 to 16 MiB. Old and new arrays overlap during rebuilding. Allocation failure falls back to in-place erasure. Individually released handles still use the ordinary ownership path. |
| Return a detached index buffer after freeing retired nodes | In Ridecore partial-then-final release, the earlier batch candidate left 442.2 MiB RSS at the final-release checkpoint. The selected order reduced it to 363.6 MiB, compared with 390.8 MiB in round 2. This is observed allocator residency at a checkpoint. |

The 83-case scan showed median time changes of −3.0% for construction,
−16.5% for lookup, and −3.0% for mixed replay. The separate lifecycle retests
showed median changes of −9.8% for repeated construction, −15.4% for half
release, and −60.0% for full release. These operations use their own timed
fixtures and root ownership arrangements.

The direct weak slot for each nullary application remained a scratch
candidate. Ridecore construction improved 17.5% and used about 28.5 MiB
less peak RSS. The main table shrank from 32 to 16 MiB and its occupancy
rose from about 38% to 65%. Five paired lookup retests found a 10.5% slowdown.
In ASP the table stayed at 32 MiB and the additional slots consumed 8 MiB
of metadata capacity. The accepted build kept one weak index.

## Round 4 · Plain builtin keys, one-pass construction, and sparse domains

| Adopted change | Evidence and tradeoff |
| --- | --- |
| Probe ordinary builtins with a compact key | The initial variant improved lookup by a median 3.44% across seven cases and slowed cold construction by a median 4.50% across two. The miss path was refined to check arity and payload requirements without assembling a general payload key first. Final lookup on Ridecore took 4.020 → 3.822 s, Sage2 improved 5.2%, and SVCOMP improved 5.3%. Qualified array constants retain their explicit sort key. |
| Compute the checked layout once and retain/store children in one pass | All eight isolated cold-construction cases improved, with a median time change of −3.11%. The isolated repeat median was −0.22%. Rollback releases the initialized child prefix, including repeat references to the same child. The final node representation and its read accessors are preserved. |
| Store a compact domain ID in declarations and create span records only for nonempty domains | Ridecore manager metadata fell 19.63 → 15.63 MiB, ASP 78.07 → 62.07 MiB, and Arbiter mixed metadata 4.56 → 3.56 MiB. A fixture with 100,000 nonempty domains took 11.63 → 12.12 ms to construct while metadata stayed at 10.44 MiB. Nonempty domain reads now use the side record described below. |

The final 83-case scan showed median time changes of −1.3% for
construction, −2.4% for lookup, and −1.3% for mixed replay. Ridecore cold
construction initially showed +6.8%. Seven paired retests measured
458.38 → 453.52 ms, or −1.1%. The same retest series measured +1.2%
on ASP, −0.8% on `mcm/184`, and +0.6% on SVCOMP. Round 4 preserved all
87 captured preflight shapes and counts.

In the final focused construction runs, peak process RSS was
458.7 → 456.3 MiB for Ridecore and 912.7 → 896.7 MiB for ASP. These peaks
cover the combined build and the full process footprint. The metadata
figures above come from the manager's storage counters.

### Follow-up · Reading declarations and nodes

A declaration domain is the ordered list of argument sorts in a function
declaration such as `(declare-fun f ((_ BitVec 32) Bool) Int)`.
The parameter-domain mix matters. Of 1,195,261 ordinary declarations
in the 29 input captures, only five had nonempty domains. Those captures
were sensitive to the storage saving and had little coverage of reading
nonempty domains.

The follow-up created declarations outside the timer and measured read-only
loops using third- and fourth-round code. The domain measurements include
testing for emptiness and reading the first argument sort when present.
The ordered sweeps use declaration order. The random sweeps use a fixed
shuffle. Each initial figure is the median of seven processes per version
on the same core.

| Read scenario | Round 3 ns | Round 4 ns | Change |
| --- | ---: | ---: | ---: |
| 500,000 ordinary declarations, all nonempty, random `DeclView::GetDomain()` | 51.31 | 79.39 | +28.08 ns |
| Same population, random `DeclInstanceView::GetDomain()` | 64.00 | 93.85 | +29.86 ns |
| 500,000 declarations, half nonempty, random `DeclView::GetDomain()` | 45.39 | 52.70 | +7.30 ns |
| 500,000 declarations, all empty, random `DeclView::GetDomain()` | 23.14 | 19.26 | −3.88 ns |
| 500,000 declarations, all nonempty, ordered `DeclView::GetDomain()` | 17.72 | 18.18 | +0.46 ns |
| 160,000 terms, random reads of ID, kind, and sort | 25.14 | 25.00 | −0.14 ns |

Five-process confirmation on different cores still measured 43% to 44%
slower domain reads in the all-nonempty random case, and 9% to 11% slower
reads in the half-nonempty random case. The compact record puts an extra
dependent load on the nonempty read path. The empty-domain path skips that
load. Random reads of declaration name, kind, and range improved on these
large fixtures. The code comparison found identical instructions in the
measured node accessors across rounds. Each read scenario verified matching
checksums. The synthetic cases establish the access-pattern sensitivity at
the component level. The domain access implementation is in
`src/node/node_internal.cppm`.

## Phase 3 · Declaration ranks, symbol lookup, table slots, and allocation

Phase 3 used paired GCC 16.2 Release runs with the same corpus inputs and
replay traces for each comparison. The archive is
`/pub/netdisk2/zhangx/dandelion-frontend-bench/phase3_bench`.

| Experiment | Result |
| --- | --- |
| Direct ordinary rank IDs with the existing declaration record size | Removed the 16-byte ordinary instance row. Peak RSS fell 13.6 MiB on Scheduling, 11.0 MiB on STP and 16.0 MiB in ASP graph replay. OISC parse CPU rose 1.9% across nine focused pairs, with every pair slower. Regrouping the manager counters increased the OISC slowdown to 3.9% in seven pairs. |
| Smaller declaration record alongside direct IDs | The smaller record cut a further 6.9 MiB from Scheduling peak RSS. Sage2 parse CPU rose 1.2% across seven focused pairs with the initial implementation. The split implementation added 1.8% parse CPU on MCMPC in an eleven-pair confirmation. |
| Lazy frontend rank publication and a 256-entry per-command owning cache | The lazy path cut STP parse CPU by about 3% against the original baseline. The cache also helped Scheduling and Sage2. They produced no clear peak RSS improvement beyond direct rank encoding, and BMC CPU was about 1.2% above the original baseline with either frontend variant. |
| Weak pointer in the ordinary declaration with all nodes still indexed | Cached BMC parse CPU fell 6.0%. In the nine-pair focused MCM replay, the variant with a node header marker increased repeat and final-release CPU by 14.2% and 11.1%. The variant without a marker increased final-release CPU by 5.5%. |
| Four-byte slots with address windows, a two-bit fingerprint, and full-width fallback | Ridecore's live and peak slot arrays went from 32/48 MiB to 16/24 MiB. Paired lookup CPU fell 3.8%, and OISC peak RSS fell 7.2%. Isolated Ridecore final release increased from 61 to 117 ms. |
| Grow at 87.5% occupancy | Ridecore lookup peak RSS fell 5.1%. Lookup CPU rose 13.2%. Sage2 parse peak RSS rose 8.4%. |
| Process-wide TCMalloc size classes | BMC peak RSS rose 31%, ASP replay peak RSS rose 21%, and Ridecore lookup peak RSS rose 19%. With 512 survivors and 32 KiB of live node blocks, RSS after release rose from 43.9 MiB to 51.9 MiB. The default glibc allocation path remained preferable for this workload set. |

## Evidence archive

All directories below are under `/pub/data/zhangx/results`. The reports
identify the comparison stages and source or binary digests. Captured
workloads are in `dandelion-lookup-breadth-20261118/suite`.

| Evidence | Directory | Files in that directory |
| --- | --- | --- |
| Round 1 stages, timing pairs, probe distributions, and shared-recipe fixture | `dandelion-node-optimization-20261118` | `report.md`, `comparison.json`, `datatype/results.json` |
| Round 2 focus, release retests, residency snapshots, and allocator experiments | `dandelion-node-memory-round2-20261118` | `report.md`, `comparison.json`, `release-paired/results.json`, `final-memory/results.json`, `pages-page-pinning.json` |
| Round 3 focus, breadth, follow-up construction and lifecycle retests | `dandelion-node-round3-20261118` | `report.md`, `expanded/comparison.csv`, `accepted-memory/results.json` |
| Round 4 focus, breadth, cold-construction retests, and nonempty-domain fixture | `dandelion-node-round4-20261118` | `report.md`, `comparison.csv`, `nonempty/results.json` |
| Read API follow-up, raw runs, and instruction comparison | `dandelion-node-round4-20261118/accessors` | `summary.csv`, `confirmation.csv`, `result.json`, `confirmation.json`, `assembly.json`, `evidence.sha256` |
| Review profiles and declaration-domain counts | `dandelion-node-review-20261118` | `capture-composition.tsv`, `declaration-domains.tsv`, `*.annotated` |
| Bitwuzla Ridecore parser instrumentation | `dandelion-bitwuzla-ridecore-20261118` | `methods.md`, `results.json` |
| Paired Dandelion and Bitwuzla Ridecore replay | `dandelion-node-round3-20261118/expanded/bitwuzla-reviewed` | `results.json` |

The read follow-up archives its driver, two binaries, and scripts alongside
the samples. The third-round driver used an archived sibling build. Its
23 relevant function bodies matched the accepted third-round executable
after normalizing instruction addresses, and the fourth-round driver
matched the fourth-round executable in the same comparison.

# Component Benchmarks

Google Benchmark drives Node workload replay for construction, traversal,
and release. Use [GoogleTest](../testing/README.md) for correctness checks.
Past performance decisions and regression diagnostics are recorded in
[Node Optimization Tradeoffs](node-optimization-tradeoffs.md).

## Build and run

The default build does not resolve or build Google Benchmark. To allow the
project to fetch the pinned software dependency if it is not already available,
configure a separate Release tree:

```sh
cmake -S . -B build/benchmark -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DDANDELION_BUILD_BENCHMARKS=ON \
  -DDANDELION_BUILD_TESTS=OFF \
  -DDANDELION_ENABLE_SANITIZERS=OFF \
  -DDANDELION_DEPENDENCY_MODE=FETCH
cmake --build build/benchmark --target dandelion_node_benchmarks
build/benchmark/benchmarks/node/dandelion_node_benchmarks --benchmark_list_tests
build/benchmark/benchmarks/node/dandelion_node_benchmarks --benchmark_dry_run
```

`FETCH` is retained in this build tree's CMake cache. Reconfigure with
`-DDANDELION_DEPENDENCY_MODE=EXISTING` for offline dependency resolution. The
source remains under `subprojects/` by default, and compiled files remain in
the build tree; no user or system installation is performed. Parent-provided
targets, explicit sources, and installed packages are supported as described in
the [CMake dependency policy](../cmake.md#google-benchmark).

The dry run executes each workload once to check that it can run; its timings
are not performance evidence. For actual measurements, run a Release build on
the intended machine, record its toolchain, options and workload, repeat runs,
and interpret variation and system load. Google Benchmark supports filtering,
repetitions, and JSON output through its command-line options; see the
[upstream user guide](https://github.com/google/benchmark/blob/v1.10.0/docs/user_guide.md).
Inspect benchmark errors and skips as well as process status.

## Frontend parser benchmarks

The [frontend workload design](frontend-workloads.md) specifies the planned
parser suite, its six scenarios, inspection metrics, and local corpus policy.
Once implemented, build and dry-run it from the repository root with locally
available dependencies.

```sh
cmake -S . -B build/frontend-bench -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DDANDELION_BUILD_BENCHMARKS=ON \
  -DDANDELION_BUILD_TESTS=OFF -DDANDELION_ENABLE_SANITIZERS=OFF \
  -DDANDELION_DEPENDENCY_MODE=EXISTING
cmake --build build/frontend-bench --target dandelion_frontend_benchmarks
frontend_bench=build/frontend-bench/benchmarks/frontend/dandelion_frontend_benchmarks
"$frontend_bench" --benchmark_dry_run
"$frontend_bench" --input /path/to/staged/case.smt2 --inspect
"$frontend_bench" --input /path/to/staged/case.smt2 --benchmark_dry_run
```

Generate a smoke suite and optionally stage an explicit corpus selection
from a metadata-safe source. Use fresh output directories. Place relative
SMT-LIB paths in `corpus_selection.txt` and supply the actual source evidence.

```sh
frontend_runs=/pub/netdisk2/zhangx/dandelion-frontend-bench
python3 -B benchmarks/frontend_generate.py \
  --tier smoke --seed 2025 --output "$frontend_runs/generated-smoke"
python3 -B benchmarks/frontend_corpus.py \
  --source /path/to/safe/non-incremental \
  --selection "$frontend_runs/corpus_selection.txt" \
  --snapshot benchmark-2025 --source-evidence 'Verified local copy' \
  --output "$frontend_runs/corpus-small"
python3 -B benchmarks/frontend_run.py --build-dir build/frontend-bench \
  --manifest "$frontend_runs/generated-smoke/generated_manifest.json" \
  --output "$frontend_runs/smoke-throughput" --mode throughput --jobs 8 \
  --scenario text,reader_64k,reader_4k,reader_irregular,phases,teardown \
  --samples 3 --timeout 30 --memory-mib 1024 --memory-budget-mib 8192
```

Pass `corpus-small/corpus_manifest.json` under the same staging directory as
another `--manifest` to include the staged corpus. Use `--mode isolated` and
a fresh output directory for one pinned child at a time. The runner keeps
inspection JSON, timings, RSS evidence, and failure records.

## Capture and measure a trace-derived workload

Use the [capture and replay design](node-workloads.md) for the supported
structure, event meanings, and mapping limits. The workflow uses Python 3.8+
on Linux and GNU time for independent benchmark child RSS. Use an existing
metadata-safe input copy and provide its case, family and provenance. All
output directories must be new paths outside the protected dataset. The
collector runs one explicitly selected input.

Apply the reference adapter in `benchmarks/reference/cvc5-282d5e0c.patch`
to a private archive of the pinned local cvc5 source. Configure against an
existing local dependency prefix.

```sh
reference=/tmp/dandelion-cvc5-reference
cvc5_source=/path/to/local/cvc5
cvc5_deps=/path/to/local/cvc5/deps
mkdir -p "$reference/source"
git -C "$cvc5_source" archive 282d5e0c84baf49c780704c532f87ba66d8b513d |
  tar -x -C "$reference/source"
patch --batch -d "$reference/source" -p1 < benchmarks/reference/cvc5-282d5e0c.patch
cmake -S "$reference/source" -B "$reference/build" -G Ninja \
  -DCMAKE_BUILD_TYPE=Production -DENABLE_AUTO_DOWNLOAD=OFF \
  -DUSE_PYTHON_VENV=OFF -DENABLE_IPO=OFF -DBUILD_SHARED_LIBS=ON \
  -DUSE_POLY=ON -DUSE_MPFR=ON -DCMAKE_PREFIX_PATH="$cvc5_deps"
cmake --build "$reference/build" --target cvc5-bin -j 8
```

Pass `-DPython_EXECUTABLE=/path/to/local/python3` if the cvc5 build needs
an existing interpreter with its build-time packages. The patch is applied
only to the private source. Use `solve` for a full reference invocation,
`preprocess` to stop after preprocessing, or `input` for a parser-only
snapshot with observable assertion order. Preprocessing-only runs can
report cvc5's `unknown` answer as part of the selected stopping boundary.

```sh
python3 -B benchmarks/collect.py \
  --solver "$reference/build/bin/cvc5" \
  --input /path/to/safe/case.smt2 --output /tmp/node-captures/case \
  --case case --family family --scale medium --origin corpus \
  --source-relative non-incremental/LOGIC/family/case.smt2 \
  --source-evidence "Verified local copy" --mode solve \
  --timeout 45 --memory-mib 4096 --trace-bytes 134217728 --baseline-samples 2

cmake -S . -B build/node-measure -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DDANDELION_BUILD_BENCHMARKS=ON -DDANDELION_BUILD_TESTS=OFF \
  -DDANDELION_ENABLE_SANITIZERS=OFF -DDANDELION_DEPENDENCY_MODE=EXISTING
cmake --build build/node-measure --target dandelion_node_benchmarks
python3 -B benchmarks/run.py --build-dir build/node-measure \
  --load-dir /tmp/node-captures --output /tmp/node-results \
  --case case --workload input --samples 3 --iterations 5 \
  --timeout 35 --memory-mib 4096
```

Repeat `--case` or `--workload` for a batch. `--scale` checks the recorded
size label. Use `--scenario` to select individual operations and
`--workload mixed` to measure the request sequence. Use `--workload lookup`
to measure the captured expression hits on a populated table. Omit the scenario
filter to run every applicable workload. Partial release needs at least
two recorded root occurrences. All heavy cases require explicit naming.
Run the same commands with `--origin fixture` and measurement
`--allow-fixture` for diagnostic-only experiments.

Each capture keeps the staged SMT-LIB file, the bounded reference trace,
its provenance, phase statistics and mapping counts in `capture.json`,
and the completed `.node` files. Incomplete captures leave a failure
record and their available logs. The measurement directory contains the
Google Benchmark JSON and RSS records for each launched sample, the
inspection result, `results.json`, and `report.md`. Use these files to
compare individual samples as well as medians by case and scenario.
Collection requires the instrumented reference solver. Measurement
consumes the generated files with the uninstrumented Release executable.

## Capture a large Bool/BV input graph

The graph adapter uses the pinned cvc5 parser and shared libraries. It
materializes the assertion-list DAG, including each recorded root
occurrence. It streams a workload without collecting a chronological
trace. Build the adapter in a private directory from the adapter patch.
The cvc5 source and existing build are inputs to this command.

```sh
adapter=/tmp/dandelion-cvc5-snapshot
cvc5_source=/path/to/local/cvc5
cvc5_build=/path/to/local/cvc5-build
mkdir -p "$adapter"
patch --batch -d "$adapter" -p1 < benchmarks/reference/cvc5-282d5e0c-snapshot.patch
c++ -std=c++17 -O3 -DNDEBUG \
  -I "$cvc5_source/include" -I "$cvc5_build/include" \
  "$adapter/dandelion_snapshot.cpp" \
  -L "$cvc5_build/src/parser" -L "$cvc5_build/src" \
  -Wl,-rpath,"$cvc5_build/src/parser:$cvc5_build/src" \
  -lcvc5parser -lcvc5 -o "$adapter/dandelion_snapshot"

python3 -B benchmarks/collect_snapshot.py \
  --snapshotter "$adapter/dandelion_snapshot" \
  --input /path/to/safe/large.smt2 --output /tmp/node-captures/large \
  --case large --family family --scale heavy --origin corpus \
  --source-relative non-incremental/QF_BV/family/large.smt2 \
  --source-evidence "Verified local copy" --input-bytes 3221225472 \
  --output-bytes 6442450944 --timeout 480 --memory-mib 32768
```

The capture records cvc5 parser time and command time separately. The
graph supports the same `--workload input` scenarios and verification as
other generated inputs. Use the trace collector for mixed and
preprocessing workloads. The optional GoogleTest fixture takes
`DANDELION_CVC5_SNAPSHOT_BINARY` as the adapter path.

## Toolchain diagnostics

The initial program exercises the current integer node factory:

- `node/integer/hit` repeatedly constructs integer `42` while retaining an
  existing identical node. The timed operation includes lookup and result
  lifetime management.
- `node/integer/create_release` constructs and releases integer `42` on each
  iteration. The timed operation includes creation and reclamation.

Each workload checks its setup before measurement and reports factory failures.
The integer workloads require no corpus. Enabling project tests includes the
Node test peer. Use the separate tests-off build for performance measurements.
The optional reference capture test takes an explicitly provided
`DANDELION_CVC5_TRACE_BINARY`. The table growth and reclamation diagnostic
takes `DANDELION_NODE_DIAGNOSTIC_WORKLOAD` for a specific larger graph.
Ordinary CTest invocations run the small fixtures.

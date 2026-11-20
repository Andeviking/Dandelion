# Standalone numeric benchmarks

The executable groups checked construction, literal decoding, and ownership
under `util/create/` and arithmetic on prebuilt values under `util/compute/`.
It uses deterministic synthetic inputs with no external datasets. Both suites
link only the standalone numeric library and Google Benchmark.

Each Google Benchmark iteration handles a batch of 40 inputs. The standard
time columns and JSON fields are per batch. The `ns/op` counter gives CPU
nanoseconds per input, and Simplex updates report `ns/row` for all columns of
a row. The `items_per_second` counter uses the same unit of work. Computation
includes destruction of temporary results. Ordinary construction, copy, and
move retain results in slots until untimed cleanup. Move and destroy prepare
fresh sources outside timing. The `create_release` case includes both
construction and destruction. Use the JSON counters for numeric analysis.
Google Benchmark appends a generic time suffix to scaled inverted-rate
counters in its console output.

## Verification

The benchmark preflights exercise every input in a batch before timing.
GoogleTest checks the fixture boundaries independently. Exact-width inputs
have their highest bit set. Tagged BV64 fixtures must carry unsigned values
at or above 2^63, and a sparse BV of width UINT32_MAX must retain the width
with no backing allocation. The Integer mix uses 36 inline and four wide
pairs in each batch. Overflow fixtures promote the result of a signed-word
operation to wide storage. The Rational cancellation chain telescopes to its
last prebuilt fraction. The large-gap FP fixture has two finite normal values
with the requested exponent separation.

The subprocess tests compare the registered inventory with dry-run output
and check that every case finishes one batch with a rate counter and no
error or skip. They exercise a missing match and unrecognized CLI option.
A harness test checks that a deliberately failed preflight reports an error
with its diagnostic.
The [numeric tests](../../docs/testing/util.md) cover the operations'
semantic results and errors independently of the benchmarks.

## Release run

```sh
cmake -S . -B /tmp/dandelion-bench-util -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DDANDELION_BUILD_BENCHMARKS=ON -DDANDELION_BUILD_TESTS=OFF \
  -DDANDELION_ENABLE_SANITIZERS=OFF -DDANDELION_DEPENDENCY_MODE=EXISTING
cmake --build /tmp/dandelion-bench-util --target dandelion_util_benchmarks
bench=/tmp/dandelion-bench-util/benchmarks/util/dandelion_util_benchmarks
"$bench" --benchmark_dry_run
taskset -c 123 "$bench" --benchmark_repetitions=3 --benchmark_min_time=0.05s \
  --benchmark_out=/tmp/dandelion-bench-util/util.json --benchmark_out_format=json
```

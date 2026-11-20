"""Independent fixtures, invoked by the benchmark integration GoogleTests."""

import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "benchmarks"))

from collect import compile_workloads, read_trace
from run import benchmark_sample, run_case
from process import run_process, sha256


def check_raises(function, pattern):
    try:
        function()
    except ValueError as error:
        assert pattern in str(error), str(error)
        return
    raise AssertionError(f"Expected {pattern}")


def conversion(directory):
    events = """new parse s 1 BOOLEAN_TYPE - 0
new parse s 2 INTEGER_TYPE - 0
new parse s 3 BITVECTOR_TYPE - 0 12
new parse s 4 BITVECTOR_TYPE - 0 5
new parse s 5 FLOATINGPOINT_TYPE - 0 8 24
new parse s 6 ARRAY_TYPE - 2 4 5
fresh parse s 7 DATATYPE_TYPE - 0
fresh parse s 8 SORT_TYPE - 0
new parse s 9 PARAMETRIC_DATATYPE - 2 7 6
new parse s 10 FUNCTION_TYPE - 3 9 5 1
fresh parse e 11 VARIABLE - 0
type parse 11 10
fresh parse e 12 VARIABLE - 0
type parse 12 10
datatype parse 7 7 8
fresh parse e 13 SKOLEM - 0
fresh parse e 14 SKOLEM - 0
fresh parse e 15 SKOLEM - 0
constructor parse 7 13 14 15 8
new parse e 40 BITVECTOR_EXTRACT_OP - 0 8 4
new parse e 41 CONST_BITVECTOR - 0 12 291
new parse e 42 BITVECTOR_EXTRACT 40 1 41
type parse 42 4
hit parse 42
new parse e 43 CONST_FLOATINGPOINT - 0 8 24 2147483648
new parse e 44 CONST_FLOATINGPOINT - 0 8 24 nan
new parse e 45 STORE_ALL - 1 43 6
new parse e 46 APPLY_CONSTRUCTOR 13 1 45
type parse 46 9
new parse e 47 APPLY_SELECTOR 15 1 46
type parse 47 6
new parse e 48 APPLY_UF 11 2 46 44
new parse e 49 APPLY_UF 12 2 46 44
new parse e 50 SELECT - 2 47 42
new parse e 51 EQUAL - 2 50 43
new parse e 52 CONST_INTEGER - 0 -123456789012345678901234567890
new parse e 53 LT - 2 52 52
command parse check-sat
root command input 48
root command input 49
root command input 51
root command input 53
cache preprocess.rewrite pre0 48 49
visit preprocess.rewrite 48
new solve e 54 BITVECTOR_ULTBV - 2 42 42
new solve e 55 EQUAL - 2 54 54
collect teardown 48
"""
    trace_file = directory / "trace.txt"
    trace_file.write_text("dandelion-cvc5 1\n" + events + f"end {len(events.splitlines())}\n")
    result = compile_workloads(read_trace(trace_file), directory)
    graph = (directory / "input.node").read_text()
    mixed = (directory / "mixed.node").read_text()
    assert graph.count("kApply 2 ") == 2, graph
    assert graph.count("kBvExtract 1 ") == 1 and " 8 4\n" in graph, graph
    assert "kFpValue 0 2147483648\n" in graph and "kFpValue 0 nan\n" in graph
    assert "kIntValue 0 -123456789012345678901234567890\n" in graph
    assert "group 8 1\n" in graph and "sort 10 apply 8 7\n" in graph
    assert "ctor 0 14 15 16 " in graph and "kArrayConst 1 " in graph
    assert mixed.count("kBvExtract 1 ") == 2, mixed
    assert result["snapshots"]["input"]["roots"] == 4
    assert result["mapping"]["solve"]["excluded"] == {
        "operator BITVECTOR_ULTBV": 1, "unsupported operand": 1}

    bad = directory / "bad.txt"
    bad.write_text("dandelion-cvc5 1\n" + events)
    check_raises(lambda: read_trace(bad), "incomplete")
    bad.write_text("dandelion-cvc5 1\nnew parse e 1 NOT - 1 2\nend 1\n")
    check_raises(lambda: read_trace(bad), "undefined")
    bad.write_text("dandelion-cvc5 1\ncommand parse check-sat\nend 1\n")
    check_raises(lambda: compile_workloads(read_trace(bad), directory), "no input roots")


def reporting(directory):
    report = directory / "benchmark.json"
    row = {"name": "node/workload/construct", "run_type": "iteration",
           "iterations": 2, "time_unit": "ns", "real_time": 5, "cpu_time": 4}
    report.write_text(json.dumps({"benchmarks": [row]}))
    assert benchmark_sample(report, row["name"])["iterations"] == 2
    for change in ({"skipped": True}, {"error_occurred": True},
                   {"real_time": float("nan")}, {"iterations": 0}):
        report.write_text(json.dumps({"benchmarks": [{**row, **change}]}))
        check_raises(lambda: benchmark_sample(report, row["name"]), "benchmark")
    report.write_text('{"benchmarks":[]}')
    check_raises(lambda: benchmark_sample(report, row["name"]), "zero")
    check_raises(lambda: benchmark_sample(directory / "missing", row["name"]), "missing")
    args = argparse.Namespace(scale=None, allow_fixture=False, workload=["input"])
    result = run_case(args, None, directory, directory, "missing")
    assert len(result) == 1 and result[0]["status"] == "missing", result
    (directory / "invalid").mkdir()
    (directory / "invalid/capture.json").write_text("[]")
    result = run_case(args, None, directory, directory, "invalid")
    assert len(result) == 1 and result[0]["status"] == "invalid", result


def lookup(directory):
    binary = Path(sys.argv[2])
    case = directory / "lookup"
    case.mkdir()
    workload = case / "lookup.node"
    workload.write_text("dandelion-node 1 lookup\nsort 1 bool\n"
                        "make 0 1 kBoolValue 0 1\nmake 1 1 kNot 1 0\n"
                        "lookup 1 0 1\nlookup 1 0\nend\n")
    manifest = {
        "schema": 1, "case": "lookup", "scale": "light", "origin": "fixture",
        "family": "fixture", "status": "ok", "snapshots": {},
        "lookup": {"status": "ok", "file": "lookup.node", "requests": 2,
                   "lookups": 5, "sha256": sha256(workload)},
    }
    manifest_path = case / "capture.json"
    manifest_path.write_text(json.dumps(manifest))
    args = argparse.Namespace(scale=None, allow_fixture=True, workload=["lookup"],
                              scenario=["lookup"], cpu=None, iterations=2,
                              samples=1, timeout=10, memory_mib=1024)
    result, = run_case(args, binary, directory, directory / "out", "lookup")
    assert result["status"] == "ok", result
    assert result["work"]["requests"] == 2 and result["work"]["lookups"] == 5
    sample, = result["samples"]
    assert sample["measurement"]["iterations"] == 2
    assert sample["measurement"]["items_per_second"] > 0

    manifest["lookup"]["lookups"] = 4
    manifest_path.write_text(json.dumps(manifest))
    result, = run_case(args, binary, directory, directory / "mismatch", "lookup")
    assert result["status"] == "invalid" and not result["samples"], result
    assert "lookup count" in result["message"], result


def lookup_conversion(directory):
    events = """new parse s 1 BITVECTOR_TYPE - 0 8
new parse e 2 CONST_BITVECTOR - 0 8 3
new parse e 3 CONST_BITVECTOR - 0 8 5
new parse e 4 BITVECTOR_ADD - 2 2 3
new parse e 5 BITVECTOR_ULTBV - 2 2 3
new parse e 6 CONST_BITVECTOR - 0 8 7
new parse e 7 EQUAL - 2 4 3
new parse e 8 BITVECTOR_EXTRACT_OP - 0 3 2
hit parse 4
hit parse 2
hit parse 1
hit parse 8
hit parse 5
command parse check-sat
root command input 7
cache-lookup preprocess.rewrite pre0 4 hit
hit preprocess.rewrite 4
"""

    def convert(output, records):
        output.mkdir()
        path = output / "reference.trace"
        path.write_text("dandelion-cvc5 1\n" + records +
                        f"end {len(records.splitlines())}\n")
        return compile_workloads(read_trace(path), output)["lookup"]

    output = directory / "mapped"
    result = convert(output, events)
    assert result["status"] == "ok" and result["file"] == "lookup.node"
    assert result["requests"] == 3 and result["lookups"] == 3
    assert result["phases"] == {
        "parse": {"expression_hits": 4, "mapped_hits": 2,
                  "metadata_hits": 1, "excluded_hits": 1},
        "preprocess.rewrite": {"expression_hits": 1, "mapped_hits": 1}}
    assert result["exclusions"] == {"operator metadata": 1,
                                    "operator BITVECTOR_ULTBV": 1}
    workload = (output / "lookup.node").read_text().splitlines()
    assert workload[0] == "dandelion-node 1 lookup"
    assert [row.split()[3:] for row in workload if row.startswith("make ")] == [
        ["kBvValue", "0", "3"], ["kBvValue", "0", "5"], ["kBvAdd", "2", "0", "1"]]
    assert workload[-2:] == ["lookup 2 0 2", "end"]

    output = directory / "unavailable"
    no_mapped = events.replace("hit parse 4\n", "").replace("hit parse 2\n", "")
    no_mapped = no_mapped.replace("hit preprocess.rewrite 4\n", "")
    result = convert(output, no_mapped)
    assert result["status"] == "unsupported" and result["lookups"] == 0
    assert not (output / "lookup.node").exists()


def processes(directory):
    python = sys.executable
    for name, program, timeout, outcome in (
        ("ok", "print('hello')", 2, "ok"),
        ("nonzero", "raise SystemExit(3)", 2, "failed"),
        ("sleep", "import time; time.sleep(10)", 0.1, "timeout"),
        ("signal", "import os, signal; os.kill(os.getpid(), signal.SIGTERM)", 2, "crash"),
    ):
        result = run_process([python, "-c", program], directory / name, timeout, 512)
        assert result["status"] == outcome, result
        assert result["rss_kib"] > 0, result
        if name == "signal":
            assert result["returncode"] == -signal.SIGTERM, result
    samples = (
        ("rss", "value = bytearray(32 << 20); value[::4096] = bytes(8192)", 2, "ok"),
        ("rss-sleep", "import time; time.sleep(10)", 0.1, "timeout"),
        ("rss-signal", "import os, signal; os.kill(os.getpid(), signal.SIGTERM)", 2, "crash"),
    )
    for name, program, timeout, outcome in samples:
        result = run_process([python, "-c", program], directory / name, timeout, 512,
                             child_rss=True)
        assert result["status"] == outcome, result
        if name == "rss":
            assert result["rss_kib"] >= 32 * 1024, result
            assert Path(result["rss_output"]).is_file(), result
        if name == "rss-signal":
            assert result["returncode"] == -signal.SIGTERM, result


def reference(directory):
    binary = os.environ["DANDELION_CVC5_TRACE_BINARY"]
    source = PROJECT / "tests/benchmark/fixtures/capture.smt2"
    script = PROJECT / "benchmarks/collect.py"
    for name, mode, cap in (("complete", "solve", "1048576"),
                            ("parser", "input", "1048576"),
                            ("capped", "solve", "256")):
        output = directory / name
        result = subprocess.run(
            [sys.executable, script, "--solver", binary, "--input", source,
             "--output", output, "--case", "capture", "--family", "fixture",
             "--scale", "light", "--origin", "fixture", "--trace-bytes", cap,
             "--mode", mode],
            check=False, capture_output=True, text=True,
        )
        record = json.loads((output / "capture.json").read_text())
        if name == "capped":
            assert result.returncode and record["status"] == "truncated", record
            continue
        assert result.returncode == 0, result.stderr
        trace = (output / "reference.trace").read_text()
        assert " parse " in trace
        if mode == "solve":
            assert " preprocess" in trace and " solve " in trace
            assert record["reference"]["answer"] == "sat", record
        else:
            assert "preprocessed" not in record["snapshots"]
            assert " solve " not in trace
            assert record["mapping"]["command"]["ownership"]["root_observations"] == 2
        assert " BITVECTOR_EXTRACT_OP - 0 8 4\n" in trace
        assert record["snapshots"]["input"]["roots"] == 2
        assert record["phases"]["parse"]["lookup_hits"] > 0

    output = directory / "theories"
    result = subprocess.run(
        [sys.executable, script, "--solver", binary, "--input",
         source.with_name("capture_theories.smt2"), "--output", output,
         "--case", "theories", "--family", "fixture", "--scale", "light",
         "--origin", "fixture", "--mode", "input"],
        check=False, capture_output=True, text=True,
    )
    assert result.returncode == 0, result.stderr
    record = json.loads((output / "capture.json").read_text())
    assert record["snapshots"]["input"]["theories"] == [
        "Array", "BV", "Bool", "DT", "FP", "Int", "UF"]
    workload = (output / "input.node").read_text()
    assert "kIntValue 0 123456789012345678901234567890" in workload
    assert "kFpFromRational" in workload and " 8 24 1 3\n" in workload


def snapshot(directory):
    binary = os.environ["DANDELION_CVC5_SNAPSHOT_BINARY"]
    fixture = PROJECT / "tests/benchmark/fixtures/capture.smt2"
    script = PROJECT / "benchmarks/collect_snapshot.py"

    def collect(name, source, *extra):
        output = directory / name
        command = [
            sys.executable, script, "--snapshotter", binary, "--input", source,
            "--output", output, "--case", name, "--family", "fixture",
            "--origin", "fixture", "--scale", "light", *extra,
        ]
        result = subprocess.run(command, capture_output=True, text=True)
        return result, json.loads((output / "capture.json").read_text())

    result, record = collect("shared", fixture)
    assert result.returncode == 0, result.stderr
    graph = record["snapshots"]["input"]
    assert (graph["nodes"], graph["edges"], graph["roots"], graph["depth"]) == (6, 5, 2, 3)
    workload = directory / "shared/input.node"
    assert workload.read_text().count("kBvExtract 1 ") == 1
    assert " 8 4\n" in workload.read_text()
    if len(sys.argv) > 2:
        inspected = json.loads(subprocess.check_output(
            [sys.argv[2], "--workload", workload, "--inspect"], text=True))
        for metric in ("nodes", "edges", "roots", "depth", "peak_slots", "requests"):
            assert inspected[metric] == graph[metric], (inspected, graph)

    source = directory / "definition.smt2"
    source.write_text("(set-logic QF_BV)\n(declare-const a (_ BitVec 8))\n"
                      "(define-fun b () (_ BitVec 8) (bvadd a #x02))\n"
                      "(assert (= b #x04))\n(assert (= b #x04))\n(check-sat)\n")
    result, record = collect("definition", source)
    assert result.returncode == 0, result.stderr
    graph = record["snapshots"]["input"]
    assert graph["assertion_commands"] == 2 and graph["definition_commands"] == 1
    assert graph["roots"] == 3 and graph["definition_roots"] == 1, graph

    for name, text in (
        ("empty", "(set-logic QF_BV)\n(check-sat)\n"),
        ("incremental", fixture.read_text() + "(check-sat)\n"),
        ("unsupported", "(set-logic QF_BV)\n(push 1)\n(check-sat)\n"),
    ):
        source = directory / (name + ".smt2")
        source.write_text(text)
        result, record = collect(name, source)
        assert result.returncode != 0, record
        assert record["status"] in ("empty", "unsupported"), record
        assert not record.get("snapshots"), record
    result, record = collect("bounded", fixture, "--output-bytes", "32")
    assert result.returncode != 0 and record["status"] == "resource-limit", record


def memory_inspection(directory):
    binary = sys.argv[2]
    workload = directory / "graph.node"
    workload.write_text("""dandelion-node 1 graph
sort 1 bool
make 0 1 kBoolValue 0 1
make 1 1 kAnd 2 0 0
make 2 1 kNot 1 0
drop 0
roots 1 2
end
""")
    result = json.loads(subprocess.check_output(
        [binary, "--workload", workload, "--inspect-memory"], text=True))
    stages = {row["stage"]: row for row in result["stages"]}
    assert list(stages) == ["loaded", "metadata", "replayed", "partial-release",
                            "compacted", "final-release", "manager-destroyed"]
    assert result["workload_bytes"] > 0
    for name, count, edges in [("metadata", 0, 0), ("replayed", 3, 3),
                               ("partial-release", 2, 1), ("compacted", 2, 1),
                               ("final-release", 0, 0)]:
        row = stages[name]
        assert (row["live_nodes"], row["live_edges"]) == (count, edges), row
        assert (row["node_bytes"] > 0) == bool(count), row
        assert row["peak_table_bytes"] >= row["table_bytes"], row
    for name in ("loaded", "manager-destroyed"):
        assert "node_bytes" not in stages[name]
    for row in stages.values():
        assert row["rss_kib"] is None or row["rss_kib"] > 0
    rejected = subprocess.run([binary, "--workload", workload, "--inspect",
                               "--inspect-memory"], capture_output=True,
                              text=True, check=False)
    assert rejected.returncode != 0


def numeric_node(directory):
    binary = sys.argv[2]
    names = [
        "integer/hit", "integer/create_release",
        "integer/wide_hit", "integer/wide_create_release",
        "bv/word_hit", "bv/word_highbit_create_release",
        "bv/wide_hit", "bv/wide_create_release",
        "fp/special_create_release", "fp/word_create_release",
        "fp/wide_create_release",
        "rational_conv/small_create_release", "rational_conv/wide_create_release",
        "dag/postorder", "dag/constant_fold",
    ]
    expected = {"node/" + name for name in names}
    listing = subprocess.check_output([binary, "--benchmark_list_tests"], text=True)
    assert set(listing.splitlines()) == expected, listing
    report = directory / "numeric node.json"
    subprocess.run(
        [binary, "--benchmark_dry_run", f"--benchmark_out={report}",
         "--benchmark_out_format=json"], check=True)
    rows = json.loads(report.read_text())["benchmarks"]
    assert len(rows) == len(expected), rows
    by_name = {row["name"]: row for row in rows}
    assert by_name.keys() == expected, by_name.keys()
    for row in rows:
        assert row["iterations"] == 1 and row["time_unit"] == "ns", row
        assert not row.get("error_occurred") and not row.get("skipped"), row

    for name in names[2:-2]:
        row = by_name["node/" + name]
        baseline = int(name.startswith("rational_conv/"))  # Rounding-mode child.
        assert row["live_nodes"] == baseline + 1, row
        assert row["released_live_nodes"] == baseline, row
        assert row["node_bytes"] > row["released_node_bytes"], row
        assert row["numeric_backing_bytes"] == 0, row  # Packed node tails.
        assert row["released_numeric_backing_bytes"] == 0, row
    for name in names[-2:]:
        row = by_name["node/" + name]
        assert [row[key] for key in ("roots", "live_nodes", "live_edges", "depth")] == [
            15, 96, 157, 65], row
        assert row["node_bytes"] > 0 and row["numeric_backing_bytes"] == 0, row
    folded = by_name["node/dag/constant_fold"]
    assert folded["folded_live_nodes"] > folded["live_nodes"], folded
    assert folded["folded_node_bytes"] > folded["node_bytes"], folded
    assert folded["folded_numeric_backing_bytes"] == 0, folded

    unmatched = subprocess.run(
        [binary, "--benchmark_dry_run", "--benchmark_filter=^node/no-such-benchmark$"],
        capture_output=True, text=True, check=False)
    assert unmatched.returncode != 0, unmatched


if __name__ == "__main__":
    checks = {"conversion": conversion, "reporting": reporting,
              "lookup": lookup, "lookup-conversion": lookup_conversion,
              "processes": processes, "reference": reference,
              "snapshot": snapshot, "memory-inspection": memory_inspection,
              "numeric-node": numeric_node}
    with tempfile.TemporaryDirectory(prefix="dandelion-benchmark-test-") as temp:
        checks[sys.argv[1]](Path(temp))

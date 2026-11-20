"""Bounded frontend benchmark fixtures invoked by GoogleTest."""

import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess
import sys
import tempfile
from unittest.mock import patch

PROJECT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(PROJECT / "benchmarks"))

SEED = 20261119
THEORIES = {"core", "bv", "fp", "int", "array", "uf", "dt", "mixed"}
SHAPES = {
    "deep_chain", "skewed_vs_balanced", "wide_flat", "diamond_dag",
    "sharing_mechanisms", "shadowing_symtab", "command_partition",
    "payload_trivia_boundary",
}
MANIFEST_COUNTS = {
    "bytes": "bytes",
    "max_command_bytes": "max_command_bytes",
    "expected_commands": "commands",
    "expected_assertions": "assertions",
    "expected_definitions": "definitions",
}
STORAGE_FIELDS = (
    "live_nodes", "live_edges", "node_bytes", "numeric_backing_bytes",
    "metadata_bytes", "table_bytes", "peak_table_bytes",
)
RSS_FIELDS = (
    "baseline_rss_kib", "retained_rss_kib", "post_teardown_rss_kib",
    "transient_rss_kib", "peak_rss_kib",
)


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8"))


def expect_value_error(function):
    try:
        function()
    except ValueError:
        return
    raise AssertionError("Expected ValueError")


def fixture_bytes(directory, case):
    relative = Path(case["relative_path"])
    assert not relative.is_absolute() and ".." not in relative.parts, case
    path = directory / relative
    data = path.read_bytes()
    assert len(data) == case["bytes"], case
    assert hashlib.sha256(data).hexdigest() == case["sha256"], case
    return data


def generator_smoke(directory, executable):
    from frontend_generate import generate_suite

    first = directory / "first"
    second = directory / "second"
    manifest = generate_suite(first, tier="smoke", seed=SEED, jobs=2)
    assert manifest["schema"] == 1 and manifest["tier"] == "smoke", manifest
    assert manifest["seed"] == SEED, manifest
    assert manifest == generate_suite(second, tier="smoke", seed=SEED, jobs=1)
    cases = manifest["cases"]
    assert {case["theory"] for case in cases} == THEORIES
    assert {case["shape"] for case in cases} == SHAPES
    for key in ("case", "relative_path"):
        assert all(case[key] for case in cases)
        assert len({case[key] for case in cases}) == len(cases)
    for case in cases:
        assert fixture_bytes(first, case) == fixture_bytes(second, case), case
        for key in MANIFEST_COUNTS:
            assert type(case[key]) is int and case[key] >= 0, case
        assert 0 < case["max_command_bytes"] <= case["bytes"], case
        assert case["expected_commands"] > 0, case
        if executable is not None:
            path = first / case["relative_path"]
            result = subprocess.run(
                [executable, "--input", path, "--inspect"], timeout=15,
                capture_output=True, text=True, check=False,
            )
            assert result.returncode == 0, (case, result.stderr, result.stdout)
            inspected = json.loads(result.stdout)
            assert inspected["status"] == "ok", (case, inspected)
            for manifest_key, inspect_key in MANIFEST_COUNTS.items():
                assert inspected[inspect_key] == case[manifest_key], (
                    case, inspect_key, inspected,
                )
    if executable is None:
        print("Generated fixture inspection requires the benchmark executable.")


def corpus_safety(directory, _executable):
    from frontend_corpus import select_and_stage_corpus

    root = directory / "synthetic corpus"
    inputs = {
        "QF_BV/a.smt2": (
            b"(set-logic QF_BV)\n(assert (= #b0 #b0))\n(check-sat)\n"
        ),
        "QF_BV/b.smt2": (
            b"(set-logic QF_BV)\n(assert (= #b1 #b1))\n(check-sat)\n"
        ),
        "QF_LIA/only.smt2": (
            b"(set-logic QF_LIA)\n(assert (< 1 2))\n(check-sat)\n"
        ),
        "QF_LIA/tiny.smt2": b"(check-sat)\n",
        "QF_LIA/large.smt2": b";" + b"x" * 128 + b"\n(check-sat)\n",
        "QF_LIA/unindexed.smt2": (
            b"(set-logic QF_LIA)\n(assert true)\n(check-sat)\n"
        ),
    }
    for relative, data in inputs.items():
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    index = [
        {"benchmarks": ["QF_BV/a.smt2", "QF_LIA/only.smt2",
                        "QF_LIA/tiny.smt2"]},
        {"benchmarks": ["QF_BV/a.smt2", "QF_BV/b.smt2", "QF_LIA/large.smt2"]},
    ]
    (root / "benchmarks.json").write_text(json.dumps(index), encoding="utf-8")

    def select(output):
        return select_and_stage_corpus(
            dataset_root=root, output_dir=output, per_logic=1, min_bytes=32,
            max_bytes=96, seed=SEED, jobs=2,
        )

    output = directory / "staged"
    returned = select(output)
    selected = (output / "corpus_selection.txt").read_text(
        encoding="utf-8",
    ).splitlines()
    assert len(selected) == 2, selected
    assert "QF_LIA/only.smt2" in selected, selected
    assert len({"QF_BV/a.smt2", "QF_BV/b.smt2"}.intersection(selected)) == 1
    manifest = read_json(output / "corpus_manifest.json")
    assert manifest["schema"] == returned["schema"] == 1, manifest
    cases = manifest["cases"]
    assert cases == returned["cases"], returned
    assert len(cases) == len({case["case"] for case in cases}) == 2, cases
    assert {fixture_bytes(output, case) for case in cases} == {
        inputs[relative] for relative in selected
    }, cases

    protected = Path("/pub/netdisk1/zhangx")
    alias = directory / "dataset alias"
    alias.symlink_to(protected, target_is_directory=True)
    # Guard against a regression writing into the actual protected tree.
    with patch("os.mkdir", side_effect=AssertionError("Unexpected mkdir")):
        for parent in (protected, alias):
            expect_value_error(lambda: select(parent / directory.name))


def check_benchmark_sample(directory):
    from frontend_run import benchmark_sample

    path = directory / "benchmark.json"
    name = "frontend/parse/text"
    row = {
        "name": name, "run_type": "iteration", "iterations": 2,
        "time_unit": "ns", "real_time": 5, "cpu_time": 4,
    }

    def write_rows(rows):
        path.write_text(json.dumps({"benchmarks": rows}), encoding="utf-8")

    write_rows([row])
    assert benchmark_sample(path, name)["iterations"] == 2
    expect_value_error(lambda: benchmark_sample(path, "frontend/parse/phases"))
    for invalid in (
        [], [row, row], [{**row, "skipped": True}],
        [{**row, "error_occurred": True}], [{**row, "iterations": 0}],
        [{**row, "real_time": float("nan")}],
    ):
        write_rows(invalid)
        expect_value_error(lambda: benchmark_sample(path, name))


def instrument_concurrent_commands(directory, executable):
    arrivals = directory / "arrivals"
    arrivals.mkdir()
    wrapper = directory / executable.name
    wrapper.write_text(
        "#!/usr/bin/env python3\n"
        "import os\n"
        "from pathlib import Path\n"
        "import sys\n"
        "import time\n"
        f"arrivals = Path({str(arrivals)!r})\n"
        f"executable = {str(executable)!r}\n"
        "args = sys.argv[1:]\n"
        "if '--input' in args and '--inspect' not in args:\n"
        "    source = Path(args[args.index('--input') + 1])\n"
        "    arrival = arrivals / source.name\n"
        "    arrival.touch()\n"
        "    deadline = time.monotonic() + 10\n"
        "    while len(list(arrivals.iterdir())) < 2:\n"
        "        if time.monotonic() >= deadline:\n"
        "            arrival.unlink()\n"
        "            sys.exit('Two distinct cases did not reach the runner')\n"
        "        time.sleep(0.01)\n"
        "os.execv(executable, [executable, *args])\n",
        encoding="utf-8",
    )
    wrapper.chmod(0o700)
    return wrapper, arrivals


def records(value):
    if isinstance(value, dict):
        yield value
        for item in value.values():
            yield from records(item)
    elif isinstance(value, list):
        for item in value:
            yield from records(item)


def check_run_output(output, case_names):
    result = read_json(output / "results.json")
    report = (output / "report.md").read_text(encoding="utf-8")
    for name in case_names:
        assert name in report, report
        case_records = [record for record in records(result)
                        if record.get("case") == name]
        entries = [
            entry for record in case_records for entry in records(record)
        ]
        times = [entry for entry in entries
                 if {"real_time", "cpu_time", "time_unit"} <= entry.keys()]
        assert times, (name, result)
        for entry in times:
            assert not entry.get("error_occurred") and not entry.get("skipped")
            assert entry["time_unit"] in {"ns", "us", "ms", "s"}, entry
            for key in ("real_time", "cpu_time"):
                assert math.isfinite(entry[key]) and entry[key] >= 0, entry
        for key in (*STORAGE_FIELDS, *RSS_FIELDS):
            values = [entry[key] for entry in entries if key in entry]
            assert values and all(type(value) is int and value >= 0
                                  for value in values), (name, key, result)
            if key in {"node_bytes", "table_bytes", "peak_rss_kib"}:
                assert all(value > 0 for value in values), (name, key, values)


def check_canonicalization(directory, executable):
    import frontend_run

    # Exercise argument parsing without a compiled benchmark executable.
    for enabled in (False, True):
        argv = ["frontend_run.py", "--build-dir", str(directory), "--manifest",
                str(directory / "manifest.json"), "--output", str(directory / "out")]
        if enabled:
            argv.append("--canonicalize")
        result = {"status": "ok", "batch": {"interrupted_signal": None}}
        with patch.object(sys, "argv", argv), patch.object(
                frontend_run, "_run_manifests", return_value=result) as run:
            assert frontend_run.main() == 0
            assert run.call_args.kwargs["canonicalize"] is enabled

    if executable is None:
        print("Canonicalization integration requires the benchmark executable.")
        return

    suite = directory / "canonical-suite"
    suite.mkdir()
    data = (b"(set-logic ALL)\n(declare-const a Int)\n(declare-const b Int)\n"
            b"(declare-const p Bool)\n(declare-const q Bool)\n"
            b"(assert (= (+ a b) (+ b a)))\n(assert (xor p q))\n"
            b"(assert (xor q p))\n(check-sat)\n")
    path = suite / "canonical.smt2"
    path.write_bytes(data)
    manifest = suite / "manifest.json"
    manifest.write_text(json.dumps({
        "schema": 1, "cases": [{"case": "canonical", "relative_path": path.name,
                                "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}],
    }), encoding="utf-8")

    for enabled in (False, True):
        flag = ["--canonicalize"] if enabled else []
        for scenario in frontend_run.ADAPTERS:
            result = subprocess.run(
                [executable, "--input", path, "--inspect", "--scenario", scenario, *flag],
                timeout=15, capture_output=True, text=True, check=False,
            )
            assert result.returncode == 0, (result.stdout, result.stderr)
            metrics = json.loads(result.stdout)
            assert metrics["schema_version"] == 2 and metrics["status"] == "ok", metrics
            assert (metrics["nodes"], metrics["edges"]) == (
                (7, 6) if enabled else (9, 10)), metrics
        output = directory / ("canonical-on" if enabled else "canonical-off")
        result = frontend_run.run_suite(
            manifest_path=manifest, output_dir=output, benchmark_exe=executable,
            mode="isolated", scenarios=frontend_run.ADAPTERS, samples=1,
            canonicalize=enabled,
        )
        assert result["status"] == "ok", result
        assert result["settings"].get("canonicalize", False) is enabled
        case = result["cases"][0]
        for sample in (case["inspect"], *case["samples"]):
            assert ("--canonicalize" in sample["argv"]) is enabled, sample
        report = (output / "report.md").read_text(encoding="utf-8")
        assert ("Canonicalization enabled" in report) is enabled
        check_run_output(output, {"canonical"})


def runner_concurrent(directory, executable):
    from frontend_run import run_suite

    check_benchmark_sample(directory)
    check_canonicalization(directory, executable)
    if executable is None:
        print("Runner concurrency requires the benchmark executable.")
        return

    suite = directory / "suite"
    suite.mkdir()
    cases = []
    for name, assertion in (("negation", "(assert (not true))"),
                            ("disjunction", "(assert (or true false))")):
        relative = name + ".smt2"
        data = f"(set-logic QF_SAT)\n{assertion}\n(check-sat)\n".encode()
        (suite / relative).write_bytes(data)
        cases.append({
            "case": name, "relative_path": relative, "theory": "core",
            "shape": "command_partition", "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest(),
            "max_command_bytes": len(assertion), "expected_commands": 3,
            "expected_assertions": 1, "expected_definitions": 0,
        })
    manifest = suite / "manifest.json"
    manifest.write_text(json.dumps({
        "schema": 1, "tier": "smoke", "seed": SEED, "cases": cases,
    }), encoding="utf-8")

    wrapper, arrivals = instrument_concurrent_commands(directory, executable)
    for mode, binary in (("throughput", wrapper), ("isolated", executable)):
        output = directory / mode
        run_suite(manifest_path=manifest, output_dir=output,
                  benchmark_exe=binary, jobs=2, mode=mode)
        check_run_output(output, {case["case"] for case in cases})
    assert {path.name for path in arrivals.iterdir()} == {
        case["relative_path"] for case in cases
    }


if __name__ == "__main__":
    checks = {
        "generator-smoke": generator_smoke,
        "corpus-safety": corpus_safety,
        "runner-concurrent": runner_concurrent,
    }
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("check", choices=checks)
    parser.add_argument("executable", nargs="?", type=Path)
    args = parser.parse_args()
    executable = args.executable.resolve() if args.executable else None
    with tempfile.TemporaryDirectory(prefix="dandelion-frontend-test-") as temp:
        checks[args.check](Path(temp), executable)

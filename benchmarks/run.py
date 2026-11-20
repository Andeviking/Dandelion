#!/usr/bin/env python3
"""Measure explicit Node workloads with a tests-off Dandelion Release build."""

import argparse
import json
import math
import os
from pathlib import Path
import platform
import re
import statistics
import subprocess
import sys

from process import outside_dataset, run_process, sha256, write_json

PROJECT = Path(__file__).resolve().parent.parent
GRAPH_SCENARIOS = ("construct", "repeat", "walk", "release-partial", "release-final")
TRACE_SCENARIOS = ("mixed", "lookup")
UNITS_MS = {"ns": 1e-6, "us": 1e-3, "ms": 1, "s": 1e3}


def benchmark_sample(path, expected):
    if not path.is_file():
        raise ValueError("missing Google Benchmark output")
    try:
        result = json.loads(path.read_text())
        rows = result["benchmarks"]
        if not isinstance(rows, list):
            raise ValueError("invalid benchmark rows")
        if not rows:
            raise ValueError("zero benchmark rows")
        if len(rows) != 1:
            raise ValueError("expected one Google Benchmark result row")
        row = rows[0]
        if row["name"] not in (expected, expected + "/real_time"):
            raise ValueError(f"unexpected benchmark result {row['name']}")
        if row.get("skipped") or row.get("error_occurred") or row.get("error_message"):
            raise ValueError(f"benchmark skipped or failed ({row.get('error_message', 'no detail')})")
        if (row["run_type"] != "iteration" or type(row["iterations"]) is not int
                or row["iterations"] <= 0 or row["time_unit"] not in UNITS_MS):
            raise ValueError("invalid benchmark iterations or time unit")
        for key in ("real_time", "cpu_time"):
            value = row[key]
            if type(value) not in (int, float) or not math.isfinite(value) or value < 0:
                raise ValueError(f"invalid benchmark {key}")
        return row
    except (AttributeError, KeyError, TypeError, json.JSONDecodeError) as error:
        raise ValueError(f"malformed Google Benchmark result: {error}") from error


def environment(build, binary):
    cache = {}
    for item in (build / "CMakeCache.txt").read_text().splitlines():
        if item and not item.startswith(("//", "#")) and "=" in item:
            left, value = item.split("=", 1)
            cache[left.split(":", 1)[0]] = value
    required = {"CMAKE_BUILD_TYPE": "Release", "DANDELION_BUILD_TESTS": "OFF",
                "DANDELION_ENABLE_SANITIZERS": "OFF", "DANDELION_BUILD_BENCHMARKS": "ON"}
    for key, value in required.items():
        if cache.get(key) != value:
            raise ValueError(f"The measurement build requires {key}={value}")
    if Path(cache["CMAKE_HOME_DIRECTORY"]).resolve() != PROJECT:
        raise ValueError("The measurement build must belong to this Dandelion source tree")
    commands = json.loads((build / "compile_commands.json").read_text())
    relevant = [entry for entry in commands if Path(entry["file"]).parent in
                (PROJECT / "src/node", PROJECT / "benchmarks/node")]
    if not relevant or any(re.search(r"sanitize|DANDELION_NODE_TESTING|fprofile|"
                                     r"finstrument-functions|--coverage|(^| )-pg( |$)",
                                     entry["command"]) for entry in relevant):
        raise ValueError("The compilation database has missing or instrumented Node commands")
    if not binary.is_file():
        raise ValueError(f"missing benchmark executable {binary}")
    compiler = cache["CMAKE_CXX_COMPILER"]
    units = {Path(entry["file"]) for entry in relevant}
    sources = sorted(units | set((PROJECT / "benchmarks").glob("*.py"))
                     | set((PROJECT / "cmake").glob("*.cmake"))
                     | {PROJECT / path for path in (
                         "CMakeLists.txt", "src/node/CMakeLists.txt",
                         "benchmarks/node/CMakeLists.txt")})
    cpuinfo = next((line.split(":", 1)[1].strip() for line in
                    Path("/proc/cpuinfo").read_text().splitlines()
                    if line.startswith("model name")), platform.processor())

    def git(*args):
        return subprocess.check_output(["git", "-C", str(PROJECT), *args],
                                       timeout=10, text=True).strip()

    return {
        "build": str(build), "binary": str(binary), "binary_sha256": sha256(binary),
        "configuration": {key: value for key, value in cache.items() if key in required
                          or key.startswith(("CMAKE_CXX_", "CMAKE_EXE_LINKER_FLAGS",
                                             "DANDELION_DEPENDENCY_MODE"))},
        "compiler_version": subprocess.check_output([compiler, "--version"], text=True,
                                                    timeout=10).splitlines()[0],
        "compile_commands": relevant, "dandelion_commit": git("rev-parse", "HEAD"),
        "worktree": git("status", "--short", "--untracked-files=normal").splitlines(),
        "source_hashes": {str(path.relative_to(PROJECT)): sha256(path) for path in sources},
        "platform": platform.platform(), "cpu": cpuinfo,
        "cpu_affinity_available": sorted(os.sched_getaffinity(0)),
        "load_average": os.getloadavg(),
    }


def run_case(args, binary, load, output, case):
    records = []
    try:
        manifest_path = outside_dataset(load / case / "capture.json")
        manifest = json.loads(manifest_path.read_text())
        if (not isinstance(manifest, dict) or manifest.get("schema") != 1
                or manifest.get("case") != case):
            raise ValueError("case manifest identity or version mismatch")
        if args.scale and args.scale != manifest["scale"]:
            raise ValueError(f"case scale is {manifest['scale']}, expected {args.scale}")
        if manifest["origin"] != "corpus" and not args.allow_fixture:
            raise ValueError("fixture requires --allow-fixture")
        if manifest["status"] != "ok":
            return [{"case": case, "status": manifest["status"],
                     "message": manifest.get("message", "capture failed")}]
        if (not isinstance(manifest.get("family"), str)
                or manifest["origin"] not in ("corpus", "fixture")
                or manifest["scale"] not in ("light", "medium", "heavy")
                or not isinstance(manifest.get("snapshots"), dict)):
            raise ValueError("invalid case manifest metadata")
    except FileNotFoundError as error:
        return [{"case": case, "status": "missing", "message": str(error)}]
    except (ValueError, KeyError, OSError) as error:
        return [{"case": case, "status": "invalid", "message": str(error)}]

    for role in args.workload or ["input"]:
        entry = {"case": case, "family": manifest["family"], "scale": manifest["scale"],
                 "origin": manifest["origin"], "workload": role,
                 "manifest": str(manifest_path), "samples": []}
        records.append(entry)
        try:
            data = manifest.get(role) if role in TRACE_SCENARIOS else manifest["snapshots"].get(role)
            if data is None:
                entry.update(status="missing", message=f"no {role} workload in capture")
                continue
            if data["status"] != "ok":
                entry.update(status=data["status"],
                             message=data.get("reason", "workload unavailable"))
                continue
            if data["file"] != role + ".node" or data["requests"] <= 0:
                raise ValueError("invalid workload metadata")
            workload = outside_dataset(manifest_path.parent / data["file"])
            if not workload.is_file():
                raise FileNotFoundError(str(workload))
            if sha256(workload) != data.get("sha256"):
                raise ValueError("workload SHA-256 mismatch")
        except FileNotFoundError as error:
            entry.update(status="missing", message=str(error))
            continue
        except (TypeError, KeyError, ValueError, OSError) as error:
            entry.update(status="corrupt", message=str(error))
            continue
        result_dir = output / case / role
        result_dir.mkdir(parents=True)
        entry["workload_path"] = str(workload)
        entry["workload_sha256"] = data["sha256"]
        command = [binary, "--workload", workload]
        preflight = run_process([*command, "--inspect"], result_dir / "inspect",
                                args.timeout, args.memory_mib, cpu=args.cpu)
        entry["preflight"] = preflight
        if preflight["status"] != "ok":
            status = preflight["status"]
            message = Path(preflight["stderr"]).read_text(errors="replace").strip()
            if "resource limit" in message or "bad_alloc" in message:
                status = "resource-limit"
            elif status == "failed":
                status = "mapping-failed"
            entry.update(status=status, message=message or "workload inspection failed")
            continue
        try:
            metrics = json.loads(Path(preflight["stdout"]).read_text())
            if metrics["requests"] != data["requests"] or metrics["requests"] == 0:
                raise ValueError("inspected request count differs from capture")
            if role == "lookup":
                if metrics["lookups"] <= 0 or metrics["lookups"] != data["lookups"]:
                    raise ValueError("inspected lookup count differs from capture")
            elif role != "mixed" and (metrics["nodes"] <= 0 or metrics["roots"] != data["roots"]):
                raise ValueError("invalid inspected graph metrics")
        except (ValueError, KeyError, TypeError) as error:
            entry.update(status="invalid", message=str(error))
            continue
        entry["work"] = metrics
        entry["captured_shape"] = data
        available = (role,) if role in TRACE_SCENARIOS else tuple(
            name for name in GRAPH_SCENARIOS if name != "release-partial" or metrics["roots"] > 1)
        for scenario in args.scenario or available:
            if scenario not in available:
                entry["samples"].append({"scenario": scenario, "status": "unavailable"})
                continue
            name = "node/workload/" + scenario
            for repeat in range(1, args.samples + 1):
                prefix = result_dir / f"{scenario}-{repeat}"
                json_path = prefix.with_suffix(".json")
                result = run_process(
                    [*command, f"--benchmark_filter=^{name}(/real_time)?$",
                     f"--benchmark_min_time={args.iterations}x",
                     f"--benchmark_out={json_path}", "--benchmark_out_format=json"],
                    prefix, args.timeout, args.memory_mib, cpu=args.cpu, child_rss=True)
                sample = dict(result, scenario=scenario, repetition=repeat,
                              raw_benchmark_json=str(json_path))
                try:
                    row = benchmark_sample(json_path, name)
                    if result["status"] == "ok" and row["iterations"] != args.iterations:
                        raise ValueError("unexpected benchmark iteration count")
                    sample["measurement"] = row
                except (ValueError, OSError) as error:
                    if sample["status"] == "ok":
                        sample["status"] = "benchmark-failed"
                    sample["message"] = str(error)
                if sample["status"] not in ("ok", "timeout", "crash"):
                    message = sample.get("message", "")
                    if "resource limit" in message or "bad_alloc" in message:
                        sample["status"] = "resource-limit"
                entry["samples"].append(sample)
        entry["status"] = ("ok" if entry["samples"] and
                           all(sample["status"] == "ok" for sample in entry["samples"])
                           else "failed")
    return records


def report(result):
    lines = [
        "# Dandelion Node component benchmark", "",
        f"Benchmark status **{result['status']}**.", "",
        "Wall and CPU columns are Google Benchmark milliseconds per iteration.",
        "RSS is GNU time's benchmark child peak in MiB. It includes the loaded workload,",
        "benchmark runtime, manager metadata, and scenario setup.",
        "Process samples and unrounded values are in results.json.", "",
        "| Case and family | Origin | Scale | Workload | Dandelion nodes / edges / roots | Construction requests | Recorded lookups |",
        "| --- | --- | --- | --- | ---: | ---: | ---: |",
    ]
    for item in result["workloads"]:
        if "work" in item:
            work = item["work"]
            graph = (f"{work['nodes']} / {work['edges']} / {work['roots']}"
                     if item["workload"] not in TRACE_SCENARIOS else "—")
            origin = "diagnostic fixture" if item["origin"] == "fixture" else "corpus"
            lines.append(f"| {item['case']} ({item['family']}) | {origin} | "
                         f"{item['scale']} | {item['workload']} | {graph} | "
                         f"{work['requests']} | {work.get('lookups', '—')} |")
    lines.extend(["", "| Case | Workload | Scenario | Sample | Iterations | Wall ms | CPU ms | RSS MiB |",
                  "| --- | --- | --- | ---: | ---: | ---: | ---: | ---: |"])
    medians = []
    failures = []
    for item in result["workloads"]:
        if item["status"] != "ok" and "message" in item:
            failures.append(f"- {item['case']} {item.get('workload', '')} "
                            f"{item['status']} ({item['message']})")
        by_scenario = {}
        for sample in item.get("samples", []):
            scenario = sample["scenario"]
            if sample["status"] != "ok":
                failures.append(f"- {item['case']} {item['workload']} {scenario} "
                                f"{sample['status']} ({sample.get('message', '')})")
                continue
            row = sample["measurement"]
            unit = UNITS_MS[row["time_unit"]]
            values = (row["real_time"] * unit, row["cpu_time"] * unit,
                      sample["rss_kib"] / 1024)
            by_scenario.setdefault(scenario, []).append(values)
            lines.append(f"| {item['case']} | {item['workload']} | {scenario} | "
                         f"{sample['repetition']} | {row['iterations']} | "
                         f"{values[0]:.5g} | {values[1]:.5g} | {values[2]:.5g} |")
        for scenario, samples in by_scenario.items():
            values = [statistics.median(series) for series in zip(*samples)]
            medians.append(f"| {item['case']} | {item['workload']} | {scenario} | "
                           f"{len(samples)} | {values[0]:.5g} | {values[1]:.5g} | {values[2]:.5g} |")
    lines.extend(["", "## Medians", "",
                  "| Case | Workload | Scenario | Samples | Wall ms | CPU ms | RSS MiB |",
                  "| --- | --- | --- | ---: | ---: | ---: | ---: |", *medians])
    if failures:
        lines.extend(["", "## Failed or unavailable selections", "", *failures])
    lines.extend(["", "The build, source hashes, limits and exact commands are recorded in results.json.", ""])
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--load-dir", type=Path, required=True, help="Parent of case directories")
    parser.add_argument("--output", type=Path, required=True, help="New result directory")
    parser.add_argument("--case", action="append", required=True)
    parser.add_argument("--scale", choices=("light", "medium", "heavy"))
    parser.add_argument("--workload", choices=("input", "preprocessed", *TRACE_SCENARIOS), action="append")
    parser.add_argument("--scenario", choices=(*GRAPH_SCENARIOS, *TRACE_SCENARIOS), action="append")
    parser.add_argument("--allow-fixture", action="store_true")
    parser.add_argument("--samples", type=int, default=3)
    parser.add_argument("--iterations", type=int, default=3)
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--memory-mib", type=int, default=4096)
    parser.add_argument("--cpu", type=int)
    args = parser.parse_args()
    if not (args.iterations > 0 and args.samples > 0 and args.memory_mib > 0
            and math.isfinite(args.timeout) and args.timeout > 0):
        parser.error("Specify positive iterations, samples and limits")
    if any(not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", case) for case in args.case):
        parser.error("Use single case identifiers")
    try:
        build = outside_dataset(args.build_dir)
        load = outside_dataset(args.load_dir)
        output = outside_dataset(args.output)
        binary = build / "benchmarks/node/dandelion_node_benchmarks"
        host = environment(build, binary)
        output.mkdir(parents=True)
    except (ValueError, KeyError, OSError, subprocess.SubprocessError) as error:
        parser.error(str(error))
    if args.workload:
        args.workload = list(dict.fromkeys(args.workload))
    if args.scenario:
        args.scenario = list(dict.fromkeys(args.scenario))
    result = {"schema": 1, "command": [sys.executable, *sys.argv],
              "environment": host, "workloads": []}
    for case in dict.fromkeys(args.case):
        result["workloads"].extend(run_case(args, binary, load, output, case))
        write_json(output / "results.json", dict(result, status="in-progress"))
    result["status"] = ("ok" if result["workloads"] and
                        all(item["status"] == "ok" for item in result["workloads"]) else "failed")
    write_json(output / "results.json", result)
    (output / "report.md").write_text(report(result))
    return 0 if result["status"] == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())

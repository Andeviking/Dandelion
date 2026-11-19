#!/usr/bin/env python3
"""Measure staged frontend workloads in pinned, resource-bounded processes."""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict, deque
from contextlib import contextmanager
import ctypes
from dataclasses import dataclass
import hashlib
import heapq
import json
import math
import os
from pathlib import Path
import platform
import re
import signal
import statistics
import subprocess
import sys
import threading
import time

from process import outside_dataset, run_process, sha256, write_json
from run import UNITS_MS, benchmark_sample as _benchmark_sample

SCRIPT = Path(__file__).resolve()
PROJECT = SCRIPT.parent.parent
BENCHMARK_BINARY = Path("benchmarks/frontend/dandelion_frontend_benchmarks")
CPU_SYSFS = Path("/sys/devices/system/cpu")
ADAPTERS = ("text", "reader_64k", "reader_4k", "reader_irregular")
SCENARIOS = (*ADAPTERS, "phases", "teardown")
PHASES = ("next_syntax_ns", "apply_ns", "teardown_ns")
RSS_FIELDS = (
    "baseline_rss_kib", "peak_rss_kib", "retained_rss_kib",
    "post_teardown_rss_kib", "transient_rss_kib",
)
STORAGE_FIELDS = (
    "live_nodes", "live_edges", "node_bytes", "numeric_backing_bytes",
    "metadata_bytes", "table_bytes", "peak_table_bytes",
)
RATIOS = ("peak_input_ratio", "retained_input_ratio")
COUNTS = {
    "bytes": "bytes",
    "max_command_bytes": "max_command_bytes",
    "expected_commands": "commands",
    "expected_assertions": "assertions",
    "expected_definitions": "definitions",
}
INSPECT_COUNTS = (
    *COUNTS.values(), *PHASES, "tokens", "applied_commands", "checks", "reader_calls",
    "user_sorts", "user_functions", "datatype_groups", "named_terms", "annotations",
    "metadata", "roots", "nodes", "edges", "depth", "max_fanout",
)
INSPECT_STATUSES = {
    "ok", "syntax", "invalid_input", "unsupported", "model_dependent", "input_io",
    "resource", "measurement_error",
}
RATE_FIELDS = ("mib_per_s", "tokens_per_s", "nodes_per_s")
RESOURCE_ERROR = re.compile(r"resource limit|bad_alloc|cannot allocate memory", re.IGNORECASE)
MIB = 1024 ** 2


def benchmark_sample(path, expected):
    """Read the same strict single-iteration-row format as the Node runner."""
    try:
        row = _benchmark_sample(path, expected)
        json.dumps(row, allow_nan=False)
        return row
    except (OverflowError, UnicodeError) as error:
        raise ValueError(f"Malformed Google Benchmark result: {error}") from error


def cpu_list(value):
    cpus = set()
    for item in value.strip().split(","):
        first, separator, last = item.partition("-")
        start, end = int(first), int(last if separator else first)
        if not 0 <= start <= end:
            raise ValueError(f"Invalid CPU list {value!r}")
        cpus.update(range(start, end + 1))
    return cpus


def discover_cpus(root=None, allowed=None):
    """Return one allowed CPU per SMT sibling group, in NUMA round-robin order."""
    root = CPU_SYSFS if root is None else root
    allowed = set(os.sched_getaffinity(0) if allowed is None else allowed)
    notes = []
    try:
        allowed &= cpu_list((root / "online").read_text())
    except (OSError, ValueError):
        notes.append("Using process affinity because the online CPU list is unavailable")
    if not allowed:
        raise ValueError("No eligible online CPU in the process affinity mask")

    def numa_node(cpu):
        nodes = (root / f"cpu{cpu}").glob("node[0-9]*")
        return next((int(node.name[4:]) for node in nodes if node.name[4:].isdecimal()), None)

    groups = defaultdict(deque)
    for cpu in sorted(allowed):
        topology = root / f"cpu{cpu}" / "topology"
        for filename in ("thread_siblings_list", "core_cpus_list"):
            try:
                siblings = cpu_list((topology / filename).read_text())
                if cpu in siblings:
                    break
            except (OSError, ValueError):
                pass
        else:
            notes.append("Using one eligible CPU because physical CPU topology is unavailable")
            cpu = min(allowed)
            return [(cpu, numa_node(cpu))], sorted(allowed), notes
        if cpu == min(siblings & allowed):
            groups[numa_node(cpu)].append(cpu)
    if None in groups:
        notes.append("NUMA node unavailable for one or more eligible cores")
    queues = deque((node, groups[node])
                   for node in sorted(groups, key=lambda node: -1 if node is None else node))
    slots = []
    while queues:
        node, cpus = queues.popleft()
        slots.append((cpus.popleft(), node))
        if cpus:
            queues.append((node, cpus))
    return slots, sorted(allowed), notes


def build_provenance(build, binary):
    cache = {}
    for line in (build / "CMakeCache.txt").read_text().splitlines():
        if line and not line.startswith(("//", "#")) and "=" in line:
            name, value = line.split("=", 1)
            cache[name.split(":", 1)[0]] = value
    required = {
        "CMAKE_BUILD_TYPE": "Release",
        "DANDELION_BUILD_TESTS": "OFF",
        "DANDELION_ENABLE_SANITIZERS": "OFF",
        "DANDELION_BUILD_BENCHMARKS": "ON",
    }
    for name, value in required.items():
        if cache.get(name) != value:
            raise ValueError(f"The measurement build requires {name}={value}")
    if Path(cache["CMAKE_HOME_DIRECTORY"]).resolve() != PROJECT:
        raise ValueError("The measurement build must belong to this Dandelion source tree")
    if build not in binary.parents:
        raise ValueError("The benchmark executable must belong to the measurement build")
    commands = json.loads((build / "compile_commands.json").read_text())
    source_dirs = {PROJECT / item for item in ("src/frontend", "src/node", "benchmarks/frontend")}

    def source_path(entry):
        return (Path(entry["directory"]) / entry["file"]).resolve()

    relevant = [entry for entry in commands if source_path(entry).parent in source_dirs]
    instrumented = re.compile(
        r"sanitize|DANDELION_(?:NODE|FRONTEND)_TESTING|fprofile|finstrument-functions|"
        r"--coverage|(?:^| )-pg(?: |$)")
    if not relevant or any(instrumented.search(entry["command"]) for entry in relevant):
        raise ValueError("The compilation database has missing or instrumented frontend commands")

    def command_output(argv):
        return subprocess.check_output(argv, text=True, timeout=10).strip()

    sources = {source_path(entry) for entry in relevant}
    sources.update(path / "CMakeLists.txt" for path in source_dirs | {PROJECT})
    sources.update(SCRIPT.parent.glob("*.py"))
    sources.update((PROJECT / "cmake").glob("*.cmake"))
    git = ["git", "-C", str(PROJECT)]
    return {
        "build": str(build),
        "configuration": {name: value for name, value in cache.items() if name in required
                          or name.startswith(("CMAKE_CXX_", "CMAKE_EXE_LINKER_FLAGS",
                                              "DANDELION_DEPENDENCY_MODE"))},
        "compiler_version": command_output([cache["CMAKE_CXX_COMPILER"], "--version"]).splitlines()[0],
        "compile_commands": relevant,
        "dandelion_commit": command_output([*git, "rev-parse", "HEAD"]),
        "worktree": command_output([*git, "status", "--short", "--untracked-files=normal"]).splitlines(),
        "source_hashes": {str(path.relative_to(PROJECT)): sha256(path) for path in sorted(sources)},
    }


def unsigned(value):
    return type(value) is int and value >= 0


def finite_nonnegative(value):
    try:
        return type(value) in (int, float) and value >= 0 and math.isfinite(value)
    except OverflowError:
        return False


def decode_json(data):
    """Reject non-finite numbers before retaining raw JSON fields in results."""
    value = json.loads(data)
    json.dumps(value, allow_nan=False)
    return value


def load_cases(manifest_paths, selected):
    cases = []
    manifests = []
    seen = set()
    for path in manifest_paths:
        path = outside_dataset(path)
        raw = path.read_bytes()
        manifest = decode_json(raw)
        if (not isinstance(manifest, dict) or type(manifest.get("schema")) is not int
                or manifest["schema"] != 1 or not isinstance(manifest.get("cases"), list)):
            raise ValueError(f"Invalid frontend manifest schema in {path}")
        manifests.append({"path": str(path), "sha256": hashlib.sha256(raw).hexdigest(),
                          "metadata": {key: value for key, value in manifest.items() if key != "cases"}})
        ids = set()
        for entry in manifest["cases"]:
            if not isinstance(entry, dict) or not isinstance(entry.get("case"), str) or not entry["case"]:
                raise ValueError(f"Missing case identifier in {path}")
            name = entry["case"]
            if name in ids:
                raise ValueError(f"Duplicate case {name!r} in {path}")
            ids.add(name)
            if selected is None or name in selected:
                seen.add(name)
                cases.append({"case": name, "manifest": str(path), "input": entry, "samples": []})
    if selected is not None:
        for name in dict.fromkeys(selected):
            if name not in seen:
                cases.append({"case": name, "status": "unavailable", "samples": [],
                              "message": "Case not found in the supplied manifests"})
    return cases, manifests


def verified_input(case):
    entry = case["input"]
    relative = Path(entry["relative_path"])
    if relative.is_absolute() or ".." in relative.parts or relative.suffix != ".smt2":
        raise ValueError(f"Invalid relative SMT-LIB path {relative}")
    root = Path(case["manifest"]).parent
    path = outside_dataset(root / relative)
    if root not in path.parents:
        raise ValueError("Input resolves outside the manifest directory")
    if not path.is_file():
        raise FileNotFoundError(f"Missing regular input file {path}")
    for key in COUNTS:
        if (key == "bytes" or key in entry) and not unsigned(entry.get(key)):
            raise ValueError(f"Invalid manifest field {key}")
    if entry["bytes"] != path.stat().st_size:
        raise ValueError(f"Input size mismatch for {path}")
    if sha256(path) != entry.get("sha256"):
        raise ValueError(f"Input SHA-256 mismatch for {path}")
    return path


def inspect_sample(path, expected):
    metrics = decode_json(path.read_bytes())
    if (not isinstance(metrics, dict) or type(metrics.get("schema_version")) is not int
            or metrics["schema_version"] != 1 or metrics.get("status") not in INSPECT_STATUSES):
        raise ValueError("Invalid frontend inspection schema or status")
    if not isinstance(metrics.get("diagnostic_message"), str):
        raise ValueError("Invalid frontend inspection diagnostic")
    if metrics["status"] != "ok":
        return metrics
    for key in INSPECT_COUNTS:
        if not unsigned(metrics.get(key)):
            raise ValueError(f"Invalid frontend inspection count {key}")
    for key in (*RSS_FIELDS, *STORAGE_FIELDS):
        if key not in metrics or (metrics[key] is not None and not unsigned(metrics[key])):
            raise ValueError(f"Invalid frontend inspection memory metric {key}")
    for key in RATIOS:
        if key not in metrics or (metrics[key] is not None and not finite_nonnegative(metrics[key])):
            raise ValueError(f"Invalid frontend inspection memory ratio {key}")
    if (not isinstance(metrics.get("logic"), str) or not metrics["logic"]
            or metrics["diagnostic_message"] or metrics["checks"] != 1
            or metrics["commands"] != metrics["applied_commands"]
            or metrics["diagnostic_code"] is not None or metrics["diagnostic_span"] is not None):
        raise ValueError("Inconsistent successful frontend inspection")
    if "logic" in expected and metrics["logic"] != expected["logic"]:
        raise ValueError("Inspected logic differs from the manifest")
    for key, field in COUNTS.items():
        if key in expected and metrics[field] != expected[key]:
            raise ValueError(f"Inspected {field} differs from the manifest")
    return metrics


class LauncherCancelled(BaseException):
    pass


def reap_descendants():
    """Reap descendants adopted by this launcher's Linux subreaper."""
    pid = os.getpid()
    children = Path(f"/proc/{pid}/task/{pid}/children")
    group = os.getpgrp()
    try:
        while True:
            reaped, _ = os.waitpid(-1, os.WNOHANG)
            if reaped:
                continue
            # On cancellation during Popen the child may still be setting up
            # its new session. Never signal the launcher's own process group.
            for child in map(int, children.read_text().split()):
                try:
                    child_group = os.getpgid(child)
                    if child_group == group:
                        os.kill(child, signal.SIGKILL)
                    else:
                        os.killpg(child_group, signal.SIGKILL)
                except ProcessLookupError:
                    pass
            os.waitpid(-1, 0)
    except ChildProcessError:
        pass


def launch_child(arguments):
    """Run once in a fresh interpreter so run_process can safely use preexec_fn."""
    prefix, timeout, memory_mib, cpu, *argv = arguments
    prefix = Path(prefix)
    cancelled = False

    def interrupt(signum, _frame):
        nonlocal cancelled
        if not cancelled:
            cancelled = True
            raise LauncherCancelled

    signal.signal(signal.SIGTERM, interrupt)
    signal.signal(signal.SIGINT, interrupt)
    try:
        # Adopt any descendants orphaned when run_process kills the GNU time group.
        pr_set_child_subreaper = 36
        libc = ctypes.CDLL(None, use_errno=True)
        if libc.prctl(pr_set_child_subreaper, 1, 0, 0, 0) != 0:
            raise OSError(ctypes.get_errno(), "Cannot register the launcher as a child subreaper")
        result = run_process(argv, prefix, int(timeout), int(memory_mib),
                             cpu=int(cpu), child_rss=True)
    except (LauncherCancelled, ChildProcessError):
        if not cancelled:
            raise
        return 128 + signal.SIGTERM
    except OSError as error:
        result = {"status": "launch-error", "message": str(error)}
    finally:
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
        signal.signal(signal.SIGINT, signal.SIG_IGN)
        reap_descendants()
    write_json(prefix.with_suffix(".launcher.json"), result)
    return 0


def process_failure(process):
    message = process.get("message", "")
    stderr = process.get("stderr")
    if not message and stderr and Path(stderr).is_file():
        try:
            with Path(stderr).open(encoding="utf-8", errors="replace") as stream:
                message = stream.read(4096).strip()
        except OSError as error:
            message = str(error)
    status = process["status"]
    if status == "failed" and RESOURCE_ERROR.search(message):
        status = "resource-limit"
    return status, message or status


@dataclass
class Job:
    case_index: int
    ordinal: int
    sample: dict
    prefix: Path


def finish_job(job, process, case, cancelled):
    sample = job.sample
    result = dict(process)
    sample.update(result)
    if cancelled:
        sample.update(status="cancelled", message="Suite interrupted")
        return
    if result["status"] not in ("ok", "failed"):
        sample["status"], sample["message"] = process_failure(result)
        return
    try:
        if job.ordinal < 0:
            metrics = inspect_sample(Path(sample["stdout"]), case["input"])
            sample["inspection"] = metrics
            if metrics["status"] != "ok":
                status = metrics["status"]
                sample.update(status="resource-limit" if status == "resource" else status,
                              message=metrics["diagnostic_message"])
                return
        else:
            name = "frontend/parse/" + sample["scenario"]
            row = benchmark_sample(Path(sample["raw_benchmark_json"]), name)
            sample["measurement"] = row
            if result["status"] == "ok":
                unit = UNITS_MS[row["time_unit"]]
                wall_ms = row["real_time"] * unit
                cpu_ms = row["cpu_time"] * unit
                if not all(map(finite_nonnegative, (wall_ms, cpu_ms))):
                    raise ValueError("Benchmark times in milliseconds are out of range")
                sample.update(wall_ms=wall_ms, cpu_ms=cpu_ms)
                if sample["scenario"] in ADAPTERS:
                    seconds = wall_ms / 1000
                    if seconds <= 0:
                        raise ValueError("Zero wall time makes parsing rates unavailable")
                    metrics = case["inspect"]["inspection"]
                    rates = dict(zip(RATE_FIELDS, (metrics["bytes"] / MIB / seconds,
                                                  metrics["tokens"] / seconds,
                                                  metrics["nodes"] / seconds)))
                    if not all(finite_nonnegative(value) for value in rates.values()):
                        raise ValueError("Parsing rates are out of range")
                    sample.update(rates)
                if sample["scenario"] == "phases":
                    for key in PHASES:
                        if not finite_nonnegative(row.get(key)):
                            raise ValueError(f"Invalid Google Benchmark phase counter {key}")
                    sample.update({key: row[key] for key in PHASES})
    except (ValueError, KeyError, TypeError, OSError, OverflowError) as error:
        sample["message"] = str(error)
        if result["status"] == "ok":
            sample["status"] = "invalid-inspect" if job.ordinal < 0 else "benchmark-failed"
        else:
            sample["status"], _ = process_failure(result)
        if sample["status"] in ("failed", "benchmark-failed") and RESOURCE_ERROR.search(str(error)):
            sample["status"] = "resource-limit"
        return
    if result["status"] != "ok":
        sample["status"], sample["message"] = process_failure(result)


@contextmanager
def cancellation_flag():
    signals = []

    def interrupt(signum, _frame):
        if not signals:
            signals.append(signum)

    # Library callers in other threads cannot install process signal handlers.
    handlers = {}
    if threading.current_thread() is threading.main_thread():
        for signum in (signal.SIGINT, signal.SIGTERM):
            handlers[signum] = signal.signal(signum, interrupt)
    try:
        yield signals
    finally:
        for signum, handler in handlers.items():
            signal.signal(signum, handler)


def execute(cases, binary, output, settings, slots, timed):
    mode = settings["mode"]
    timeout = settings["timeout_s"]
    allowance = settings["memory_mib"]
    budget = settings["memory_budget_mib"]
    events = []
    active = {}
    free = deque(slots)
    pending = []
    reserved = high_water = 0
    started = time.monotonic()

    def event(action, job):
        events.append({
            "event": action, "elapsed_s": time.monotonic() - started,
            "case": cases[job.case_index]["case"], "case_index": job.case_index,
            "scenario": job.sample["scenario"], "repetition": job.sample.get("repetition"),
            "cpu_affinity": job.sample["cpu_affinity"], "memory_mib": allowance,
            "reserved_mib": reserved,
        })

    def unavailable(case, status, message):
        case["samples"].extend({"scenario": scenario, "repetition": repeat, "mode": mode,
                                "status": status, "message": message}
                               for scenario, repeat in timed[len(case["samples"]):])

    def complete(job, slot, process, cancelled=False):
        nonlocal reserved
        reserved -= allowance
        event("release", job)
        free.append(slot)
        case = cases[job.case_index]
        finish_job(job, process, case, cancelled)
        if job.ordinal < 0:
            if job.sample["status"] == "ok" and not cancelled:
                heapq.heappush(pending, (0, job.case_index))
            else:
                if cancelled:
                    unavailable(case, "cancelled", "Suite interrupted")
                else:
                    unavailable(case, "unavailable", "Frontend inspection failed")

    def process_record(job, launcher_status):
        try:
            return json.loads(job.prefix.with_suffix(".launcher.json").read_text(encoding="utf-8"))
        except (OSError, ValueError) as error:
            return {
                "status": "launch-error", "message": f"Launcher exited {launcher_status}: {error}",
                "stdout": str(job.prefix.with_suffix(".stdout")),
                "stderr": str(job.prefix.with_suffix(".stderr")),
                "rss_output": str(job.prefix.with_suffix(".rusage")),
            }

    with cancellation_flag() as interrupted:
        try:
            for index, case in enumerate(cases):
                case["mode"] = mode
                if interrupted:
                    break
                if "status" not in case:
                    try:
                        case["input_path"] = str(verified_input(case))
                        heapq.heappush(pending, (-1, index))
                    except FileNotFoundError as error:
                        case.update(status="missing", message=str(error))
                    except (OSError, TypeError, ValueError, KeyError) as error:
                        case.update(status="corrupt", message=str(error))
                if "status" in case:
                    unavailable(case, "unavailable", case["message"])

            while (pending or active) and not interrupted:
                while pending and free and reserved + allowance <= budget and not interrupted:
                    ordinal, index = heapq.heappop(pending)
                    case = cases[index]
                    directory = output / "cases" / f"{index + 1:04d}"
                    directory.mkdir(parents=True, exist_ok=True)
                    argv = [str(binary), "--input", case["input_path"]]
                    if ordinal < 0:
                        scenario = "inspect"
                        prefix = directory / scenario
                        argv.append("--inspect")
                        sample = case["inspect"] = {"scenario": scenario}
                    else:
                        scenario, repeat = timed[ordinal]
                        prefix = directory / f"{scenario}-{repeat}"
                        raw = prefix.with_suffix(".json")
                        name = "frontend/parse/" + scenario
                        argv.extend(("--scenario", scenario, f"--benchmark_filter=^{name}(/real_time)?$",
                                     "--benchmark_min_time=1x", f"--benchmark_out={raw}",
                                     "--benchmark_out_format=json"))
                        sample = {"scenario": scenario, "repetition": repeat, "raw_benchmark_json": str(raw)}
                        case["samples"].append(sample)
                        if ordinal + 1 < len(timed):
                            heapq.heappush(pending, (ordinal + 1, index))
                    slot = free.popleft()
                    cpu, node = slot
                    launcher_log = prefix.with_suffix(".launcher.stderr")
                    sample.update(mode=mode, cpu_affinity=cpu, numa_node=node, argv=argv,
                                  stdout=str(prefix.with_suffix(".stdout")),
                                  stderr=str(prefix.with_suffix(".stderr")),
                                  launcher_stderr=str(launcher_log))
                    job = Job(index, ordinal, sample, prefix)
                    reserved += allowance
                    high_water = max(high_water, reserved)
                    event("reserve", job)
                    try:
                        with launcher_log.open("wb") as log:
                            launcher = subprocess.Popen(
                                [sys.executable, "-B", str(SCRIPT), "--launch-child", str(prefix),
                                 str(timeout), str(allowance), str(cpu), *argv],
                                stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                stderr=log, start_new_session=True)
                    except (OSError, subprocess.SubprocessError) as error:
                        complete(job, slot, {"status": "launch-error", "message": str(error),
                                             "stderr": str(launcher_log)})
                    else:
                        active[launcher] = job, slot

                completed = False
                for launcher, (job, slot) in list(active.items()):
                    code = launcher.poll()
                    if code is not None:
                        del active[launcher]
                        complete(job, slot, process_record(job, code))
                        completed = True
                if active and not completed and not interrupted:
                    time.sleep(0.005)
        finally:
            for launcher in active:
                launcher.terminate()
            for launcher, (job, slot) in active.items():
                code = launcher.wait()
                complete(job, slot, process_record(job, code), cancelled=True)

    for case in cases:
        case["mode"] = mode
        if interrupted:
            unavailable(case, "cancelled", "Suite interrupted")
        if "status" not in case:
            inspection_status = case.get("inspect", {}).get("status", "cancelled")
            if inspection_status != "ok":
                case["status"] = inspection_status
                case["message"] = case.get("inspect", {}).get("message", "Suite interrupted")
            else:
                case["status"] = ("ok" if all(sample["status"] == "ok" for sample in case["samples"])
                                  else "failed")
    return {
        "elapsed_s": time.monotonic() - started, "events": events,
        "memory_high_water_mib": high_water, "memory_reserved_mib": reserved,
        "interrupted_signal": interrupted[0] if interrupted else None,
    }


def summarize(result, execution):
    all_samples = [sample for case in result["cases"] for sample in case["samples"]]
    result["status_counts"] = {
        "cases": dict(Counter(case["status"] for case in result["cases"])),
        "inspect": dict(Counter(case["inspect"]["status"] for case in result["cases"] if "inspect" in case)),
        "samples": dict(Counter(sample["status"] for sample in all_samples)),
    }
    batch = result["batch"] = {key: value for key, value in execution.items() if key != "events"}
    result["memory_events"] = execution["events"]
    parsed_bytes = parsed_tokens = parsed_nodes = 0
    metric_keys = ("wall_ms", "cpu_ms", "wall_s", "user_s", "system_s", "rss_kib", *PHASES, *RATE_FIELDS)
    for case in result["cases"]:
        groups = defaultdict(list)
        for sample in case["samples"]:
            if sample["status"] == "ok":
                groups[sample["scenario"]].append(sample)
                if sample["scenario"] in ADAPTERS:
                    inspect = case["inspect"]["inspection"]
                    iterations = sample["measurement"]["iterations"]
                    parsed_bytes += inspect["bytes"] * iterations
                    parsed_tokens += inspect["tokens"] * iterations
                    parsed_nodes += inspect["nodes"] * iterations
        case["summary"] = {}
        for scenario, samples in groups.items():
            summary = case["summary"][scenario] = {"successful_samples": len(samples), "metrics": {}}
            for key in metric_keys:
                values = [sample[key] for sample in samples if key in sample and sample[key] is not None]
                if values:
                    low, high = min(values), max(values)
                    summary["metrics"][key] = {
                        "median": statistics.median(values), "min": low, "max": high,
                        "spread": high - low,
                    }
    elapsed = batch["elapsed_s"]
    batch.update(
        successful_samples_per_s=result["status_counts"]["samples"].get("ok", 0) / elapsed,
        successful_cases_per_s=result["status_counts"]["cases"].get("ok", 0) / elapsed,
        adapter_mib_per_s=parsed_bytes / MIB / elapsed,
        adapter_tokens_per_s=parsed_tokens / elapsed,
        adapter_nodes_per_s=parsed_nodes / elapsed,
    )
    result["status"] = (
        "ok" if result["cases"] and execution["interrupted_signal"] is None
        and all(case["status"] == "ok" for case in result["cases"]) else "failed")


def report(result):
    def display(value):
        return "—" if value is None else format(value, ".6g")

    def cell(value):
        return str(value).replace("|", "\\|").replace("\r", " ").replace("\n", " ")

    settings = result["settings"]
    batch = result["batch"]
    lines = [
        "# Dandelion frontend benchmarks", "",
        f"Run status **{result['status']}**. Mode **{settings['mode']}** with "
        f"{settings['effective_jobs']} concurrent slots.",
        f"Suite elapsed time {display(batch['elapsed_s'])} s. "
        f"Successful samples per second {display(batch['successful_samples_per_s'])}. "
        f"Batch adapter throughput {display(batch['adapter_mib_per_s'])} MiB/s.", "",
    ]
    for kind, counts in result["status_counts"].items():
        counts_text = ", ".join(f"{status} {count}" for status, count in sorted(counts.items()))
        lines.append(f"- {kind.capitalize()} outcomes — {counts_text or 'none'}")
    lines.extend([
        "", "## Successful sample medians", "",
        "Wall and CPU times are per iteration. Parentheses show the minimum and maximum.",
        "Child peak RSS comes from GNU time and covers the whole measured process.", "",
        "| Case | Scenario | Mode | Samples | Wall ms | CPU ms | MiB/s | Tokens/s | Nodes/s | Syntax ns | Apply ns | Teardown ns | Child peak KiB |",
        "| --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ])
    columns = ("wall_ms", "cpu_ms", *RATE_FIELDS, *PHASES, "rss_kib")
    for case in result["cases"]:
        for scenario, summary in case["summary"].items():
            values = []
            for key in columns:
                metric = summary["metrics"].get(key)
                values.append("—" if metric is None else
                              f"{display(metric['median'])} ({display(metric['min'])}–{display(metric['max'])})")
            lines.append("| " + " | ".join([cell(case["case"]), scenario, case["mode"],
                                              str(summary["successful_samples"]), *values]) + " |")

    lines.extend(["", "## Inspection", "",
                  "RSS boundaries describe the inspected parse and teardown. Storage is live manager "
                  "accounting at retention. Peak table bytes are separate from retained table bytes.", "",
                  "| Case | Bytes | Tokens | Nodes | Edges | Depth | Baseline KiB | Peak KiB | Retained KiB | Post-teardown KiB | Transient KiB | Peak/Input | Retained/Input | Inspect child peak KiB |",
                  "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
                  ])
    inspected = []
    for case in result["cases"]:
        child = case.get("inspect", {})
        if child.get("status") == "ok":
            metrics = child["inspection"]
            fields = ("bytes", "tokens", "nodes", "edges", "depth", *RSS_FIELDS, *RATIOS)
            values = [display(metrics[key]) for key in fields] + [display(child.get("rss_kib"))]
            lines.append("| " + " | ".join([cell(case["case"]), *values]) + " |")
            inspected.append((case["case"], metrics))
    lines.extend(["", "| Case | Live nodes | Live edges | Node bytes | Numeric backing bytes | Metadata bytes | Table bytes | Peak table bytes |",
                  "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |"])
    for name, metrics in inspected:
        values = [display(metrics[key]) for key in STORAGE_FIELDS]
        lines.append("| " + " | ".join([cell(name), *values]) + " |")

    failures = []
    for case in result["cases"]:
        name = cell(case["case"])
        if case["status"] != "ok" and "message" in case:
            failures.append(f"- {name} — {case['status']} ({cell(case['message'])})")
        for sample in case["samples"]:
            if sample["status"] != "ok":
                failures.append(f"- {name} / {sample['scenario']} / sample {sample['repetition']} "
                                f"— {sample['status']} ({cell(sample.get('message', ''))})")
    if failures:
        lines.extend(["", "## Failed or unavailable selections", "", *failures])
    lines.extend(["", "Raw measurements, child logs, provenance, and limits are recorded in results.json.", ""])
    return "\n".join(lines)


def _run_manifests(manifest_paths, *, output_dir, benchmark_exe=None, build_dir=None,
                   jobs=None, mode="throughput", scenarios=None, samples=3, timeout=60,
                   memory_mib=8192, memory_budget_mib=None, cases=None):
    output = outside_dataset(output_dir)
    if mode not in ("throughput", "isolated"):
        raise ValueError(f"Unknown execution mode {mode!r}")
    limits = [samples, timeout, memory_mib, *(value for value in (jobs, memory_budget_mib)
                                             if value is not None)]
    if any(type(value) is not int or value <= 0 for value in limits):
        raise ValueError("Specify positive integer samples, timeout, job count, and memory limits")
    scenarios = SCENARIOS if scenarios is None else tuple(scenarios)
    if not scenarios or any(scenario not in SCENARIOS for scenario in scenarios):
        raise ValueError(f"Invalid frontend scenarios {scenarios!r}")
    scenarios = tuple(dict.fromkeys(scenarios))
    slots, available, topology_notes = discover_cpus()
    slots = slots[:1 if mode == "isolated" else jobs]
    if memory_budget_mib is None:
        memory_budget_mib = memory_mib * len(slots)
    if memory_budget_mib < memory_mib:
        raise ValueError("The per-child memory allowance exceeds the total memory budget")
    effective = min(len(slots), memory_budget_mib // memory_mib)

    build = outside_dataset(build_dir) if build_dir is not None else None
    if benchmark_exe is None and build is None:
        raise ValueError("Specify either a build directory or a benchmark executable")
    binary = outside_dataset(benchmark_exe if benchmark_exe is not None else build / BENCHMARK_BINARY)
    if not binary.is_file() or not os.access(binary, os.X_OK):
        raise ValueError(f"Missing executable benchmark binary {binary}")

    try:
        model = next((line.split(":", 1)[1].strip() for line in Path("/proc/cpuinfo").read_text().splitlines()
                      if line.startswith("model name")), platform.processor())
    except OSError:
        model = platform.processor()
    environment = {
        "build": None, "binary": str(binary), "binary_sha256": sha256(binary),
        "platform": platform.platform(), "cpu": model, "load_average": os.getloadavg(),
        "cpu_affinity_available": available, "topology_notes": topology_notes,
    }
    if build is not None:
        environment.update(build_provenance(build, binary))

    inputs, manifests = load_cases(manifest_paths, cases)
    output.mkdir(parents=True)
    settings = {
        "mode": mode, "requested_jobs": jobs, "cpu_slots": len(slots), "effective_jobs": effective,
        "slots": [{"cpu": cpu, "numa_node": node} for cpu, node in slots],
        "scenarios": scenarios, "samples": samples, "timeout_s": timeout, "memory_mib": memory_mib,
        "memory_budget_mib": memory_budget_mib, "benchmark_iterations": 1,
    }
    result = {"schema": 1, "command": [sys.executable, *sys.argv], "environment": environment,
              "settings": settings, "manifests": manifests, "cases": inputs}
    timed = [(scenario, repeat) for scenario in scenarios for repeat in range(1, samples + 1)]
    execution = execute(inputs, binary, output, settings, slots, timed)
    summarize(result, execution)
    write_json(output / "results.json", result)
    (output / "report.md").write_text(report(result), encoding="utf-8")
    return result


def run_suite(*, manifest_path: Path, output_dir: Path, benchmark_exe: Path | None = None,
              build_dir: Path | None = None, jobs: int | None = None, mode: str = "throughput",
              scenarios: tuple[str, ...] | list[str] | None = None, samples: int = 3, timeout: int = 60,
              memory_mib: int = 8192, memory_budget_mib: int | None = None,
              cases: list[str] | None = None) -> dict:
    return _run_manifests(
        (manifest_path,), output_dir=output_dir, benchmark_exe=benchmark_exe, build_dir=build_dir,
        jobs=jobs, mode=mode, scenarios=scenarios, samples=samples, timeout=timeout,
        memory_mib=memory_mib, memory_budget_mib=memory_budget_mib, cases=cases,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mode", choices=("throughput", "isolated"), default="throughput")
    parser.add_argument("--jobs", type=int)
    parser.add_argument("--scenario", action="append", help="Scenario or comma-separated scenarios")
    parser.add_argument("--samples", type=int, default=3)
    parser.add_argument("--timeout", type=int, default=60)
    parser.add_argument("--memory-mib", type=int, default=8192)
    parser.add_argument("--memory-budget-mib", type=int)
    parser.add_argument("--case", action="append")
    args = parser.parse_args()
    scenarios = [scenario for item in args.scenario for scenario in item.split(",")] if args.scenario else None
    try:
        result = _run_manifests(
            args.manifest, output_dir=args.output, build_dir=args.build_dir, jobs=args.jobs,
            mode=args.mode, scenarios=scenarios, samples=args.samples, timeout=args.timeout,
            memory_mib=args.memory_mib, memory_budget_mib=args.memory_budget_mib, cases=args.case,
        )
    except (OSError, ValueError, KeyError, TypeError, subprocess.SubprocessError) as error:
        parser.error(str(error))
    interrupted = result["batch"]["interrupted_signal"]
    return 128 + interrupted if interrupted else int(result["status"] != "ok")


if __name__ == "__main__":
    if sys.argv[1:2] == ["--launch-child"]:
        raise SystemExit(launch_child(sys.argv[2:]))
    raise SystemExit(main())

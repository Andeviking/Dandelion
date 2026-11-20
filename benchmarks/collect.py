#!/usr/bin/env python3
"""Capture a bounded cvc5 invocation on one explicitly selected safe copy."""

import argparse
import os
from pathlib import Path
import re
import statistics
import sys

from cvc5_trace import TraceError, read_trace, summarize, trace_terminal
from node_workloads import compile_workloads
from process import outside_dataset, run_process, sha256, write_json

REFERENCE_REVISION = "282d5e0c84baf49c780704c532f87ba66d8b513d"
PATCH = Path(__file__).with_name("reference") / "cvc5-282d5e0c.patch"


def capture(args, output, record):
    source = outside_dataset(args.input)
    solver = outside_dataset(args.solver)
    if not source.is_file():
        raise TraceError(f"missing SMT-LIB input {source}", "missing")
    if not solver.is_file():
        raise TraceError(f"missing instrumented solver {solver}", "missing")
    if source.stat().st_size > args.input_bytes:
        raise TraceError("SMT-LIB input exceeds configured byte limit", "resource-limit")
    staged = output / "input.smt2"
    with source.open("rb") as stream, staged.open("wb") as copied:
        copied.write(stream.read(args.input_bytes + 1))
    if staged.stat().st_size > args.input_bytes:
        raise TraceError("SMT-LIB input exceeds configured byte limit", "resource-limit")
    input_hash = sha256(staged)
    record["source"] = {
        "path": str(source), "relative_path": args.source_relative,
        "evidence": args.source_evidence, "sha256": input_hash,
        "bytes": staged.stat().st_size,
    }
    if args.expected_sha256 and input_hash != args.expected_sha256:
        raise TraceError("input SHA-256 differs from the manifest", "corrupt")

    reference = {"source_revision": REFERENCE_REVISION, "executable": str(solver),
                 "executable_sha256": sha256(solver),
                 "instrumentation_patch_sha256": sha256(PATCH), "mode": args.mode}
    record["reference"] = reference
    env = dict(os.environ)
    for variable in ("DANDELION_CVC5_TRACE", "DANDELION_CVC5_TRACE_BYTES"):
        env.pop(variable, None)
    argv = [solver, "--no-early-exit", "--no-incremental", "--no-use-portfolio",
            "--lang=smt2"]
    if args.mode == "input":
        argv.append("--parse-only")
    if args.mode == "preprocess":
        argv.append("--preprocess-only")
    argv.append(staged)
    reference["version_process"] = run_process(
        [solver, "--version"], output / "version", args.timeout, args.memory_mib, env=env)
    version = Path(reference["version_process"]["stdout"]).read_text(errors="replace")
    reference["version"] = version.splitlines()[0] if version else "unavailable"

    baseline = []
    reference["without_logging"] = baseline
    for repetition in range(args.baseline_samples):
        baseline.append(run_process(argv, output / f"baseline-{repetition + 1}",
                                    args.timeout, args.memory_mib, env=env, cpu=args.cpu))
    path = output / "reference.trace"
    env.update(DANDELION_CVC5_TRACE=str(path),
               DANDELION_CVC5_TRACE_BYTES=str(args.trace_bytes))
    reference["trace_byte_limit"] = args.trace_bytes
    observed = run_process(argv, output / "reference", args.timeout, args.memory_mib,
                           env=env, cpu=args.cpu, file_bytes=args.trace_bytes + 4096)
    reference["process"] = observed
    if path.is_file():
        terminal = trace_terminal(path)
        reference.update(trace_file=str(path), trace_bytes=path.stat().st_size,
                         trace_sha256=sha256(path),
                         footer=terminal if terminal.startswith(("end ", "truncated "))
                         else None)
    if observed["status"] != "ok":
        error = Path(observed["stderr"]).read_text(errors="replace")[-2048:]
        status = observed["status"]
        if "bad_alloc" in error or "Cannot allocate memory" in error:
            status = "resource-limit"
        raise TraceError(observed.get("message", error.strip()) or status, status)

    answers = [word for word in Path(observed["stdout"]).read_text().splitlines()
               if word in ("sat", "unsat", "unknown")]
    reference["answer"] = answers[0] if len(answers) == 1 else None
    if not path.is_file():
        raise TraceError("instrumented solver did not produce a trace", "missing")
    trace = read_trace(path)
    reference["trace_events"] = trace.count
    record["phases"] = summarize(trace)

    if (sum(command in ("check-sat", "check-sat-assuming") for command in trace.commands) != 1
            or any(command in {"push", "pop", "reset", "reset-assertions",
                               "define-fun-rec", "define-funs-rec"}
                   for command in trace.commands)):
        raise TraceError("capture requires one nonincremental query", "unsupported")
    if args.mode != "input" and len(answers) != 1:
        raise TraceError("missing or repeated reference answers", "incomplete")
    if args.mode == "solve" and answers[0] == "unknown":
        raise TraceError("reference solve returned unknown", "inconclusive")
    if args.mode == "preprocess" and not trace.roots.get("preprocessed"):
        raise TraceError("preprocessing snapshot unavailable", "incomplete")
    comparable = [sample for sample in baseline if sample["status"] == "ok"
                  and Path(sample["stdout"]).read_bytes() == Path(observed["stdout"]).read_bytes()]
    if comparable:
        cpu_without = statistics.median(sample["user_s"] + sample["system_s"]
                                        for sample in comparable)
        reference["overhead"] = {
            "comparable_samples": len(comparable), "median_cpu_without_s": cpu_without,
            "cpu_with_s": observed["user_s"] + observed["system_s"],
            "median_wall_without_s": statistics.median(sample["wall_s"] for sample in comparable),
            "wall_with_s": observed["wall_s"],
        }

    record.update(compile_workloads(trace, output, retain_input_roots=args.mode != "input"))
    available = [work for work in record["snapshots"].values() if work["status"] == "ok"]
    if not available:
        raise TraceError("no complete snapshot maps to Dandelion", "mapping-failed")
    for work in [*available, record["mixed"], record["lookup"]]:
        if work["status"] == "ok":
            work["sha256"] = sha256(output / work["file"])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--solver", type=Path, required=True)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True, help="New case directory")
    parser.add_argument("--case", required=True)
    parser.add_argument("--family", required=True)
    parser.add_argument("--origin", choices=("corpus", "fixture"), required=True)
    parser.add_argument("--scale", choices=("light", "medium", "heavy"), required=True)
    parser.add_argument("--source-relative")
    parser.add_argument("--source-evidence")
    parser.add_argument("--expected-sha256")
    parser.add_argument("--mode", choices=("input", "preprocess", "solve"), default="solve")
    parser.add_argument("--timeout", type=float, default=45)
    parser.add_argument("--memory-mib", type=int, default=4096)
    parser.add_argument("--input-bytes", type=int, default=32 * 1024 * 1024)
    parser.add_argument("--trace-bytes", type=int, default=128 * 1024 * 1024)
    parser.add_argument("--baseline-samples", type=int, default=0)
    parser.add_argument("--cpu", type=int)
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", args.case):
        parser.error("Use a single case identifier")
    if not (args.timeout > 0 and args.memory_mib > 0 and args.input_bytes > 0
            and args.trace_bytes > 0 and 0 <= args.baseline_samples <= 10):
        parser.error("Specify positive finite limits and at most ten baseline samples")
    if args.timeout == float("inf"):
        parser.error("Specify a finite timeout")
    if args.origin == "corpus" and not (args.source_relative and args.source_evidence):
        parser.error("Corpus capture requires --source-relative and --source-evidence")
    try:
        output = outside_dataset(args.output)
        output.mkdir(parents=True)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    record = {
        "schema": 1, "case": args.case, "family": args.family,
        "origin": args.origin, "scale": args.scale,
        "command": [sys.executable, *sys.argv],
    }
    try:
        capture(args, output, record)
    except (MemoryError, RecursionError) as error:
        record.update(status="resource-limit", message=type(error).__name__)
        print("Reference conversion exceeded a resource limit", file=sys.stderr)
    except (OSError, ValueError) as error:
        record.update(status=getattr(error, "status", "failed"), message=str(error))
        print(error, file=sys.stderr)
    else:
        record["status"] = "ok"
    write_json(output / "capture.json", record)
    return 0 if record["status"] == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())

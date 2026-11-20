#!/usr/bin/env python3
"""Capture the assertion DAG from one explicitly selected local Bool/BV input."""

import argparse
import json
import math
from pathlib import Path
import re
import sys

from cvc5_trace import TraceError
from process import outside_dataset, run_process, sha256, write_json

REFERENCE_REVISION = "282d5e0c84baf49c780704c532f87ba66d8b513d"
PATCH = Path(__file__).with_name("reference") / "cvc5-282d5e0c-snapshot.patch"


def capture(args, output, record):
    source = outside_dataset(args.input)
    adapter = outside_dataset(args.snapshotter)
    if not source.is_file():
        raise TraceError(f"missing SMT-LIB input {source}", "missing")
    if not adapter.is_file():
        raise TraceError(f"missing cvc5 snapshot adapter {adapter}", "missing")
    info = source.stat()
    if info.st_size > args.input_bytes:
        raise TraceError("SMT-LIB input exceeds the byte limit", "resource-limit")
    digest = sha256(source)
    record["source"] = {
        "path": str(source), "relative_path": args.source_relative,
        "evidence": args.source_evidence, "bytes": info.st_size, "sha256": digest,
    }
    if args.expected_sha256 and digest != args.expected_sha256:
        raise TraceError("input SHA-256 differs from the manifest", "corrupt")
    reference = {
        "source_revision": REFERENCE_REVISION, "mode": "input-snapshot",
        "snapshotter": str(adapter), "snapshotter_sha256": sha256(adapter),
        "adapter_patch_sha256": sha256(PATCH),
    }
    record["reference"] = reference
    workload = output / "input.node"
    process = run_process([adapter, source, workload], output / "snapshot",
                          args.timeout, args.memory_mib, cpu=args.cpu,
                          file_bytes=args.output_bytes, child_rss=True)
    reference["process"] = process
    if process["status"] != "ok":
        status = process["status"]
        if status == "failed":
            status = {2: "empty", 3: "unsupported", 4: "invalid",
                      5: "resource-limit"}.get(process["returncode"], status)
        message = Path(process["stderr"]).read_text(errors="replace").strip()
        raise TraceError(message or status, status)

    after = source.stat()
    if (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns) != (
            after.st_dev, after.st_ino, after.st_size, after.st_mtime_ns):
        raise TraceError("input changed during snapshot generation", "corrupt")
    with workload.open("rb") as stream:
        stream.seek(-4, 2)
        if stream.read() != b"end\n":
            raise TraceError("snapshot workload is missing its footer", "incomplete")
    metrics = json.loads(Path(process["stdout"]).read_text())
    for key in ("requests", "nodes", "roots", "operations", "parser_calls"):
        if type(metrics[key]) is not int or metrics[key] <= 0:
            raise TraceError(f"invalid snapshot metric {key}", "corrupt")
    record["snapshots"] = {
        "input": dict(metrics, status="ok", file=workload.name,
                      bytes=workload.stat().st_size, sha256=sha256(workload)),
    }
    record["mixed"] = {"status": "unsupported",
                       "reason": "assertion snapshot contains no ordered request trace"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshotter", type=Path, required=True)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--case", required=True)
    parser.add_argument("--family", required=True)
    parser.add_argument("--origin", choices=("corpus", "fixture"), required=True)
    parser.add_argument("--scale", choices=("light", "medium", "heavy"), required=True)
    parser.add_argument("--source-relative")
    parser.add_argument("--source-evidence")
    parser.add_argument("--expected-sha256")
    parser.add_argument("--timeout", type=float, default=60)
    parser.add_argument("--memory-mib", type=int, default=4096)
    parser.add_argument("--input-bytes", type=int, default=32 * 1024 * 1024)
    parser.add_argument("--output-bytes", type=int, default=128 * 1024 * 1024)
    parser.add_argument("--cpu", type=int)
    args = parser.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", args.case):
        parser.error("Use a single case identifier")
    if (not math.isfinite(args.timeout) or args.timeout <= 0 or
            min(args.memory_mib, args.input_bytes, args.output_bytes) <= 0):
        parser.error("Specify positive finite limits")
    if args.origin == "corpus" and not (args.source_relative and args.source_evidence):
        parser.error("Corpus snapshots require --source-relative and --source-evidence")
    try:
        output = outside_dataset(args.output)
        output.mkdir(parents=True)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    record = {"schema": 1, "case": args.case, "family": args.family,
              "origin": args.origin, "scale": args.scale,
              "command": [sys.executable, *sys.argv]}
    try:
        capture(args, output, record)
    except (OSError, ValueError, KeyError, TypeError) as error:
        record.update(status=getattr(error, "status", "invalid"), message=str(error))
        print(error, file=sys.stderr)
    else:
        record["status"] = "ok"
    write_json(output / "capture.json", record)
    return 0 if record["status"] == "ok" else 1


if __name__ == "__main__":
    raise SystemExit(main())

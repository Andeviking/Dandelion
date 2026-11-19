"""Opt-in typed parse and reference comparisons for six verified local copies."""

import argparse
import hashlib
import json
import os
import re
import resource
import stat
import subprocess
from pathlib import Path

Z3_REVISION = "430d19e697e2ec9bad79ff210d41b4a7a5fcfa3e"
MANIFEST_HASH = "aa75bc101baec860810afc5aac9db976dcaa822b54e00fc1f1616420968a0e86"
SNAPSHOT = "https://zenodo.org/records/16740866"
PROTECTED = Path("/pub/netdisk1/zhangx")
SAMPLES = {
    "QF_BV/challenge/integerOverflow.smt2": (29, 3, 2, 17, 0),
    "QF_NIA/20220315-MathProblems/STC_0473.smt2": (12, 1, 3, 0, 0),
    "QF_DT/20172804-Barrett/barrett-jsat/tests/v1/v1l50090.cvc.smt2": (13, 1, 3, 0, 1),
    "QF_UFDT/20210312-Bouvier/vlsat3_e80.smt2": (247, 236, 1, 0, 2),
    "QF_AUFBV/ecc/com.galois.ecc.P384ECC64.ec_full_sub3.short.smt2": (185, 121, 57, 0, 0),
    "QF_ABV/egt/egt-0497.smt2": (13, 5, 1, 0, 0),
}
BV_LOGICS = {"QF_BV", "QF_ABV", "QF_AUFBV"}
LIMITS = {"wall_seconds": 5, "cpu_soft_seconds": 3, "cpu_hard_seconds": 4,
          "address_space_bytes": 768 << 20, "file_bytes": 1 << 20}
ERROR_RESPONSE = re.compile(r"\(\s*error(?=[\s)])")
SMT_TOKEN = re.compile(r';[^\r\n]*|\(|\)|\|[^|]*\||"(?:[^"]|"")*"|[^\s();"|]+|["|]')
DISPOSITIONS = {
    "accepted": "passed",
    "available": "passed",
    "matches": "passed",
    "mismatch": "failed",
    "diagnostic": "failed",
    "unsupported": "unsupported",
    "unavailable": "unavailable",
    "timeout": "timed_out",
    "signal": "crashed",
    "inconclusive": "inconclusive",
    "skipped_recovery": "inconclusive",
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def absolute(path):
    return Path(os.path.abspath(path))


def checked_path(path, directory=False):
    path = absolute(path)
    if path == PROTECTED or PROTECTED in path.parents:
        raise ValueError(f"Path is in the protected dataset tree: {path}")
    for parent in reversed(path.parents):
        if not stat.S_ISDIR(parent.lstat().st_mode):
            raise ValueError(f"Path component is not a plain directory: {parent}")
    predicate = stat.S_ISDIR if directory else stat.S_ISREG
    if not predicate(path.lstat().st_mode):
        raise ValueError(f"Unexpected file type: {path}")
    return path


def read_blob(source, path):
    data = checked_path(source / path).read_bytes()
    tracked = subprocess.check_output(
        ["git", "-C", source, "show", f"{Z3_REVISION}:{path.as_posix()}"], timeout=5
    )
    if tracked != data:
        raise ValueError(f"Local bytes differ from the pinned Git blob: {path}")
    return data


def impose_limits():
    resource.setrlimit(resource.RLIMIT_CPU,
                       (LIMITS["cpu_soft_seconds"], LIMITS["cpu_hard_seconds"]))
    resource.setrlimit(resource.RLIMIT_AS, (LIMITS["address_space_bytes"],) * 2)
    resource.setrlimit(resource.RLIMIT_FSIZE, (LIMITS["file_bytes"],) * 2)


def run(argv, output, tag, data=None):
    outfile = output / f"{tag}.stdout"
    errfile = output / f"{tag}.stderr"
    with outfile.open("wb") as out, errfile.open("wb") as err:
        try:
            completed = subprocess.run(
                argv, input=data, stdout=out, stderr=err,
                timeout=LIMITS["wall_seconds"], preexec_fn=impose_limits, check=False
            )
            status = completed.returncode
        except subprocess.TimeoutExpired:
            status = "timeout"
        except (FileNotFoundError, PermissionError):
            status = "unavailable"
    return {
        "argv": [str(item) for item in argv],
        "exit": status,
        "stdout": outfile.read_bytes().decode(errors="replace"),
        "stderr": errfile.read_bytes().decode(errors="replace"),
    }


def process_status(code):
    if isinstance(code, str):
        return code
    if code < 0:
        return "signal"
    if code != 0:
        return "diagnostic"
    return None


def classify(record, expected=None):
    status = process_status(record["exit"])
    if status is not None:
        return status
    stdout = record["stdout"].strip(" \t\r\n")
    if record["stderr"].strip() or ERROR_RESPONSE.search(stdout):
        return "diagnostic"
    if stdout == "unsupported":
        return "unsupported"
    if expected is None:
        return "accepted" if not stdout else "mismatch"
    if stdout == expected:
        return "matches"
    if stdout in {"", "unknown"}:
        return "inconclusive"
    return "mismatch"


def probe_summary(logic, counts):
    commands, assertions, functions, definitions, groups = counts
    return (f"{logic} commands={commands} assertions={assertions} "
            f"functions={functions} definitions={definitions} datatype_groups={groups}\n")


def comparison_shape(script, assertions):
    stack = []
    comparison = None
    checked = False
    for match in SMT_TOKEN.finditer(script):
        token = match.group()
        if token.startswith(";"):
            continue
        if checked:
            return False
        if token == "(":
            stack.append([])
        elif token == ")" and stack:
            form = stack.pop()
            if stack:
                stack[-1].append(form)
            elif not form or not isinstance(form[0], str):
                return False
            elif form[0] == "assert":
                if comparison is not None or len(form) != 2:
                    return False
                comparison = form[1]
            elif form[0] == "check-sat":
                checked = len(form) == 1
                if not checked:
                    return False
        elif stack and token not in {')', '"', '|'}:
            stack[-1].append(token)
        else:
            return False

    if not checked or stack:
        return False
    if assertions == 0:
        return comparison == "false"
    if not isinstance(comparison, list):
        return False
    pairs = comparison[2:] if comparison[:2] == ["or", "false"] else [comparison]
    return len(pairs) == assertions and all(
        isinstance(pair, list) and len(pair) == 3 and pair[0] == "xor"
        for pair in pairs
    )


def classify_probe(record, expected_summary, *, assertions=None):
    code = record["exit"]
    if code == 2:
        return "unsupported"
    if code == 3:
        return "inconclusive"
    status = process_status(code)
    if status is not None:
        return status
    if record["stderr"] != expected_summary:
        return "mismatch"
    stdout = record["stdout"]
    if assertions is not None:
        return "available" if comparison_shape(stdout, assertions) else "inconclusive"
    if ERROR_RESPONSE.search(stdout):
        return "diagnostic"
    return "accepted" if not stdout else "mismatch"


def verification_verdict(report):
    counts = dict.fromkeys(DISPOSITIONS.values(), 0)
    cases = report["cases"].values()
    for case in cases:
        for name, result in case.items():
            if name not in {"typed", "recovery"} and not name.endswith(("_parse", "_xor")):
                continue
            status = result["status"]
            if status == "skipped_profile":
                continue
            disposition = DISPOSITIONS[status]
            if status == "diagnostic" and name.endswith(("_parse", "_xor")):
                disposition = "inconclusive"
            counts[disposition] += 1
    for name, result in report["oracle_controls"].items():
        status = result["status"]
        disposition = DISPOSITIONS[status]
        if status == "diagnostic" and name != "probe":
            disposition = "inconclusive"
        counts[disposition] += 1

    report["dispositions"] = counts
    if counts["failed"]:
        report["verdict"] = "failed"
        return 1
    semantic_evidence = bool(cases) and all(
        any(name.endswith("_xor") and result["status"] == "matches"
            for name, result in case.items())
        for case in cases
    )
    if counts["passed"] != sum(counts.values()) or not semantic_evidence:
        report["verdict"] = "incomplete"
        return 2
    report["verdict"] = "complete"
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--z3-source", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--probe", type=Path, required=True)
    for name in ("z3", "cvc5", "bitwuzla", "yices"):
        parser.add_argument(f"--{name}", type=Path)
    args = parser.parse_args()

    source = checked_path(args.z3_source, directory=True)
    output = absolute(args.out)
    checked_path(output.parent, directory=True)
    if PROTECTED == output or PROTECTED in output.parents:
        raise ValueError("Output is in the protected dataset tree")
    output.mkdir()
    sample = Path("fstar/benchmarks/smtlib-sample")
    manifest_blob = read_blob(source, sample / "manifest.json")
    manifest = json.loads(manifest_blob)
    if sha(manifest_blob) != MANIFEST_HASH or manifest["source"] != SNAPSHOT:
        raise ValueError("Unexpected benchmark manifest")
    entries = {row["path"]: row for row in manifest["cases"]}

    tools = {"probe": args.probe, "z3": args.z3, "cvc5": args.cvc5,
             "bitwuzla": args.bitwuzla, "yices": args.yices}
    versions = {}
    for name, executable in tools.items():
        if name == "probe":
            continue
        versions[name] = (run([executable, "-version" if name == "z3" else "--version"],
                              output, f"{name}-version")
                          if executable else {"exit": "unavailable"})
    report = {"manifest_sha256": MANIFEST_HASH, "source": SNAPSHOT,
              "source_revision": Z3_REVISION, "limits": LIMITS,
              "versions": versions, "cases": {}, "oracle_controls": {}}
    for path, counts in SAMPLES.items():
        row = entries[path]
        logic = row["logic"]
        data = read_blob(source, sample / path)
        if (sha(data) != row["sha256"] or len(data) != row["size_bytes"]
                or row["archive_path"] != f"non-incremental/{path}"):
            raise ValueError(f"Benchmark manifest mismatch: {path}")
        clean = output / f"{logic}.smt2"
        clean.write_bytes(data)
        expected = probe_summary(logic, counts)
        typed = run([args.probe], output, f"{logic}-typed", data)
        typed["status"] = classify_probe(typed, expected)
        case = {"path": path, "sha256": sha(data), "archive_path": row["archive_path"],
                "bytes": len(data), "typed": typed}
        report["cases"][logic] = case
        recovered = run([args.probe, "--compare"], output, f"{logic}-recovered", data)
        recovered["status"] = classify_probe(recovered, expected, assertions=counts[1])
        case["recovery"] = {key: value for key, value in recovered.items() if key != "stdout"}
        comparison = output / f"{logic}-recovered.stdout"
        reference = {
            "cvc5_parse": (args.cvc5, ["--lang=smt2", "--parse-only", clean], None),
            "bitwuzla_parse": (args.bitwuzla, ["--parse-only", clean], None),
            "z3_xor": (args.z3, ["-smt2", comparison], "unsat"),
            "cvc5_xor": (args.cvc5, ["--lang=smt2", comparison], "unsat"),
            "yices_xor": (args.yices, [comparison], "unsat"),
        }
        for name, (executable, flags, expected_reference) in reference.items():
            if name in {"bitwuzla_parse", "yices_xor"} and logic not in BV_LOGICS:
                status = "skipped_profile"
            elif executable is None:
                status = "unavailable"
            elif name.endswith("_xor") and recovered["status"] != "available":
                status = "skipped_recovery"
            else:
                result = run([executable, *flags], output, f"{logic}-{name}")
                result["status"] = classify(result, expected_reference)
                case[name] = result
                continue
            case[name] = {"status": status}
        print(logic, typed["status"], f"recovery={recovered['status']}",
              *(f"{name}={case[name]['status']}" for name in reference))

    clean = (b"(set-logic QF_BV)(declare-const x (_ BitVec 8))"
             b"(assert (= (bvadd x #x01) #x0a))(check-sat)")
    good = run([args.probe, "--compare"], output, "oracle-clean", clean)
    good["status"] = classify_probe(
        good, probe_summary("QF_BV", (4, 1, 1, 0, 0)), assertions=1
    )
    recovered_text = good["stdout"]
    original_term = "(bvadd |x| (_ bv1 8))"
    bad = output / "oracle-perturbed.smt2"
    if good["status"] == "available":
        if recovered_text.count(original_term) == 1:
            bad.write_text(recovered_text.replace(original_term, "(bvadd |x| (_ bv2 8))"))
        else:
            good["status"] = "mismatch"
    report["oracle_controls"]["probe"] = {
        key: value for key, value in good.items() if key != "stdout"
    }
    for name, executable, flags in (
        ("z3", args.z3, ["-smt2"]), ("cvc5", args.cvc5, ["--lang=smt2"]),
        ("yices", args.yices, []),
    ):
        for kind, path, expected in (
            ("same", output / "oracle-clean.stdout", "unsat"),
            ("different", bad, "sat"),
        ):
            label = f"{name}-{kind}"
            if executable is None:
                observed = {"status": "unavailable"}
            elif good["status"] != "available":
                observed = {"status": "skipped_recovery"}
            else:
                observed = run([executable, *flags, path], output, f"oracle-{label}")
                observed["status"] = classify(observed, expected)
            report["oracle_controls"][label] = observed
    verdict = verification_verdict(report)
    (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(report["verdict"], *(f"{name}={count}"
                              for name, count in report["dispositions"].items()))
    return verdict


if __name__ == "__main__":
    raise SystemExit(main())

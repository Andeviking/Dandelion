"""Test the opt-in corpus helper without datasets or reference solvers."""

import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tests.frontend import benchmark_regression as runner


def record(exit_status=0, stdout="", stderr=""):
    return {"exit": exit_status, "stdout": stdout, "stderr": stderr}


class ClassifyTest(unittest.TestCase):
    def test_parse_only_requires_clean_silence(self):
        cases = (
            (record(), "accepted"),
            (record(stdout='(error "parse error")\n'), "diagnostic"),
            (record(stdout="sat\n"), "mismatch"),
            (record(stdout="unsat\n"), "mismatch"),
            (record(stdout="unsupported\n"), "unsupported"),
            (record(stderr="parse warning"), "diagnostic"),
        )
        for observed, expected in cases:
            with self.subTest(observed=observed):
                self.assertEqual(runner.classify(observed), expected)

    def test_xor_requires_a_single_result(self):
        cases = (
            (record(stdout=" \tunsat\r\n"), "matches"),
            (record(stdout="sat\n"), "mismatch"),
            (record(stdout="unknown\n"), "inconclusive"),
            (record(), "inconclusive"),
            (record(stdout="unsat\nsat\n"), "mismatch"),
            (record(stdout="unsat\nunsat\n"), "mismatch"),
            (record(stdout="unsat\nsuccess\n"), "mismatch"),
            (record(stdout='(error "parse error")\nunsat\n'), "diagnostic"),
            (record(stdout='unsat\n(error "late error")\n'), "diagnostic"),
        )
        for observed, expected in cases:
            with self.subTest(observed=observed):
                self.assertEqual(runner.classify(observed, "unsat"), expected)
        self.assertEqual(runner.classify(record(stdout="sat\n"), "sat"), "matches")

    def test_process_status_takes_precedence_over_stdout(self):
        for exit_status, expected in (
            (1, "diagnostic"),
            (2, "diagnostic"),
            (-9, "signal"),
            ("timeout", "timeout"),
            ("unavailable", "unavailable"),
        ):
            with self.subTest(exit_status=exit_status):
                self.assertEqual(
                    runner.classify(record(exit_status, "unsat\n"), "unsat"),
                    expected,
                )


class RunnerVerdictTest(unittest.TestCase):
    def run_case(self, overrides=None, references=True, logic="QF_BV", samples=True):
        path = f"{logic}/sample.smt2"
        data = f"(set-logic {logic})(assert true)(check-sat)".encode()
        sample = Path("fstar/benchmarks/smtlib-sample")
        manifest = json.dumps({
            "source": runner.SNAPSHOT,
            "cases": [{
                "path": path, "logic": logic, "sha256": runner.sha(data),
                "size_bytes": len(data), "archive_path": f"non-incremental/{path}",
            }],
        }).encode()
        summary = (f"{logic} commands=3 assertions=1 functions=0 definitions=0 "
                   "datatype_groups=0\n")
        control_summary = ("QF_BV commands=4 assertions=1 functions=1 "
                           "definitions=0 datatype_groups=0\n")
        comparison = (f"(set-logic {logic})\n"
                      "(assert (xor true true))\n(check-sat)\n")
        control = ("(set-logic QF_BV)\n(declare-const x (_ BitVec 8))\n"
                   "(assert (xor (= (bvadd x #x01) #x0a) "
                   "(= (bvadd |x| (_ bv1 8)) (_ bv10 8))))\n(check-sat)\n")
        overrides = overrides or {}

        def run(argv, output, tag, data=None):
            if tag in overrides:
                observed = overrides[tag]
            elif tag == f"{logic}-typed":
                observed = record(stderr=summary)
            elif tag == f"{logic}-recovered":
                observed = record(stdout=comparison, stderr=summary)
            elif tag == "oracle-clean":
                observed = record(stdout=control, stderr=control_summary)
            elif tag.endswith(("_xor", "-same")):
                observed = record(stdout="unsat\n")
            elif tag.endswith("-different"):
                observed = record(stdout="sat\n")
            elif tag.endswith(("_parse", "-version")):
                observed = record()
            else:
                self.fail(f"Unexpected subprocess: {tag}")
            for stream in ("stdout", "stderr"):
                (output / f"{tag}.{stream}").write_text(observed[stream])
            return observed.copy()

        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "results"
            argv = ["benchmark_regression.py", "--z3-source", directory,
                    "--out", str(output), "--probe", "probe"]
            if references:
                for name in ("z3", "cvc5", "bitwuzla", "yices"):
                    argv.extend((f"--{name}", name))
            blobs = {sample / "manifest.json": manifest, sample / path: data}
            selected = {path: (3, 1, 0, 0, 0)} if samples else {}
            with contextlib.ExitStack() as stack:
                stack.enter_context(patch("sys.argv", argv))
                stack.enter_context(patch.multiple(
                    runner, SAMPLES=selected, MANIFEST_HASH=runner.sha(manifest)
                ))
                stack.enter_context(patch.object(
                    runner, "read_blob", side_effect=lambda _, key: blobs[key]
                ))
                stack.enter_context(patch.object(runner, "run", side_effect=run))
                stack.enter_context(contextlib.redirect_stdout(io.StringIO()))
                status = runner.main()
            report = json.loads((output / "report.json").read_text())
            perturbed = output / "oracle-perturbed.smt2"
            if perturbed.exists():
                self.assertEqual(
                    perturbed.read_text(),
                    control.replace("(bvadd |x| (_ bv1 8))", "(bvadd |x| (_ bv2 8))"),
                )
        return status, report

    def assert_verdict(self, status, report, expected, verdict, **counts):
        self.assertEqual(status, expected)
        self.assertEqual(report["verdict"], verdict)
        expected_counts = dict.fromkeys(
            ("passed", "failed", "unsupported", "unavailable", "timed_out",
             "crashed", "inconclusive"),
            0,
        )
        expected_counts.update(counts)
        self.assertEqual(report["dispositions"], expected_counts)

    def test_complete_verification_and_profile_skips(self):
        status, report = self.run_case()
        self.assert_verdict(status, report, 0, "complete", passed=14)
        status, report = self.run_case(logic="QF_NIA")
        self.assert_verdict(status, report, 0, "complete", passed=12)
        self.assertEqual(report["cases"]["QF_NIA"]["bitwuzla_parse"]["status"],
                         "skipped_profile")

    def test_no_reference_executables_is_incomplete(self):
        status, report = self.run_case(references=False)
        self.assert_verdict(status, report, 2, "incomplete", passed=3, unavailable=11)

    def test_passing_controls_without_sample_evidence_are_incomplete(self):
        status, report = self.run_case(samples=False)
        self.assert_verdict(status, report, 2, "incomplete", passed=7)

    def test_invalid_typed_protocol_fails(self):
        summary = ("QF_BV commands=3 assertions=1 functions=0 definitions=0 "
                   "datatype_groups=0\n")
        for observed in (
            record(stdout="sat\n", stderr=summary),
            record(stdout='(error "bad")\n', stderr=summary),
            record(stderr=summary.replace("assertions=1", "assertions=0")),
            record(stderr=summary + "warning\n"),
        ):
            with self.subTest(observed=observed):
                status, report = self.run_case({"QF_BV-typed": observed})
                self.assert_verdict(status, report, 1, "failed", passed=13, failed=1)

    def test_required_checks_with_incomplete_evidence(self):
        cases = (
            ("QF_BV-typed", record(2), "unsupported"),
            ("QF_BV-z3_xor", record("unavailable"), "unavailable"),
            ("QF_BV-bitwuzla_parse", record(-9), "crashed"),
            ("QF_BV-cvc5_xor", record("timeout"), "timed_out"),
            ("QF_BV-z3_xor", record(stdout="unknown\n"), "inconclusive"),
            ("QF_BV-cvc5_parse", record(stdout='(error "parse error")\n'),
             "inconclusive"),
            ("oracle-cvc5-different", record("timeout"), "timed_out"),
        )
        for tag, observed, disposition in cases:
            with self.subTest(tag=tag, observed=observed):
                status, report = self.run_case({tag: observed})
                self.assert_verdict(status, report, 2, "incomplete", passed=13,
                                    **{disposition: 1})

    def test_recovery_status_is_preserved_and_comparisons_are_inconclusive(self):
        summary = ("QF_BV commands=3 assertions=1 functions=0 definitions=0 "
                   "datatype_groups=0\n")
        for observed, counts in (
            (record(3, stderr="Unmapped comparison Kind"), {"inconclusive": 4}),
            (record(stderr=summary), {"inconclusive": 4}),
            (record(-9), {"crashed": 1, "inconclusive": 3}),
            (record("timeout"), {"timed_out": 1, "inconclusive": 3}),
        ):
            with self.subTest(observed=observed):
                status, report = self.run_case({"QF_BV-recovered": observed})
                self.assert_verdict(status, report, 2, "incomplete", passed=10, **counts)

    def test_control_recovery_failure_still_writes_report(self):
        status, report = self.run_case({"oracle-clean": record(3)})
        self.assert_verdict(status, report, 2, "incomplete", passed=7, inconclusive=7)

    def test_recovery_requires_one_xor_pair_per_assertion(self):
        summary = ("QF_BV commands=3 assertions=1 functions=0 definitions=0 "
                   "datatype_groups=0\n")
        for malformed in (
            "(assert false)",
            "(assert (or false (xor true)))",
            "(assert (or false (xor true true true)))",
            "(assert (or false (xor true true) (xor true true)))",
            "(assert (or false (and (xor true true) true)))",
            "(assert false)\n; (assert (or false (xor true true)))",
            "(declare-const |(assert (or false (xor true true)))| Bool)\n"
            "(assert false)",
        ):
            with self.subTest(malformed=malformed):
                status, report = self.run_case({"QF_BV-recovered": record(
                    stdout=f"(set-logic QF_BV)\n{malformed}\n(check-sat)\n",
                    stderr=summary,
                )})
                self.assert_verdict(status, report, 2, "incomplete",
                                    passed=10, inconclusive=4)
                case = report["cases"]["QF_BV"]
                self.assertEqual(case["recovery"]["status"], "inconclusive")
                self.assertEqual(case["z3_xor"]["status"], "skipped_recovery")

    def test_comparison_shape_accepts_zero_and_multiple_assertions(self):
        for count, assertion in (
            (0, "(assert false)"),
            (2, "(assert (or false (xor (= |(| \"a)\") true)\n"
                "; ignored (xor true true)\n(xor (not true) false)))"),
        ):
            with self.subTest(count=count):
                summary = runner.probe_summary("QF_BV", (count + 2, count, 0, 0, 0))
                observed = record(stdout=assertion + "\n(check-sat)\n", stderr=summary)
                self.assertEqual(runner.classify_probe(
                    observed, summary, assertions=count
                ), "available")

    def test_xor_mismatch_takes_precedence_over_incomplete_checks(self):
        status, report = self.run_case({
            "QF_BV-z3_xor": record(stdout="sat\n"),
            "QF_BV-cvc5_xor": record("timeout"),
            "oracle-yices-different": record(stdout="unsat\n"),
        })
        self.assert_verdict(status, report, 1, "failed", passed=11, failed=2, timed_out=1)


if __name__ == "__main__":
    unittest.main()

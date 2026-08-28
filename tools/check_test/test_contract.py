from __future__ import annotations

import json
import subprocess
import sys
import unittest
from unittest.mock import patch

from support import TOOLS, gate, isolated_project

from checks.basic import architecture_check, complexity_check, format_check
from checks.model import Check
from checks.profile import load as load_profile


class PublicContractTests(unittest.TestCase):
    def test_check_py_lists_the_target_schema(self) -> None:
        with isolated_project() as project:
            result = subprocess.run(
                [sys.executable, str(TOOLS / "check.py"), "--project-root", str(project), "--list"],
                text=True,
                encoding="utf-8",
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(json.loads(result.stdout)["schema"], "check-test/v1")

    def test_check_py_baseline_architecture_flow_passes(self) -> None:
        with isolated_project() as project:
            result = subprocess.run(
                [sys.executable, str(TOOLS / "check.py"), "--project-root", str(project), "inspect", "architecture"],
                text=True,
                encoding="utf-8",
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
            self.assertEqual(result.returncode, 0, result.stderr)
            report = json.loads(result.stdout)
            self.assertTrue(report["passed"])
            self.assertEqual(report["checks"][0]["status"], "pass")
            self.assertTrue((project / report["report"]).is_file())

    def test_profile_is_owned_by_the_target_project(self) -> None:
        with isolated_project():
            profile = load_profile()
            self.assertEqual(profile.report_schema, "check-test/v1")
            self.assertEqual(profile.preset("fast"), "fixture-fast")
            self.assertEqual(profile.build_directory("fixture-fast").name, "fast")
            self.assertEqual(profile.source_roots, ("library", "spec"))

    def test_architecture_rejects_forbidden_include(self) -> None:
        with isolated_project() as project:
            source = project / "library" / "answer.cpp"
            source.write_text('#include "spec/private.hpp"\nint answer() { return 42; }\n', encoding="utf-8")
            value = gate()
            self.assertFalse(architecture_check(value))
            check = value.report()["checks"][0]
            self.assertEqual(check["status"], "fail")
            self.assertEqual(check["detail"]["count"], 1)

    def test_architecture_rejects_missing_and_wrong_schema_rules(self) -> None:
        with isolated_project() as project:
            rules = project / "policy" / "architecture.json"
            rules.unlink()
            missing = gate()
            self.assertFalse(architecture_check(missing))
            self.assertEqual(missing.checks[0].summary, "architecture rules are missing")
            rules.write_text('{"schema":"wrong"}\n', encoding="utf-8")
            wrong = gate()
            self.assertFalse(architecture_check(wrong))
            self.assertEqual(wrong.checks[0].summary, "unsupported architecture rules schema")

    def test_format_rejects_empty_source_set(self) -> None:
        with isolated_project(), patch("checks.basic.source_files", return_value=[]):
            value = gate()
            self.assertFalse(format_check(value))
            self.assertEqual(value.checks[0].summary, "no source files found")

    def test_format_check_reports_only_files_requiring_formatting(self) -> None:
        violation = "warning: code should be clang-formatted [-Wclang-format-violations]"

        def format_result(command, **_kwargs):
            return subprocess.CompletedProcess(
                command,
                1 if command[-1].endswith("answer.cpp") else 0,
                violation if command[-1].endswith("answer.cpp") else "",
            )

        with isolated_project(), patch("checks.basic.shutil.which", return_value="clang-format"), patch(
            "checks.basic.command_output", side_effect=format_result
        ):
            value = gate()
            self.assertFalse(format_check(value))

        check = value.report()["checks"][0]
        self.assertEqual(check["summary"], "source files require formatting")
        self.assertEqual(check["detail"], {"files": ["library/answer.cpp"], "count": 1})
        self.assertNotIn("command", check["detail"])
        self.assertNotIn("log_tail", check["detail"])

    def test_complexity_enforces_ccn_length_and_parameter_limits(self) -> None:
        with isolated_project(), patch.object(type(gate()), "command", return_value=True) as command:
            self.assertTrue(complexity_check(gate()))
            arguments = command.call_args.args[2]
            self.assertEqual(arguments[arguments.index("-C") + 1], "10")
            self.assertEqual(arguments[arguments.index("-L") + 1], "80")
            self.assertEqual(arguments[arguments.index("-a") + 1], "5")

    def test_complexity_empty_increment_is_a_valid_noop(self) -> None:
        value = gate()
        self.assertTrue(complexity_check(value, []))
        self.assertEqual(value.checks[0].status, "pass")
        self.assertEqual(value.checks[0].detail["scanned_count"], 0)


class ResultModelTests(unittest.TestCase):
    def test_command_pass_failure_skip_and_crash_are_distinct(self) -> None:
        value = gate()
        self.assertTrue(value.command("pass", 10, [sys.executable, "-c", "pass"]))
        self.assertFalse(value.command("fail", 10, [sys.executable, "-c", "raise SystemExit(7)"]))
        self.assertTrue(value.command("skip", 10, ["tool-that-does-not-exist-check-test"]))
        self.assertFalse(value.custom("crash", 10, lambda _check: (_ for _ in ()).throw(RuntimeError("boom"))))
        self.assertEqual([item.status for item in value.checks], ["pass", "fail", "skipped", "fail"])
        self.assertEqual(value.checks[1].detail["returncode"], 7)
        self.assertEqual(value.checks[3].summary, "checker crashed")

    def test_report_fails_on_blocked_and_excludes_skips_from_maximum(self) -> None:
        value = gate()
        value.custom("pass", 10, lambda check: check.finish(True, "passed"))
        value.skipped("optional", 20, "unavailable")
        value.blocked("downstream", 30, "upstream failed")
        report = value.report()
        self.assertFalse(report["passed"])
        self.assertEqual(report["score"], {"value": 10, "maximum": 40, "percentage": 25.0})

    def test_check_finish_records_structured_detail(self) -> None:
        check = Check("x", 10)
        self.assertFalse(check.finish(False, "bad", finding="x.cpp"))
        self.assertEqual(check.as_json()["detail"], {"finding": "x.cpp"})


if __name__ == "__main__":
    unittest.main()

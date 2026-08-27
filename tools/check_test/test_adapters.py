from __future__ import annotations

import json
import subprocess
import unittest
from pathlib import Path
from unittest.mock import patch

from support import gate, isolated_project

from checks.analyzers import _run_analyzer, parse_cppcheck, parse_iwyu, parse_tidy
from checks.coverage import coverage
from checks.mutation import mutation_test


def completed(returncode: int = 0, stdout: str = "") -> subprocess.CompletedProcess[str]:
    return subprocess.CompletedProcess(["fake"], returncode, stdout)


class AnalyzerInterceptionTests(unittest.TestCase):
    def test_all_analyzer_formats_are_parsed(self) -> None:
        self.assertEqual(len(parse_cppcheck(["a.cpp\t2\t3\twarning\trule\tmessage"])), 1)
        self.assertEqual(len(parse_tidy(["a.cpp:2:3: warning: message [rule]"])), 1)
        self.assertEqual(len(parse_iwyu(["a.cpp should add these lines:", "#include <vector>", "---"])), 1)

    def test_analyzer_rejects_empty_database_and_unparsed_tool_failure(self) -> None:
        check_gate = gate()
        with patch("checks.analyzers.missing_analyzer_tool", return_value=None):
            self.assertFalse(check_gate.custom("cppcheck", 10, lambda check: _run_analyzer(check, "cppcheck", [], "fixture-fast", False)))
        self.assertEqual(check_gate.checks[0].summary, "compilation database contains no project units")

        with isolated_project() as project:
            unit = project / "library" / "answer.cpp"
            failed = gate()
            with patch("checks.analyzers.missing_analyzer_tool", return_value=None), patch("checks.analyzers.command_output", return_value=completed(2, "opaque error")):
                failed.custom("cppcheck", 10, lambda check: _run_analyzer(check, "cppcheck", [unit], "fixture-fast", False))
            self.assertEqual(failed.checks[0].status, "fail")
            self.assertEqual(failed.checks[0].detail["returncodes"], [2])


class CoverageInterceptionTests(unittest.TestCase):
    def _prepared(self, project: Path) -> tuple[Path, Path]:
        build = project / "out" / "fast"
        build.mkdir(parents=True)
        (build / "CMakeCache.txt").write_text("FIXTURE_COVERAGE:BOOL=ON\n", encoding="utf-8")
        (build / "run.profraw").write_bytes(b"profile")
        binary = build / "fixture_test"
        binary.write_bytes(b"binary")
        return build, binary

    def test_missing_tools_disabled_option_profiles_and_binaries_are_rejected(self) -> None:
        with isolated_project() as project:
            value = gate()
            with patch("checks.coverage._llvm_tool", return_value=None):
                self.assertTrue(coverage(value, "fixture-fast"))
            self.assertEqual(value.checks[0].status, "skipped")

            build = project / "out" / "fast"
            build.mkdir(parents=True)
            with patch("checks.coverage._llvm_tool", side_effect=lambda name: name):
                disabled = gate()
                self.assertFalse(coverage(disabled, "fixture-fast"))
                self.assertIn("FIXTURE_COVERAGE=ON", disabled.checks[0].summary)

            (build / "CMakeCache.txt").write_text("FIXTURE_COVERAGE:BOOL=ON\n", encoding="utf-8")
            with patch("checks.coverage._llvm_tool", side_effect=lambda name: name):
                no_profiles = gate()
                self.assertFalse(coverage(no_profiles, "fixture-fast"))
                self.assertEqual(no_profiles.checks[0].summary, "no coverage profiles were produced")

            (build / "run.profraw").write_bytes(b"profile")
            with patch("checks.coverage._llvm_tool", side_effect=lambda name: name), patch("checks.coverage._instrumented_binaries", return_value=[]):
                no_binaries = gate()
                self.assertFalse(coverage(no_binaries, "fixture-fast"))
                self.assertEqual(no_binaries.checks[0].summary, "no instrumented test executables were found")

    def test_merge_export_parse_threshold_and_baseline_paths(self) -> None:
        with isolated_project() as project:
            _build, binary = self._prepared(project)
            with patch("checks.coverage._llvm_tool", side_effect=lambda name: name), patch("checks.coverage._instrumented_binaries", return_value=[binary]), patch("checks.coverage.command_output", return_value=completed(3, "merge failed")):
                merge = gate()
                self.assertFalse(coverage(merge, "fixture-fast"))
                self.assertEqual(merge.checks[0].summary, "coverage profile merge failed")

        for percent, expected in ((89.9, False), (95.0, True)):
            with self.subTest(percent=percent), isolated_project() as project:
                _build, binary = self._prepared(project)
                export = json.dumps({"data": [{"totals": {"lines": {"percent": percent}, "regions": {"percent": 94.0}}}]})
                with patch("checks.coverage._llvm_tool", side_effect=lambda name: name), patch("checks.coverage._instrumented_binaries", return_value=[binary]), patch("checks.coverage.command_output", side_effect=[completed(), completed(0, export)]):
                    value = gate()
                    self.assertEqual(coverage(value, "fixture-fast"), expected)
                    self.assertEqual(value.checks[0].status, "pass" if expected else "fail")

    def test_unparseable_coverage_export_is_rejected(self) -> None:
        with isolated_project() as project:
            _build, binary = self._prepared(project)
            with patch("checks.coverage._llvm_tool", side_effect=lambda name: name), patch("checks.coverage._instrumented_binaries", return_value=[binary]), patch("checks.coverage.command_output", side_effect=[completed(), completed(0, "not-json")]):
                value = gate()
                self.assertFalse(coverage(value, "fixture-fast"))
                self.assertEqual(value.checks[0].summary, "coverage export could not be parsed")

    def test_failed_coverage_export_is_rejected(self) -> None:
        with isolated_project() as project:
            _build, binary = self._prepared(project)
            with patch("checks.coverage._llvm_tool", side_effect=lambda name: name), patch("checks.coverage._instrumented_binaries", return_value=[binary]), patch("checks.coverage.command_output", side_effect=[completed(), completed(4, "export failed")]):
                value = gate()
                self.assertFalse(coverage(value, "fixture-fast"))
                self.assertEqual(value.checks[0].summary, "coverage export failed")


class MutationInterceptionTests(unittest.TestCase):
    def test_missing_tools_skip_and_missing_executable_fail(self) -> None:
        with isolated_project():
            with patch("checks.mutation.find_mull_tools", return_value=None):
                skipped = gate()
                self.assertTrue(mutation_test(skipped, "fixture-mutation"))
                self.assertEqual(skipped.checks[0].status, "skipped")
            with patch("checks.mutation._test_executable", return_value=None):
                missing = gate()
                self.assertFalse(mutation_test(missing, "fixture-mutation", runner="mull"))
                self.assertEqual(missing.checks[0].summary, "no instrumented test executable was found")

    def test_no_mutants_low_score_and_baseline_are_distinguished(self) -> None:
        cases = (("No mutants found", False, "no mutants found"), ("Mutation score: 80%", False, "mutation score is below threshold"), ("Mutation score: 95%", True, "passed"))
        for output, expected, summary in cases:
            with self.subTest(output=output), isolated_project() as project:
                executable = project / "out" / "mutation" / "fixture_test"
                executable.parent.mkdir(parents=True)
                executable.write_bytes(b"binary")
                with patch("checks.mutation._test_executable", return_value=executable), patch("checks.mutation.command_output", return_value=completed(0, output)):
                    value = gate()
                    self.assertEqual(mutation_test(value, "fixture-mutation", runner="mull"), expected)
                    self.assertEqual(value.checks[0].summary, summary)


if __name__ == "__main__":
    unittest.main()

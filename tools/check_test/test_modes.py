from __future__ import annotations

import unittest
from unittest.mock import patch

from support import gate, isolated_project, report_by_id

from checks.modes import fast, full
from checks.modes.hardening import run as hardening


def pass_check(value, check_id: str, maximum: int) -> bool:
    return value.custom(check_id, maximum, lambda check: check.finish(True, "passed"))


class ModeInterceptionTests(unittest.TestCase):
    def test_fast_configure_build_and_test_failures_block_dependants(self) -> None:
        with isolated_project(), patch("checks.modes.fast.architecture_check"), patch("checks.modes.fast.configure", return_value=False):
            value = gate("fast")
            fast(value)
            checks = report_by_id(value)
            self.assertEqual(checks["build"]["status"], "blocked")
            self.assertEqual(checks["coverage.fast"]["status"], "blocked")

        with isolated_project(), patch("checks.modes.fast.architecture_check"), patch("checks.modes.fast.configure", return_value=True), patch("checks.modes.fast.incremental_build", return_value=(False, ())):
            value = gate("fast")
            fast(value)
            self.assertEqual(report_by_id(value)["tests.fast"]["status"], "blocked")

        with isolated_project(), patch("checks.modes.fast.architecture_check"), patch("checks.modes.fast.configure", return_value=True), patch("checks.modes.fast.incremental_build", return_value=(True, ())), patch("checks.modes.fast.complexity_check"), patch("checks.modes.fast.run_analyzers"), patch("checks.modes.fast.clean_coverage_profiles"), patch("checks.modes.fast.tests", return_value=False):
            value = gate("fast")
            fast(value)
            self.assertEqual(report_by_id(value)["coverage.fast"]["status"], "blocked")

    def test_fast_baseline_reaches_every_checkpoint(self) -> None:
        with isolated_project(), patch("checks.modes.fast.architecture_check", side_effect=lambda value: pass_check(value, "architecture", 10)), patch("checks.modes.fast.configure", side_effect=lambda value, _preset: pass_check(value, "configure", 10)), patch("checks.modes.fast.incremental_build", side_effect=lambda value, _preset: (pass_check(value, "build", 20), ())), patch("checks.modes.fast.complexity_check", side_effect=lambda value, _files: pass_check(value, "complexity", 10)), patch("checks.modes.fast.run_analyzers", side_effect=lambda value, *_args, **_kwargs: (pass_check(value, "cppcheck", 10), pass_check(value, "clang-tidy", 10))), patch("checks.modes.fast.clean_coverage_profiles"), patch("checks.modes.fast.tests", side_effect=lambda value, _preset, check_id: pass_check(value, check_id, 15)), patch("checks.modes.fast.coverage", side_effect=lambda value, _preset, check_id: pass_check(value, check_id, 10)):
            value = gate("fast")
            fast(value)
            self.assertTrue(value.report()["passed"])
            self.assertEqual(set(report_by_id(value)), {"architecture", "configure", "build", "complexity", "cppcheck", "clang-tidy", "tests.fast", "coverage.fast"})

    def test_full_source_configure_build_and_test_intercepts(self) -> None:
        with isolated_project(), patch("checks.modes.full.source_checks"), patch("checks.modes.full.configure", return_value=False):
            value = gate("full")
            full(value)
            checks = report_by_id(value)
            for check_id in ("build", "tests.full", "coverage.full", "cppcheck", "clang-tidy"):
                self.assertEqual(checks[check_id]["status"], "blocked")

        with isolated_project(), patch("checks.modes.full.source_checks"), patch("checks.modes.full.configure", return_value=True), patch("checks.modes.full.build", return_value=True), patch("checks.modes.full.clean_coverage_profiles"), patch("checks.modes.full.tests", return_value=False), patch("checks.modes.full.run_analyzers"):
            value = gate("full")
            full(value)
            self.assertEqual(report_by_id(value)["coverage.full"]["status"], "blocked")

    def test_full_baseline_reaches_every_checkpoint(self) -> None:
        def source_checks(value, **_kwargs):
            for check_id in ("format", "complexity", "architecture"):
                pass_check(value, check_id, 10)

        with isolated_project(), patch("checks.modes.full.source_checks", side_effect=source_checks), patch("checks.modes.full.configure", side_effect=lambda value, _preset: pass_check(value, "configure", 10)), patch("checks.modes.full.build", side_effect=lambda value, _preset: pass_check(value, "build", 20)), patch("checks.modes.full.clean_coverage_profiles"), patch("checks.modes.full.tests", side_effect=lambda value, _preset, check_id: pass_check(value, check_id, 15)), patch("checks.modes.full.coverage", side_effect=lambda value, _preset, check_id: pass_check(value, check_id, 10)), patch("checks.modes.full.run_analyzers", side_effect=lambda value, *_args, **_kwargs: tuple(pass_check(value, check_id, 10) for check_id in ("cppcheck", "clang-tidy"))):
            value = gate("full")
            full(value)
            self.assertTrue(value.report()["passed"])
            self.assertEqual(set(report_by_id(value)), {"format", "complexity", "architecture", "configure", "build", "tests.full", "coverage.full", "cppcheck", "clang-tidy"})

    def test_hardening_sanitizer_failure_and_platform_skip_are_reported(self) -> None:
        with isolated_project(), patch("checks.modes.hardening.configure", return_value=False), patch("checks.modes.hardening.platform.system", return_value="Windows"):
            value = gate("hardening")
            hardening(value)
            checks = report_by_id(value)
            self.assertEqual(checks["build"]["status"], "blocked")
            self.assertEqual(checks["tests.asan-ubsan"]["status"], "blocked")
            self.assertEqual(checks["mull"]["status"], "skipped")

    def test_hardening_baseline_reaches_sanitizer_and_mutation_checkpoints(self) -> None:
        def command(value, check_id, maximum, _command):
            return pass_check(value, check_id, maximum)

        with isolated_project(), patch("checks.modes.hardening.platform.system", return_value="Linux"), patch("checks.modes.hardening.configure", side_effect=lambda value, _preset: pass_check(value, "configure", 10)), patch("checks.modes.hardening.build", side_effect=lambda value, _preset: pass_check(value, "build", 20)), patch("checks.modes.hardening.tests", side_effect=lambda value, _preset, check_id: pass_check(value, check_id, 15)), patch("checks.modes.hardening.find_mull_tools", return_value=("mull", "frontend")), patch("checks.modes.hardening.mutation_test", side_effect=lambda value, _preset, _runner: pass_check(value, "mull", 15)), patch.object(type(gate()), "command", command):
            value = gate("hardening")
            hardening(value)
            self.assertTrue(value.report()["passed"])
            self.assertEqual(set(report_by_id(value)), {"configure", "build", "tests.asan-ubsan", "configure.mull", "build.mull", "mull"})


if __name__ == "__main__":
    unittest.main()

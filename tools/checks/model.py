"""Result model and command runner shared by all checks."""

from __future__ import annotations

import shutil
import subprocess
import time
from collections.abc import Mapping
from dataclasses import dataclass, field
from typing import Callable, Sequence

from .console import show_command, show_error, show_output, show_result, show_skip
from .project import LOG_LINE_LIMIT, root


@dataclass
class Check:
    id: str
    maximum: int
    status: str = "pass"
    summary: str = ""
    detail: dict[str, object] = field(default_factory=dict)
    started: float = field(default_factory=time.monotonic)
    elapsed_ms: int = 0

    def finish(self, passed: bool, summary: str, **detail: object) -> bool:
        self.status = "pass" if passed else "fail"
        self.summary = summary
        self.detail = detail
        self.elapsed_ms = round((time.monotonic() - self.started) * 1000)
        return passed

    def skip(self, summary: str, **detail: object) -> bool:
        self.status = "skipped"
        self.summary = summary
        self.detail = detail
        self.elapsed_ms = round((time.monotonic() - self.started) * 1000)
        return True

    def as_json(self) -> dict[str, object]:
        result: dict[str, object] = {
            "id": self.id,
            "status": self.status,
            "score": self.maximum if self.status == "pass" else 0,
            "maximum": self.maximum,
            "summary": self.summary,
            "elapsed_ms": self.elapsed_ms,
        }
        if self.detail:
            result["detail"] = self.detail
        return result


Action = Callable[[Check], bool]
CommandDetail = Callable[[subprocess.CompletedProcess[str]], dict[str, object]]


class Gate:
    def __init__(
        self,
        mode: str,
        *,
        is_gate: bool,
        report_schema: str,
        verbose: bool = False,
        options: Mapping[str, bool] | None = None,
    ) -> None:
        self.mode = mode
        self.is_gate = is_gate
        self.report_schema = report_schema
        self.verbose = verbose
        self.options = dict(options or {})
        self.checks: list[Check] = []

    def command(
        self,
        check_id: str,
        maximum: int,
        command: Sequence[str],
        detail: CommandDetail | None = None,
    ) -> bool:
        def run(check: Check) -> bool:
            if shutil.which(command[0]) is None:
                reason = f"required system tool '{command[0]}' was not found on PATH; skipped"
                show_skip(check_id, reason)
                return check.skip(reason, tool=command[0])
            if self.verbose:
                show_command(check_id, command)
            try:
                result = subprocess.run(
                    command,
                    cwd=root(),
                    text=True,
                    encoding="utf-8",
                    errors="replace",
                    stdout=subprocess.PIPE,
                    stderr=subprocess.STDOUT,
                    check=False,
                )
            except OSError as error:
                show_error(check_id, f"command could not start: {error}")
                return check.finish(False, "command could not start", error=str(error))
            if self.verbose:
                show_output(check_id, result.stdout)
                show_result(check_id, result.returncode)
            command_detail = detail(result) if detail else {}
            if result.returncode == 0:
                return check.finish(True, "passed", **command_detail)
            return check.finish(
                False,
                "command failed",
                returncode=result.returncode,
                command=list(command),
                log_tail=result.stdout.strip().splitlines()[-LOG_LINE_LIMIT:],
                **command_detail,
            )

        return self.custom(check_id, maximum, run)

    def custom(self, check_id: str, maximum: int, action: Action) -> bool:
        check = Check(check_id, maximum)
        self.checks.append(check)
        try:
            return action(check)
        except Exception as error:  # Keep one broken diagnostic from hiding the report.
            show_error(check_id, f"checker crashed: {error}")
            return check.finish(False, "checker crashed", error=str(error))

    def blocked(self, check_id: str, maximum: int, reason: str) -> None:
        check = Check(check_id, maximum, status="blocked", summary="not run")
        check.detail = {"reason": reason}
        self.checks.append(check)

    def skipped(self, check_id: str, maximum: int, reason: str, **detail: object) -> None:
        show_skip(check_id, reason)
        check = Check(check_id, maximum)
        check.skip(reason, **detail)
        self.checks.append(check)

    def skip_reason(self, check_id: str) -> str | None:
        for check in reversed(self.checks):
            if check.id == check_id and check.status == "skipped":
                return check.summary
        return None

    def report(self) -> dict[str, object]:
        scored_checks = [item for item in self.checks if item.status != "skipped"]
        maximum = sum(item.maximum for item in scored_checks)
        score = sum(item.maximum for item in self.checks if item.status == "pass")
        return {
            "schema": self.report_schema,
            "mode": self.mode,
            "gate": self.is_gate,
            "passed": bool(self.checks)
            and all(item.status in ("pass", "skipped") for item in self.checks),
            "score": {
                "value": score,
                "maximum": maximum,
                "percentage": round(100 * score / maximum if maximum else 0, 1),
            },
            "checks": [item.as_json() for item in self.checks],
        }

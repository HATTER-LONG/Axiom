"""Mull mutation-testing adapter."""

from __future__ import annotations

import json
import os
import re
import shutil
from pathlib import Path

from .console import show_skip
from .model import Check, Gate
from .project import FINDING_LIMIT, LOG_LINE_LIMIT, build_directory, command_output, relative

MULL_RUNNER = re.compile(r"^mull-runner(?:-(\d+))?$")
MUTATION_SCORE = re.compile(r"Mutation score:\s*([0-9]+(?:\.[0-9]+)?)%")
MUTANT_COUNTS = re.compile(r"(?:Killed|Survived) mutants \((\d+)/(\d+)\)")
SURVIVOR = re.compile(
    r"^(.*?):(\d+):(\d+):\s+warning:\s+Survived:\s+(.*?)\s+\[([^]]+)\]$"
)
MUTATION_SCORE_THRESHOLD = 90


def find_mull_tools() -> tuple[str, str] | None:
    """Find a matching runner/frontend pair, including LLVM-version-suffixed installs."""
    direct_runner = shutil.which("mull-runner")
    direct_frontend = shutil.which("mull-ir-frontend")
    if direct_runner and direct_frontend:
        return direct_runner, direct_frontend

    candidates: list[tuple[int, str, str]] = []
    for directory in os.get_exec_path():
        try:
            entries = list(Path(directory).iterdir())
        except OSError:
            continue
        for entry in entries:
            match = MULL_RUNNER.match(entry.name)
            if match and match.group(1) and os.access(entry, os.X_OK) and entry.is_file():
                version = match.group(1)
                frontend = shutil.which(f"mull-ir-frontend-{version}")
                if frontend:
                    candidates.append((int(version), str(entry), frontend))
    selected = max(candidates, default=None)
    return (selected[1], selected[2]) if selected else None


def mutation_test(
    gate: Gate, preset: str, runner: str | None = None, check_id: str = "mull"
) -> bool:
    return gate.custom(check_id, 15, lambda check: _run_mull(check, gate, preset, runner))


def _run_mull(check: Check, gate: Gate, preset: str, runner: str | None) -> bool:
    tools = find_mull_tools() if runner is None else None
    runner = runner or (tools[0] if tools else None)
    if runner is None:
        reason = "matching mull-runner and mull-ir-frontend are unavailable; skipped"
        show_skip(check.id, reason)
        return check.skip(
            reason,
            tool="matching mull-runner and mull-ir-frontend",
        )

    executable = build_directory(preset) / "tests" / "axiom_core_test"
    if os.name == "nt":
        executable = executable.with_suffix(".exe")
    if not executable.is_file():
        return check.finish(
            False, "test executable is unavailable", executable=relative(executable)
        )

    report_directory = build_directory(preset) / "mull"
    report_directory.mkdir(parents=True, exist_ok=True)
    elements_report = report_directory / "mutations.json"
    elements_report.unlink(missing_ok=True)
    command = [
        runner,
        "--strict",
        "--no-output",
        "--mutation-score-threshold",
        str(MUTATION_SCORE_THRESHOLD),
        "--reporters",
        "IDE",
        "--reporters",
        "Elements",
        "--report-dir",
        str(report_directory),
        "--report-name",
        "mutations",
        str(executable),
    ]
    result = command_output(command, verbose=gate.verbose, check_id=check.id)
    lines = result.stdout.splitlines()
    score_matches = [MUTATION_SCORE.search(line) for line in lines]
    score_match = next((match for match in reversed(score_matches) if match), None)
    counts = [MUTANT_COUNTS.search(line) for line in lines]
    count_match = next((match for match in reversed(counts) if match), None)
    survivors: list[dict[str, object]] = []
    for line in lines:
        match = SURVIVOR.match(line)
        if match:
            file, line_no, column, message, mutator = match.groups()
            survivors.append(
                {
                    "file": relative(Path(file)),
                    "line": int(line_no),
                    "column": int(column),
                    "mutator": mutator,
                    "message": message,
                }
            )

    statuses: dict[str, int] = {}
    elements_score: float | None = None
    mutant_count = 0
    try:
        elements = json.loads(elements_report.read_text(encoding="utf-8"))
        elements_score = float(elements["mutationScore"])
        survivors.clear()
        for file, file_result in elements.get("files", {}).items():
            for mutant in file_result.get("mutants", []):
                mutant_count += 1
                status = str(mutant.get("status", "Unknown"))
                statuses[status] = statuses.get(status, 0) + 1
                if status == "Survived":
                    location = mutant.get("location", {}).get("start", {})
                    survivors.append(
                        {
                            "file": relative(Path(file)),
                            "line": int(location.get("line", 0)),
                            "column": int(location.get("column", 0)),
                            "mutator": mutant.get("mutatorName", "unknown"),
                            "message": (
                                f"survived with replacement {mutant.get('replacement', '')!r}"
                            ),
                        }
                    )
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError):
        pass

    detail: dict[str, object] = {
        "executable": relative(executable),
        "report_directory": relative(report_directory),
        "elements_report": relative(elements_report),
        "threshold": MUTATION_SCORE_THRESHOLD,
    }
    score = elements_score if elements_score is not None else (
        float(score_match.group(1)) if score_match else None
    )
    if score is not None:
        detail["mutation_score"] = score
    if mutant_count:
        detail["reported_mutants"] = mutant_count
        detail["statuses"] = statuses
    elif count_match:
        detail["reported_mutants"] = int(count_match.group(2))
    if survivors:
        detail["survivor_count"] = len(survivors)
        detail["survivors"] = survivors[:FINDING_LIMIT]
        detail["suppressed_survivor_count"] = max(0, len(survivors) - FINDING_LIMIT)

    no_mutants = any("No mutants found" in line for line in lines)
    passed = (
        result.returncode == 0
        and score is not None
        and score >= MUTATION_SCORE_THRESHOLD
        and not no_mutants
    )
    if not passed:
        detail["returncode"] = result.returncode
        detail["command"] = command
        detail["log_tail"] = lines[-LOG_LINE_LIMIT:]
    if no_mutants:
        summary = "no mutants found"
    elif score is not None and score < MUTATION_SCORE_THRESHOLD:
        summary = "mutation score is below threshold"
    elif score is None:
        summary = "mutation result is unavailable"
    else:
        summary = "passed"
    return check.finish(passed, summary, **detail)

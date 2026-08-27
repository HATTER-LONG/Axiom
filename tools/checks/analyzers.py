"""Adapters for compilation-database based static analyzers."""

from __future__ import annotations

import re
import shutil
import sys
from pathlib import Path
from typing import Iterable, Sequence

from .console import show_skip
from .model import Check, Gate
from .project import (
    FINDING_LIMIT,
    LOG_LINE_LIMIT,
    build_directory,
    command_output,
    relative,
    root,
)

TIDY_DIAGNOSTIC = re.compile(r"^(.*?):(\d+):(\d+):\s+(warning|error):\s+(.*?)\s+\[([^]]+)\]$")
IWYU_SECTION = re.compile(r"^(.*?) should (add|remove) these lines:$")


def iwyu_driver() -> str | None:
    direct = shutil.which("iwyu_tool.py")
    if direct:
        return direct
    executable = shutil.which("include-what-you-use")
    if executable:
        sibling = Path(executable).with_name("iwyu_tool.py")
        if sibling.is_file():
            return str(sibling)
    return None


def iwyu_mapping_file() -> Path | None:
    """Return the optional repository-owned mapping used by IWYU."""
    path = root() / "quality" / "iwyu.imp"
    return path if path.is_file() else None


def missing_analyzer_tool(name: str) -> str | None:
    if name == "iwyu":
        missing: list[str] = []
        if shutil.which("include-what-you-use") is None:
            missing.append("include-what-you-use")
        if iwyu_driver() is None:
            missing.append("iwyu_tool.py")
        return ", ".join(missing) if missing else None
    return name if shutil.which(name) is None else None


def parse_cppcheck(lines: Iterable[str]) -> list[dict[str, object]]:
    findings: list[dict[str, object]] = []
    for line in lines:
        parts = line.split("\t", 5)
        if len(parts) != 6:
            continue
        file, line_no, column, severity, rule, message = parts
        if line_no.isdigit():
            findings.append(
                {
                    "file": relative(Path(file)),
                    "line": int(line_no),
                    "column": int(column) if column.isdigit() else 0,
                    "severity": severity,
                    "rule": rule,
                    "message": message,
                }
            )
    return findings


def parse_tidy(lines: Iterable[str]) -> list[dict[str, object]]:
    findings: list[dict[str, object]] = []
    for line in lines:
        match = TIDY_DIAGNOSTIC.match(line)
        if match:
            file, line_no, column, severity, message, rule = match.groups()
            findings.append(
                {
                    "file": relative(Path(file)),
                    "line": int(line_no),
                    "column": int(column),
                    "severity": severity,
                    "rule": rule,
                    "message": message,
                }
            )
    return findings


def parse_iwyu(lines: Iterable[str]) -> list[dict[str, object]]:
    findings: list[dict[str, object]] = []
    section: tuple[str, str] | None = None
    for line in lines:
        match = IWYU_SECTION.match(line)
        if match:
            section = match.groups()
            continue
        if not line:
            continue
        if line.startswith("The full include-list for ") or line == "---":
            section = None
            continue
        if section:
            file, action = section
            findings.append(
                {
                    "file": relative(Path(file)),
                    "line": 0,
                    "column": 0,
                    "severity": "warning",
                    "rule": f"iwyu.{action}",
                    "message": line,
                }
            )
    return findings


def analyzer(gate: Gate, name: str, units: Sequence[Path], preset: str) -> bool:
    return gate.custom(
        name,
        10,
        lambda check: _run_analyzer(check, name, units, preset, gate.verbose),
    )


def _run_analyzer(
    check: Check,
    name: str,
    units: Sequence[Path],
    preset: str,
    verbose: bool,
) -> bool:
    missing = missing_analyzer_tool(name)
    if missing:
        reason = f"required system tool was not found on PATH: {missing}; skipped"
        show_skip(check.id, reason)
        return check.skip(reason, tool=missing)
    if not units:
        return check.finish(False, "compilation database contains no project units")

    database = build_directory(preset) / "compile_commands.json"
    all_output: list[str] = []
    exit_codes: list[int] = []
    for unit in units:
        if name == "cppcheck":
            command = [
                "cppcheck",
                f"--project={database}",
                f"--file-filter={unit}",
                "--enable=warning,style,performance,portability",
                "--error-exitcode=1",
                "--template={file}\t{line}\t{column}\t{severity}\t{id}\t{message}",
            ]
        elif name == "clang-tidy":
            command = [
                "clang-tidy",
                "--quiet",
                f"-p={database.parent}",
                "--warnings-as-errors=*",
                str(unit),
            ]
        else:
            command = [
                sys.executable,
                iwyu_driver() or "iwyu_tool.py",
                "-p",
                str(database.parent),
                str(unit),
                "--",
                "-Xiwyu",
                "--error=1",
            ]
            mapping = iwyu_mapping_file()
            if mapping is not None:
                command.extend(("-Xiwyu", f"--mapping_file={mapping}"))
        result = command_output(command, verbose=verbose, check_id=f"{name}:{relative(unit)}")
        all_output.extend(result.stdout.splitlines())
        exit_codes.append(result.returncode)

    parsers = {"cppcheck": parse_cppcheck, "clang-tidy": parse_tidy, "iwyu": parse_iwyu}
    findings = parsers[name](all_output)
    failed = any(exit_codes)
    detail: dict[str, object] = {
        "scanned": [relative(unit) for unit in units],
        "scanned_count": len(units),
        "finding_count": len(findings),
    }
    if findings:
        detail["findings"] = findings[:FINDING_LIMIT]
        detail["suppressed_finding_count"] = max(0, len(findings) - FINDING_LIMIT)
    if failed and not findings:
        detail["log_tail"] = all_output[-LOG_LINE_LIMIT:]
        detail["returncodes"] = exit_codes
    return check.finish(not failed, "passed" if not failed else "findings detected", **detail)

"""Checks that operate directly on repository source files."""

from __future__ import annotations

import json
import re
import shutil
import subprocess
from pathlib import Path
from typing import Sequence

from .console import show_skip
from .model import Check, Gate
from .profile import load as load_profile
from .project import FINDING_LIMIT, command_output, relative, root, source_files


def format_check(gate: Gate) -> bool:
    files = source_files()
    if not files:
        return gate.custom("format", 10, lambda check: check.finish(False, "no source files found"))
    return gate.custom("format", 10, lambda check: _format(check, files, gate.verbose))


def _format(check: Check, files: Sequence[Path], verbose: bool) -> bool:
    if shutil.which("clang-format") is None:
        reason = "required system tool 'clang-format' was not found on PATH; skipped"
        show_skip(check.id, reason)
        return check.skip(reason, tool="clang-format")

    results: list[tuple[Path, subprocess.CompletedProcess[str]]] = []
    for file in files:
        results.append(
            (
                file,
                command_output(
                    ["clang-format", "--dry-run", "--Werror", relative(file)],
                    verbose=verbose,
                    check_id=f"format:{relative(file)}",
                ),
            )
        )
    unformatted = [
        relative(file)
        for file, result in results
        if result.returncode != 0 and "-Wclang-format-violations" in result.stdout
    ]
    tool_failures = [
        relative(file)
        for file, result in results
        if result.returncode != 0 and "-Wclang-format-violations" not in result.stdout
    ]
    if tool_failures:
        return check.finish(
            False,
            "clang-format could not inspect source files",
            files=tool_failures,
            count=len(tool_failures),
        )
    return check.finish(
        not unformatted,
        "passed" if not unformatted else "source files require formatting",
        files=unformatted,
        count=len(unformatted),
    )


def complexity_check(gate: Gate, files: Sequence[Path] | None = None) -> bool:
    if files is not None and not files:
        return gate.custom(
            "complexity",
            10,
            lambda check: check.finish(
                True, "no recompiled source files", scanned=[], scanned_count=0
            ),
        )
    paths = (
        [relative(file) for file in files]
        if files is not None
        else [name for name in load_profile().source_roots if (root() / name).is_dir()]
    )
    return gate.command(
        "complexity",
        10,
        ["lizard", "-l", "cpp", "-C", "10", "-L", "80", "-a", "5", *paths],
    )


def architecture_check(gate: Gate) -> bool:
    return gate.custom("architecture", 10, _architecture)


def _architecture(check: Check) -> bool:
    profile = load_profile()
    rules_path = profile.architecture_rules
    if not rules_path.is_file():
        return check.finish(False, "architecture rules are missing", expected=relative(rules_path))
    rules = json.loads(rules_path.read_text(encoding="utf-8"))
    if rules.get("schema") != profile.architecture_rules_schema:
        return check.finish(
            False,
            "unsupported architecture rules schema",
            expected=profile.architecture_rules_schema,
            actual=rules.get("schema"),
        )
    violations: list[dict[str, object]] = []
    files = source_files()
    for rule in rules.get("forbidden_include_prefixes", []):
        for file in files:
            file_name = relative(file)
            if not any(file_name.startswith(prefix) for prefix in rule["from"]):
                continue
            for number, text in enumerate(file.read_text(encoding="utf-8").splitlines(), 1):
                include = re.match(r'\s*#\s*include\s*[<"]([^">]+)[">]', text)
                if include and any(include.group(1).startswith(item) for item in rule["forbidden"]):
                    violations.append(
                        {
                            "rule": rule["id"],
                            "file": file_name,
                            "line": number,
                            "include": include.group(1),
                        }
                    )
    return check.finish(
        not violations,
        "passed" if not violations else "forbidden module include",
        violations=violations[:FINDING_LIMIT],
        count=len(violations),
    )

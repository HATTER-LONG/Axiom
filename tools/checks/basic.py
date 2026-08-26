"""Checks that operate directly on repository source files."""

from __future__ import annotations

import json
import re
from pathlib import Path
from typing import Sequence

from .model import Check, Gate
from .project import FINDING_LIMIT, ROOT, relative, source_files

ARCHITECTURE_RULES_SCHEMA = "axiom-architecture-rules/v1"


def format_check(gate: Gate) -> bool:
    files = [relative(file) for file in source_files()]
    if not files:
        return gate.custom("format", 10, lambda check: check.finish(False, "no source files found"))
    return gate.command("format", 10, ["clang-format", "--dry-run", "--Werror", *files])


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
        else [name for name in ("src", "apps", "tests") if (ROOT / name).is_dir()]
    )
    return gate.command("complexity", 10, ["lizard", "-l", "cpp", "-C", "10", "-L", "80", *paths])


def architecture_check(gate: Gate) -> bool:
    return gate.custom("architecture", 10, _architecture)


def _architecture(check: Check) -> bool:
    rules_path = ROOT / "quality" / "architecture_rules.json"
    if not rules_path.is_file():
        return check.finish(False, "architecture rules are missing", expected=relative(rules_path))
    rules = json.loads(rules_path.read_text(encoding="utf-8"))
    if rules.get("schema") != ARCHITECTURE_RULES_SCHEMA:
        return check.finish(
            False,
            "unsupported architecture rules schema",
            expected=ARCHITECTURE_RULES_SCHEMA,
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

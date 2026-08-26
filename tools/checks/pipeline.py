"""Shared orchestration helpers for gate modes."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Sequence

from .analyzers import analyzer, missing_analyzer_tool
from .basic import architecture_check, complexity_check, format_check
from .console import show_error
from .model import Gate
from .project import compile_units


def source_checks(gate: Gate) -> None:
    format_check(gate)
    complexity_check(gate)
    architecture_check(gate)


def run_analyzers(
    gate: Gate,
    preset: str,
    only: str | None = None,
    units: Sequence[Path] | None = None,
    analyzers: Sequence[str] | None = None,
) -> None:
    names = (only,) if only else tuple(analyzers or ("cppcheck", "clang-tidy"))
    available: list[str] = []
    for name in names:
        missing = missing_analyzer_tool(name)
        if missing:
            show_error(name, f"required system tool was not found on PATH: {missing}")
            gate.custom(
                name,
                10,
                lambda check, missing_tool=missing: check.finish(
                    False, "required system tool is unavailable", tool=missing_tool
                ),
            )
        else:
            available.append(name)
    names = tuple(available)
    if not names:
        return
    if units is None:
        try:
            units = compile_units(preset)
        except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
            for name in names:
                gate.blocked(name, 10, f"compilation database unavailable: {error}")
            return
    if not units:
        for name in names:
            gate.custom(
                name,
                10,
                lambda check: check.finish(
                    True, "no recompiled source files", scanned=[], scanned_count=0, finding_count=0
                ),
            )
        return
    for name in names:
        analyzer(gate, name, units, preset)

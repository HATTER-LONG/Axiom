"""Shared isolated-project helpers."""

from __future__ import annotations

import json
import shutil
import sys
import tempfile
from contextlib import contextmanager
from pathlib import Path
from typing import Iterator

TOOLS = Path(__file__).resolve().parents[1]
FIXTURE = Path(__file__).resolve().parent / "fixture"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))

from checks.model import Gate  # noqa: E402
from checks.project import root, set_root  # noqa: E402


def gate(mode: str = "test", *, is_gate: bool = True) -> Gate:
    return Gate(mode, is_gate=is_gate, report_schema="check-test/v1")


@contextmanager
def isolated_project() -> Iterator[Path]:
    previous = root()
    with tempfile.TemporaryDirectory(prefix="check-test-") as directory:
        project = Path(directory) / "project"
        shutil.copytree(FIXTURE, project, ignore=shutil.ignore_patterns("out", "build-quality", "__pycache__"))
        set_root(project)
        try:
            yield project
        finally:
            set_root(previous)


def report_by_id(value: Gate) -> dict[str, dict[str, object]]:
    return {item["id"]: item for item in value.report()["checks"]}


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")

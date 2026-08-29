"""Non-mutating clang-format adapter for Axiom source files."""

import shutil
from collections.abc import Mapping
from pathlib import Path
from typing import Any

from checkflow.results import ToolAvailability
from checkflow.tools import Tool, ToolCheckResult, ToolContext, ToolResult, ToolRunResult

_SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}


class FormatTool(Tool):
    """Report formatting drift without modifying project files."""

    def __init__(self) -> None:
        self._unformatted: list[str] = []
        self._checked_count = 0

    def check(self, context: ToolContext) -> ToolCheckResult:
        del context
        if shutil.which("clang-format"):
            return ToolCheckResult()
        return ToolCheckResult(availability=ToolAvailability.UNAVAILABLE, message="clang-format was not found on PATH")

    def run(self, context: ToolContext, args: Mapping[str, Any]) -> ToolRunResult:
        del args
        files = _source_files(context.project_root)
        self._checked_count = len(files)
        self._unformatted = []
        for path in files:
            relative = path.relative_to(context.project_root).as_posix()
            result = context.runner.run(["clang-format", "--dry-run", "--Werror", relative], cwd=context.project_root)
            if result.exit_code != 0:
                self._unformatted.append(relative)
        return ToolRunResult(command="clang-format --dry-run --Werror", exit_code=0 if not self._unformatted else 1)

    def parse(self, result: ToolRunResult) -> ToolResult:
        return ToolResult(success=result.exit_code == 0, messages=[] if result.exit_code == 0 else [f"source requires formatting: {path}" for path in self._unformatted], details={"checked_count": self._checked_count, "unformatted_count": len(self._unformatted), "files": self._unformatted})


TOOL = FormatTool


def _source_files(project_root: Path) -> list[Path]:
    return sorted(path for root in ("src", "apps", "tests") for path in (project_root / root).rglob("*") if path.is_file() and path.suffix.lower() in _SOURCE_SUFFIXES)

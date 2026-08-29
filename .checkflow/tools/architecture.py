"""Axiom architecture-policy adapter."""

import json
import re
from collections.abc import Mapping
from pathlib import Path
from typing import Any

from checkflow.tools import Tool, ToolCheckResult, ToolContext, ToolResult, ToolRunResult

_INCLUDE = re.compile(r'\s*#\s*include\s*[<"]([^">]+)[">]')
_SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx"}


class ArchitectureTool(Tool):
    """Check direct includes against Axiom's versioned architecture policy."""

    def __init__(self) -> None:
        self._details: dict[str, object] = {}

    def check(self, context: ToolContext) -> ToolCheckResult:
        del context
        return ToolCheckResult()

    def run(self, context: ToolContext, args: Mapping[str, Any]) -> ToolRunResult:
        rules_path = context.project_root / str(args.get("rules", "quality/architecture_rules.json"))
        try:
            rules = json.loads(rules_path.read_text(encoding="utf-8"))
            violations = _violations(context.project_root, rules)
        except (OSError, TypeError, ValueError, json.JSONDecodeError) as error:
            return ToolRunResult(command="architecture policy", exit_code=2, stderr=str(error))
        self._details = {"rules": str(rules_path.relative_to(context.project_root)), "count": len(violations)}
        return ToolRunResult(command="architecture policy", exit_code=0 if not violations else 1, stdout=json.dumps(violations, ensure_ascii=False))

    def parse(self, result: ToolRunResult) -> ToolResult:
        if result.exit_code == 0:
            return ToolResult(success=True, details=self._details)
        try:
            violations = json.loads(result.stdout)
        except json.JSONDecodeError:
            return ToolResult(success=False, messages=[result.stderr or "architecture check failed"])
        messages = [f"{item['file']}:{item['line']}: forbidden include {item['include']} [{item['rule']}]" for item in violations[:50]]
        return ToolResult(success=False, messages=messages, details={**self._details, "violations": violations[:50]})


TOOL = ArchitectureTool


def _violations(project_root: Path, rules: Mapping[str, Any]) -> list[dict[str, object]]:
    if rules.get("schema") != "axiom-architecture-rules/v1":
        raise ValueError("unsupported architecture rules schema")
    files = [path for root in ("src", "apps", "tests") for path in (project_root / root).rglob("*") if path.is_file() and path.suffix.lower() in _SOURCE_SUFFIXES]
    findings: list[dict[str, object]] = []
    for rule in rules.get("forbidden_include_prefixes", []):
        if not isinstance(rule, dict):
            raise ValueError("architecture rule must be an object")
        source_prefixes = rule.get("from")
        forbidden = rule.get("forbidden")
        rule_id = rule.get("id")
        if not isinstance(rule_id, str) or not isinstance(source_prefixes, list) or not isinstance(forbidden, list):
            raise ValueError("architecture rule is incomplete")
        for path in files:
            relative = path.relative_to(project_root).as_posix()
            if not any(relative.startswith(prefix) for prefix in source_prefixes):
                continue
            for line, text in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
                match = _INCLUDE.match(text)
                if match and any(match.group(1).startswith(prefix) for prefix in forbidden):
                    findings.append({"rule": rule_id, "file": relative, "line": line, "include": match.group(1)})
    return findings

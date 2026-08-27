"""Real compiler/tool integration checks over isolated clean and broken projects."""

from __future__ import annotations

import json
import platform
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Sequence

from support import FIXTURE, TOOLS

CASES = Path(__file__).resolve().parent / "cases"


@dataclass
class Tool:
    name: str
    supported: bool
    path: str | None
    version: str | None
    reason: str | None = None


@dataclass
class Result:
    name: str
    kind: str
    status: str
    check: str
    summary: str
    elapsed_ms: int


def _version(path: str) -> str | None:
    try:
        result = subprocess.run(
            [path, "--version"],
            text=True,
            encoding="utf-8",
            errors="replace",
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
            timeout=10,
        )
        return next((line.strip() for line in result.stdout.splitlines() if line.strip()), None)
    except (OSError, subprocess.TimeoutExpired):
        return None


def probe_tools() -> dict[str, Tool]:
    names = (
        "cmake",
        "ctest",
        "ninja",
        "clang++",
        "clang-format",
        "lizard",
        "cppcheck",
        "clang-tidy",
        "include-what-you-use",
        "iwyu_tool.py",
        "llvm-profdata",
        "llvm-cov",
        "mull-runner",
        "mull-ir-frontend",
    )
    tools: dict[str, Tool] = {}
    for name in names:
        path = shutil.which(name)
        tools[name] = Tool(
            name=name,
            supported=path is not None,
            path=path,
            version=_version(path) if path else None,
            reason=None if path else "not found on PATH",
        )
    if not tools["iwyu_tool.py"].supported and tools["include-what-you-use"].path:
        sibling = Path(tools["include-what-you-use"].path).with_name("iwyu_tool.py")
        if sibling.is_file():
            tools["iwyu_tool.py"] = Tool("iwyu_tool.py", True, str(sibling), None)
    if platform.system() == "Windows":
        for name in ("mull-runner", "mull-ir-frontend"):
            tools[name].supported = False
            tools[name].reason = "Mull is unsupported on native Windows"
    return tools


def _capability(tools: dict[str, Tool], *required: str) -> tuple[bool, str]:
    missing = [name for name in required if not tools[name].supported]
    return (not missing, "" if not missing else f"missing: {', '.join(missing)}")


def _run_check(
    project: Path, *arguments: str
) -> tuple[subprocess.CompletedProcess[str], dict[str, object] | None]:
    result = subprocess.run(
        [sys.executable, str(TOOLS / "check.py"), "--project-root", str(project), *arguments],
        text=True,
        encoding="utf-8",
        errors="replace",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    try:
        report = json.loads(result.stdout)
    except (TypeError, ValueError, json.JSONDecodeError):
        report = None
    return result, report


def _check(report: dict[str, object] | None, check_id: str) -> dict[str, object] | None:
    if report is None:
        return None
    return next((item for item in report.get("checks", []) if item.get("id") == check_id), None)


def _copy_case(project: Path, case: str, destination: str) -> None:
    shutil.copy2(CASES / case / Path(destination).name, project / destination)


def _negative(
    tools: dict[str, Tool],
    name: str,
    required: Sequence[str],
    destination: str,
    arguments: Sequence[str],
    check_id: str,
    evidence: str | None = None,
) -> Result:
    supported, reason = _capability(tools, *required)
    if not supported:
        return Result(name, "negative", "UNSUPPORTED", check_id, reason, 0)
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix=f"check-test-{name}-") as directory:
        project = Path(directory) / "project"
        shutil.copytree(
            FIXTURE, project, ignore=shutil.ignore_patterns("out", "build-quality", "__pycache__")
        )
        _copy_case(project, name, destination)
        process, report = _run_check(project, *arguments)
        item = _check(report, check_id)
        evidence_found = True
        if evidence is not None:
            log = project / "out" / "hardening" / "Testing" / "Temporary" / "LastTest.log"
            evidence_found = (
                log.is_file()
                and evidence.lower() in log.read_text(encoding="utf-8", errors="replace").lower()
            )
    elapsed = round((time.monotonic() - started) * 1000)
    if (
        process.returncode != 0
        and item is not None
        and item.get("status") == "fail"
        and evidence_found
    ):
        return Result(
            name, "negative", "DETECTED", check_id, str(item.get("summary", "failed")), elapsed
        )
    detail = (
        process.stderr.strip().splitlines()[-1]
        if process.stderr.strip()
        else "expected failure was not reported"
    )
    if item is not None:
        detail = f"status={item.get('status')}: {item.get('summary')}"
    if not evidence_found:
        detail = f"failure lacked expected runtime evidence: {evidence}"
    return Result(name, "negative", "FAIL", check_id, detail, elapsed)


def _baseline(tools: dict[str, Tool], name: str, arguments: Sequence[str]) -> Result:
    supported, reason = _capability(tools, "cmake", "ctest", "ninja", "clang++")
    if not supported:
        return Result(name, "baseline", "UNSUPPORTED", name, reason, 0)
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix=f"check-test-{name}-") as directory:
        project = Path(directory) / "project"
        shutil.copytree(
            FIXTURE, project, ignore=shutil.ignore_patterns("out", "build-quality", "__pycache__")
        )
        process, report = _run_check(project, *arguments)
    elapsed = round((time.monotonic() - started) * 1000)
    if process.returncode == 0 and report is not None and report.get("passed") is True:
        return Result(name, "baseline", "PASS", name, "clean project passed", elapsed)
    failing = (
        []
        if report is None
        else [
            f"{item.get('id')}={item.get('status')}: {item.get('summary')} "
            f"{item.get('detail', {}).get('log_tail', '')}"
            for item in report.get("checks", [])
            if item.get("status") not in ("pass", "skipped")
        ]
    )
    if failing:
        detail = ", ".join(failing)
    elif process.stderr.strip():
        detail = f"exit={process.returncode}: {process.stderr.strip().splitlines()[-1]}"
    elif process.stdout.strip():
        detail = f"exit={process.returncode}: invalid JSON: {process.stdout.strip()[-160:]}"
    else:
        detail = f"exit={process.returncode}: no output or valid report"
    return Result(name, "baseline", "FAIL", name, detail, elapsed)


def run(report_path: Path | None = None) -> tuple[bool, dict[str, object]]:
    tools = probe_tools()
    build = ("cmake", "ctest", "ninja", "clang++")
    cases = (
        (
            "architecture",
            (),
            "library/answer.cpp",
            ("inspect", "architecture"),
            "architecture",
            None,
        ),
        ("format", ("clang-format",), "library/answer.cpp", ("inspect", "format"), "format", None),
        (
            "complexity",
            ("lizard",),
            "library/answer.cpp",
            ("inspect", "complexity"),
            "complexity",
            None,
        ),
        (
            "cppcheck",
            build + ("cppcheck",),
            "library/answer.cpp",
            ("inspect", "cppcheck", "--preset", "fixture-full"),
            "cppcheck",
            None,
        ),
        (
            "clang-tidy",
            build + ("clang-tidy",),
            "library/answer.cpp",
            ("inspect", "clang-tidy", "--preset", "fixture-full"),
            "clang-tidy",
            None,
        ),
        (
            "iwyu",
            build + ("include-what-you-use", "iwyu_tool.py"),
            "library/answer.cpp",
            ("inspect", "iwyu", "--preset", "fixture-full"),
            "iwyu",
            None,
        ),
        ("test-failure", build, "spec/answer_test.cpp", ("full",), "tests.full", None),
        (
            "coverage",
            build + ("llvm-profdata", "llvm-cov"),
            "library/answer.cpp",
            ("full",),
            "coverage.full",
            None,
        ),
        (
            "asan",
            build,
            "spec/answer_test.cpp",
            ("hardening",),
            "tests.asan-ubsan",
            "AddressSanitizer",
        ),
        (
            "ubsan",
            build,
            "spec/answer_test.cpp",
            ("hardening",),
            "tests.asan-ubsan",
            "runtime error",
        ),
    )
    results = [
        _baseline(tools, "clean-full", ("full",)),
        _baseline(tools, "clean-hardening", ("hardening",)),
    ]
    results.extend(_negative(tools, *case) for case in cases)
    failed = [result for result in results if result.status == "FAIL"]
    payload: dict[str, object] = {
        "schema": "check-test-integration/v1",
        "platform": {
            "system": platform.system(),
            "release": platform.release(),
            "python": sys.version.split()[0],
        },
        "tools": [asdict(tool) for tool in tools.values()],
        "results": [asdict(result) for result in results],
        "summary": {
            "passed": not failed,
            "detected": sum(result.status == "DETECTED" for result in results),
            "clean_passed": sum(result.status == "PASS" for result in results),
            "unsupported": sum(result.status == "UNSUPPORTED" for result in results),
            "unsupported_tools": sum(not tool.supported for tool in tools.values()),
            "failed": len(failed),
        },
    }
    if report_path is not None:
        report_path.parent.mkdir(parents=True, exist_ok=True)
        report_path.write_text(
            json.dumps(payload, indent=2, ensure_ascii=False) + "\n", encoding="utf-8"
        )
    return not failed, payload


def print_report(payload: dict[str, object]) -> None:
    print("\n=== SYSTEM TOOL STATUS ===")
    for tool in payload["tools"]:
        mark = "SUPPORTED" if tool["supported"] else "UNSUPPORTED"
        detail = tool["version"] or tool["reason"] or tool["path"] or ""
        print(f"{mark:11} {tool['name']:<24} {detail}")
    print("\n=== REAL PROJECT VERIFICATION ===")
    for result in payload["results"]:
        print(
            f"{result['status']:11} {result['name']:<18} {result['check']:<18} {result['summary']} ({result['elapsed_ms']} ms)"
        )
    summary = payload["summary"]
    print("\n=== FINAL RESULT ===")
    print(
        f"{'PASS' if summary['passed'] else 'FAIL'} | detected={summary['detected']} "
        f"clean={summary['clean_passed']} unsupported_cases={summary['unsupported']} "
        f"unsupported_tools={summary['unsupported_tools']} failed={summary['failed']}"
    )

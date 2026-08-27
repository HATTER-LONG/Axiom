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

from checks.mutation import find_mull_tools

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


def _log(message: str) -> None:
    print(message, flush=True)


def _step(index: int, total: int, name: str, message: str) -> None:
    _log(f"  [{index:>2}/{total}] {name:<18} {message}")


def _version(name: str, path: str, verbose: bool) -> str | None:
    command = [path, "--version"]
    if verbose:
        _log(f"  [probe:{name}] $ {subprocess.list2cmdline(command)}")
    try:
        result = subprocess.run(
            command,
            text=True,
            encoding="utf-8",
            errors="replace",
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
            timeout=10,
        )
        version = next((line.strip() for line in result.stdout.splitlines() if line.strip()), None)
    except (OSError, subprocess.TimeoutExpired) as error:
        if verbose:
            _log(f"  [probe:{name}] ERROR: {error}")
        return None
    if verbose:
        _log(f"  [probe:{name}] exit {result.returncode}: {version or 'no output'}")
    return version


def probe_tools(verbose: bool = False) -> dict[str, Tool]:
    system = platform.system()
    mull_tools = None if system == "Windows" else find_mull_tools()
    mull_paths = (
        {}
        if mull_tools is None
        else {"mull-runner": mull_tools[0], "mull-ir-frontend": mull_tools[1]}
    )
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
    total = len(names)
    for index, name in enumerate(names, start=1):
        path = mull_paths.get(name) or shutil.which(name)
        tool = Tool(
            name=name,
            supported=path is not None,
            path=path,
            version=_version(name, path, verbose) if path else None,
            reason=None if path else "not found on PATH",
        )
        if not tool.supported and name == "iwyu_tool.py" and tools["include-what-you-use"].path:
            sibling = Path(tools["include-what-you-use"].path).with_name("iwyu_tool.py")
            if sibling.is_file():
                tool = Tool("iwyu_tool.py", True, str(sibling), None)
        if system == "Windows" and name in ("mull-runner", "mull-ir-frontend"):
            tool.supported = False
            tool.reason = "Mull is unsupported on native Windows"
        tools[name] = tool
        mark = "SUPPORTED" if tool.supported else "UNSUPPORTED"
        detail = tool.version or tool.reason or tool.path or ""
        _log(f"  [{index:>2}/{total}] {name:<22} {mark:<11} {detail}")
    return tools


def _capability(tools: dict[str, Tool], *required: str) -> tuple[bool, str]:
    missing = [name for name in required if not tools[name].supported]
    return (not missing, "" if not missing else f"missing: {', '.join(missing)}")


def _run_check(
    project: Path, *arguments: str, verbose: bool = False
) -> tuple[subprocess.CompletedProcess[str], dict[str, object] | None]:
    command = [sys.executable, str(TOOLS / "check.py"), "--project-root", str(project)]
    if verbose:
        command.append("-v")
    command.extend(arguments)
    if verbose:
        _log(f"    $ {subprocess.list2cmdline(command)}")
    result = subprocess.run(
        command,
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
    if verbose:
        _log(f"    exit {result.returncode}")
        if report is None and result.stdout.strip():
            _log(f"    stdout: {result.stdout.strip()[:1000]}")
        if result.stderr.strip():
            for line in result.stderr.strip().splitlines()[-50:]:
                _log(f"    | {line}")
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
    *,
    index: int,
    total: int,
    verbose: bool,
) -> Result:
    supported, reason = _capability(tools, *required)
    if not supported:
        _step(index, total, name, f"UNSUPPORTED: {reason}")
        return Result(name, "negative", "UNSUPPORTED", check_id, reason, 0)
    _step(
        index,
        total,
        name,
        f"running check.py {' '.join(arguments)}; expect '{check_id}' to fail",
    )
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix=f"check-test-{name}-") as directory:
        project = Path(directory) / "project"
        shutil.copytree(
            FIXTURE, project, ignore=shutil.ignore_patterns("out", "build-quality", "__pycache__")
        )
        _copy_case(project, name, destination)
        process, report = _run_check(project, *arguments, verbose=verbose)
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
        result = Result(
            name, "negative", "DETECTED", check_id, str(item.get("summary", "failed")), elapsed
        )
        _step(index, total, name, f"{result.status:<11} {result.summary} ({elapsed} ms)")
        return result
    stderr_lines = [
        line
        for line in process.stderr.strip().splitlines()
        if line and not line.startswith("[check:")
    ]
    detail = stderr_lines[-1] if stderr_lines else "expected failure was not reported"
    if item is not None:
        detail = f"status={item.get('status')}: {item.get('summary')}"
    if not evidence_found:
        detail = f"failure lacked expected runtime evidence: {evidence}"
    result = Result(name, "negative", "FAIL", check_id, detail, elapsed)
    _step(index, total, name, f"{result.status:<11} {result.summary} ({elapsed} ms)")
    return result


def _baseline(
    tools: dict[str, Tool],
    name: str,
    arguments: Sequence[str],
    *,
    index: int,
    total: int,
    verbose: bool,
) -> Result:
    supported, reason = _capability(tools, "cmake", "ctest", "ninja", "clang++")
    if not supported:
        _step(index, total, name, f"UNSUPPORTED: {reason}")
        return Result(name, "baseline", "UNSUPPORTED", name, reason, 0)
    _step(index, total, name, f"running check.py {' '.join(arguments)}; expect a clean pass")
    started = time.monotonic()
    with tempfile.TemporaryDirectory(prefix=f"check-test-{name}-") as directory:
        project = Path(directory) / "project"
        shutil.copytree(
            FIXTURE, project, ignore=shutil.ignore_patterns("out", "build-quality", "__pycache__")
        )
        process, report = _run_check(project, *arguments, verbose=verbose)
    elapsed = round((time.monotonic() - started) * 1000)
    if process.returncode == 0 and report is not None and report.get("passed") is True:
        result = Result(name, "baseline", "PASS", name, "clean project passed", elapsed)
        _step(index, total, name, f"{result.status:<11} {result.summary} ({elapsed} ms)")
        return result
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
    stderr_lines = [
        line
        for line in process.stderr.strip().splitlines()
        if line and not line.startswith("[check:")
    ]
    if failing:
        detail = ", ".join(failing)
    elif stderr_lines:
        detail = f"exit={process.returncode}: {stderr_lines[-1]}"
    elif process.stdout.strip():
        detail = f"exit={process.returncode}: invalid JSON: {process.stdout.strip()[-160:]}"
    else:
        detail = f"exit={process.returncode}: no output or valid report"
    result = Result(name, "baseline", "FAIL", name, detail, elapsed)
    _step(index, total, name, f"{result.status:<11} {result.summary} ({elapsed} ms)")
    return result


def run(report_path: Path | None = None, verbose: bool = False) -> tuple[bool, dict[str, object]]:
    _log("=== SYSTEM TOOL STATUS ===")
    tools = probe_tools(verbose)
    _log("")
    _log("=== REAL PROJECT VERIFICATION ===")
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
    total = 2 + len(cases)
    results = [
        _baseline(tools, "clean-full", ("full",), index=1, total=total, verbose=verbose),
        _baseline(tools, "clean-hardening", ("hardening",), index=2, total=total, verbose=verbose),
    ]
    results.extend(
        _negative(tools, *case, index=offset + 3, total=total, verbose=verbose)
        for offset, case in enumerate(cases)
    )
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
    summary = payload["summary"]
    print("\n=== FINAL RESULT ===")
    print(
        f"{'PASS' if summary['passed'] else 'FAIL'} | detected={summary['detected']} "
        f"clean={summary['clean_passed']} unsupported_cases={summary['unsupported']} "
        f"unsupported_tools={summary['unsupported_tools']} failed={summary['failed']}"
    )

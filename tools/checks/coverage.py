"""LLVM source-based coverage collection and reporting."""

from __future__ import annotations

import json
import shutil
from pathlib import Path

from .console import show_error
from .model import Check, Gate
from .project import LOG_LINE_LIMIT, build_directory, command_output, relative

COVERAGE_THRESHOLD = 90.0
IGNORE_FILENAME_REGEX = r"[/\\]tests[/\\]"


def clean_coverage_profiles(preset: str) -> None:
    for profile in build_directory(preset).rglob("*.profraw"):
        profile.unlink(missing_ok=True)


def coverage(gate: Gate, preset: str, check_id: str = "coverage") -> bool:
    return gate.custom(check_id, 10, lambda check: _coverage(check, gate, preset))


def _coverage(check: Check, gate: Gate, preset: str) -> bool:
    profdata_tool = _llvm_tool("llvm-profdata")
    coverage_tool = _llvm_tool("llvm-cov")
    available = {"llvm-profdata": profdata_tool, "llvm-cov": coverage_tool}
    missing = [name for name, path in available.items() if path is None]
    if missing:
        show_error(check.id, f"required system tools were not found: {', '.join(missing)}")
        return check.finish(False, "required system tools are unavailable", tool=missing)

    build_dir = build_directory(preset)
    if not _cache_option_enabled(build_dir / "CMakeCache.txt", "AXIOM_COVERAGE"):
        return check.finish(False, f"preset '{preset}' is not configured with AXIOM_COVERAGE=ON")

    profiles = sorted(build_dir.rglob("*.profraw"))
    if not profiles:
        return check.finish(False, "no coverage profiles were produced")

    binaries = _instrumented_binaries(gate, preset, build_dir)
    if not binaries:
        return check.finish(False, "no instrumented test executables were found")

    profdata = build_dir / "coverage.profdata"
    merge = command_output(
        [
            profdata_tool,
            "merge",
            "-sparse",
            *[str(profile) for profile in profiles],
            "-o",
            str(profdata),
        ],
        verbose=gate.verbose,
        check_id=check.id,
    )
    if merge.returncode != 0:
        return check.finish(
            False,
            "coverage profile merge failed",
            returncode=merge.returncode,
            log_tail=merge.stdout.splitlines()[-LOG_LINE_LIMIT:],
        )

    export = command_output(
        [
            coverage_tool,
            "export",
            str(binaries[0]),
            *[f"-object={binary}" for binary in binaries[1:]],
            f"--instr-profile={profdata}",
            f"--ignore-filename-regex={IGNORE_FILENAME_REGEX}",
            "--format=text",
        ],
        verbose=gate.verbose,
        check_id=check.id,
    )
    if export.returncode != 0:
        return check.finish(
            False,
            "coverage export failed",
            returncode=export.returncode,
            log_tail=export.stdout.splitlines()[-LOG_LINE_LIMIT:],
        )
    try:
        totals = json.loads(export.stdout)["data"][0]["totals"]
        line_percent = round(float(totals["lines"]["percent"]), 1)
        region_percent = round(float(totals["regions"]["percent"]), 1)
        branch_percent = (
            round(float(totals["branches"]["percent"]), 1) if totals.get("branches") else None
        )
    except (ValueError, IndexError, KeyError, TypeError) as error:
        return check.finish(False, "coverage export could not be parsed", error=str(error))

    export_path = build_dir / "coverage-export.json"
    export_path.write_text(export.stdout, encoding="utf-8")

    html_report = None
    if gate.options.get("coverage-html", False):
        html_dir = build_dir / "coverage-html"
        html = command_output(
            [
                coverage_tool,
                "show",
                str(binaries[0]),
                *[f"-object={binary}" for binary in binaries[1:]],
                f"--instr-profile={profdata}",
                f"--ignore-filename-regex={IGNORE_FILENAME_REGEX}",
                "--format=html",
                f"--output-dir={html_dir}",
            ],
            verbose=gate.verbose,
            check_id=check.id,
        )
        if html.returncode == 0:
            html_report = html_dir / "index.html"

    passed = line_percent >= COVERAGE_THRESHOLD
    summary = (
        "passed"
        if passed
        else f"line coverage {line_percent}% is below the {COVERAGE_THRESHOLD}% threshold"
    )
    return check.finish(
        passed,
        summary,
        line_coverage=line_percent,
        region_coverage=region_percent,
        branch_coverage=branch_percent,
        threshold=COVERAGE_THRESHOLD,
        profile_count=len(profiles),
        binaries=[relative(binary) for binary in binaries],
        export=relative(export_path),
        html=relative(html_report) if html_report else None,
    )


def _cache_option_enabled(cache_path: Path, option: str) -> bool:
    if not cache_path.is_file():
        return False
    for line in cache_path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(f"{option}:"):
            value = line.split("=", 1)[1].strip().upper()
            return value in {"ON", "TRUE", "YES", "1"}
    return False


def _instrumented_binaries(gate: Gate, preset: str, build_dir: Path) -> list[Path]:
    result = command_output(
        ["ctest", "--preset", preset, "--show-only=json-v1"],
        verbose=gate.verbose,
        check_id="coverage:enumerate",
    )
    if result.returncode != 0:
        return []
    try:
        payload = json.loads(result.stdout)
    except ValueError:
        return []
    binaries: list[Path] = []
    for test in payload.get("tests", []):
        command = test.get("command") or []
        if not command:
            continue
        executable = Path(command[0]).resolve()
        try:
            executable.relative_to(build_dir.resolve())
        except ValueError:
            continue
        if executable.is_file() and executable not in binaries:
            binaries.append(executable)
    return binaries


def _llvm_tool(name: str) -> str | None:
    found = shutil.which(name)
    if found:
        return found
    compiler = shutil.which("clang++")
    if not compiler:
        return None
    for candidate in (name, f"{name}.exe"):
        sibling = Path(compiler).parent / candidate
        if sibling.is_file():
            return str(sibling)
    return None

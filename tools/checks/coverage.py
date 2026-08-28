"""LLVM source-based coverage collection and reporting."""

from __future__ import annotations

import json
import shutil
from pathlib import Path

from .console import show_skip
from .model import Check, Gate
from .profile import load as load_profile
from .project import LOG_LINE_LIMIT, build_directory, command_output, relative

COVERAGE_SCHEMA = "axiom-coverage/v1"


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
        reason = f"required system tools were not found: {', '.join(missing)}; skipped"
        show_skip(check.id, reason)
        return check.skip(reason, tool=missing)

    build_dir = build_directory(preset)
    profile = load_profile()
    option = profile.coverage_cache_option
    if not _cache_option_enabled(build_dir / "CMakeCache.txt", option):
        return check.finish(False, f"preset '{preset}' is not configured with {option}=ON")

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

    export_command = [
        coverage_tool,
        "export",
        str(binaries[0]),
        *[f"-object={binary}" for binary in binaries[1:]],
        f"--instr-profile={profdata}",
        f"--ignore-filename-regex={profile.coverage_ignore_regex}",
    ]
    export = command_output(
        [*export_command, "--format=text", "--summary-only", "--skip-functions"],
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
        coverage_data = json.loads(export.stdout)["data"][0]
        totals = coverage_data["totals"]
        line_percent = round(float(totals["lines"]["percent"]), 1)
        region_percent = round(float(totals["regions"]["percent"]), 1)
        branch_percent = (
            round(float(totals["branches"]["percent"]), 1) if totals.get("branches") else None
        )
    except (ValueError, IndexError, KeyError, TypeError) as error:
        return check.finish(False, "coverage export could not be parsed", error=str(error))

    line_export = command_output(
        [*export_command, "--format=lcov", "--skip-functions"],
        verbose=gate.verbose,
        check_id=check.id,
    )
    if line_export.returncode != 0:
        return check.finish(
            False,
            "line coverage export failed",
            returncode=line_export.returncode,
            log_tail=line_export.stdout.splitlines()[-LOG_LINE_LIMIT:],
        )

    export_path = build_dir / "coverage-export.json"
    try:
        compact_export = _compact_export(coverage_data, line_export.stdout)
    except (KeyError, TypeError, ValueError) as error:
        return check.finish(False, "line coverage export could not be parsed", error=str(error))
    export_path.write_text(
        json.dumps(compact_export, ensure_ascii=False, separators=(",", ":")) + "\n",
        encoding="utf-8",
    )

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
                f"--ignore-filename-regex={profile.coverage_ignore_regex}",
                "--format=html",
                f"--output-dir={html_dir}",
            ],
            verbose=gate.verbose,
            check_id=check.id,
        )
        if html.returncode == 0:
            html_report = html_dir / "index.html"

    threshold = profile.coverage_threshold
    passed = line_percent >= threshold
    summary = (
        "passed" if passed else f"line coverage {line_percent}% is below the {threshold}% threshold"
    )
    return check.finish(
        passed,
        summary,
        line_coverage=line_percent,
        region_coverage=region_percent,
        branch_coverage=branch_percent,
        threshold=threshold,
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


def _compact_export(coverage_data: dict[str, object], lcov: str) -> dict[str, object]:
    """Convert LLVM's repetitive export into a small, agent-oriented report."""
    file_summaries = {
        relative(Path(str(item["filename"]))): _summary(item.get("summary", {}))
        for item in coverage_data.get("files", [])
    }
    files: list[dict[str, object]] = []
    current: dict[str, object] | None = None
    lines: list[tuple[int, int]] = []
    branches: list[tuple[int, int, int, int | None]] = []

    def finish_file() -> None:
        nonlocal current, lines, branches
        if current is None:
            return
        path = str(current["file"])
        current["summary"] = file_summaries.get(path, {})
        current["line_runs"] = _line_runs(lines)
        current["branch_groups"] = _branch_groups(branches)
        if not _fully_covered(current["summary"]):
            files.append(current)
        current = None
        lines = []
        branches = []

    for record in lcov.splitlines():
        if record.startswith("SF:"):
            finish_file()
            current = {"file": relative(Path(record[3:]))}
        elif record.startswith("DA:") and current is not None:
            fields = record[3:].split(",")
            lines.append((int(fields[0]), int(fields[1])))
        elif record.startswith("BRDA:") and current is not None:
            line, block, branch, taken = record[5:].split(",", 3)
            branches.append(
                (int(line), int(block), int(branch), None if taken == "-" else int(taken))
            )
        elif record == "end_of_record":
            finish_file()
    finish_file()

    return {
        "schema": COVERAGE_SCHEMA,
        "legend": {
            "line_runs": {
                "layout": ["first_line", "last_line", "hits"],
                "meaning": "inclusive line range; hits=0 means uncovered",
            },
            "branch_groups": {
                "layout": ["line", "block", "first_branch", "branch_hits"],
                "meaning": (
                    "branch_hits[i] is the hit count for first_branch+i; "
                    "0 means uncovered and null means not evaluated"
                ),
            },
            "summary": {
                "layout": ["covered", "count", "percent"],
                "meaning": "covered items, total items, and coverage percentage",
            },
        },
        "totals": _summary(coverage_data["totals"]),
        "omitted_fully_covered_files": len(file_summaries) - len(files),
        "files": files,
    }


def _summary(value: object) -> dict[str, list[float | int]]:
    if not isinstance(value, dict):
        raise TypeError("coverage summary must be an object")
    result: dict[str, list[float | int]] = {}
    for name in ("lines", "branches", "regions", "functions", "instantiations", "mcdc"):
        metric = value.get(name)
        if not isinstance(metric, dict) or "count" not in metric or "covered" not in metric:
            continue
        count = int(metric["count"])
        covered = int(metric["covered"])
        percent = round(float(metric.get("percent", 100.0 if count == 0 else 0.0)), 1)
        result[name] = [covered, count, percent]
    return result


def _fully_covered(value: object) -> bool:
    if not isinstance(value, dict) or not value:
        return False
    measured = False
    for metric in value.values():
        if not isinstance(metric, list) or len(metric) < 2:
            return False
        covered, count = metric[:2]
        if count:
            measured = True
            if covered != count:
                return False
    return measured


def _line_runs(lines: list[tuple[int, int]]) -> list[list[int]]:
    runs: list[list[int]] = []
    for line, hits in sorted(lines):
        if runs and line == runs[-1][1] + 1 and hits == runs[-1][2]:
            runs[-1][1] = line
        else:
            runs.append([line, line, hits])
    return runs


def _branch_groups(
    branches: list[tuple[int, int, int, int | None]],
) -> list[list[object]]:
    groups: list[list[object]] = []
    for line, block, branch, hits in sorted(branches):
        if (
            groups
            and groups[-1][0] == line
            and groups[-1][1] == block
            and branch == groups[-1][2] + len(groups[-1][3])
        ):
            branch_hits = groups[-1][3]
            if isinstance(branch_hits, list):
                branch_hits.append(hits)
        else:
            groups.append([line, block, branch, [hits]])
    return groups


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

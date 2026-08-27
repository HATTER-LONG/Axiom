"""Project-owned settings for the reusable quality-check engine."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

from .project import root


@dataclass(frozen=True)
class Profile:
    report_schema: str
    architecture_rules: Path
    architecture_rules_schema: str
    coverage_cache_option: str
    mutation_frontend_option: str
    presets: dict[str, str]
    source_roots: tuple[str, ...]
    build_directories: dict[str, str]
    coverage_threshold: float
    coverage_ignore_regex: str
    mutation_score_threshold: float

    def preset(self, name: str) -> str:
        try:
            return self.presets[name]
        except KeyError as error:
            raise ValueError(f"profile has no preset for '{name}'") from error

    def build_directory(self, preset: str) -> Path:
        try:
            return root() / self.build_directories[preset]
        except KeyError as error:
            raise ValueError(f"profile has no build directory for preset '{preset}'") from error


def load() -> Profile:
    """Load explicit project configuration, keeping the engine project-agnostic."""
    path = root() / "quality" / "check_profile.json"
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        return Profile(
            report_schema=str(data["report_schema"]),
            architecture_rules=root() / str(data["architecture_rules"]),
            architecture_rules_schema=str(data["architecture_rules_schema"]),
            coverage_cache_option=str(data["coverage_cache_option"]),
            mutation_frontend_option=str(data["mutation_frontend_option"]),
            presets={str(name): str(value) for name, value in data["presets"].items()},
            source_roots=tuple(str(value) for value in data.get("source_roots", ("src", "apps", "tests"))),
            build_directories={
                str(name): str(value)
                for name, value in data.get(
                    "build_directories",
                    {
                        preset: f"build-quality/{preset.removeprefix('quality-')}"
                        for preset in data["presets"].values()
                    },
                ).items()
            },
            coverage_threshold=float(data.get("coverage_threshold", 90.0)),
            coverage_ignore_regex=str(data.get("coverage_ignore_regex", r"[/\\]tests[/\\]")),
            mutation_score_threshold=float(data.get("mutation_score_threshold", 90.0)),
        )
    except (OSError, KeyError, TypeError, ValueError, json.JSONDecodeError) as error:
        raise ValueError(f"invalid check profile {path}: {error}") from error

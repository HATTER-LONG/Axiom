"""Runtime hardening gate using the sanitizer CMake preset."""

from __future__ import annotations

from ..cmake import build_and_test
from ..model import Gate


def run(gate: Gate) -> None:
    build_and_test(gate, "quality-hardening", "tests.asan-ubsan")

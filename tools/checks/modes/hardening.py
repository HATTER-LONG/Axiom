"""Runtime and test-suite hardening using sanitizers and mutation testing."""

from __future__ import annotations

from ..cmake import build, configure, tests
from ..model import Gate
from ..mutation import find_mull_tools, mutation_test


def run(gate: Gate) -> None:
    sanitizer_preset = "quality-hardening"
    if not configure(gate, sanitizer_preset):
        gate.blocked("build", 20, "configure failed")
        gate.blocked("tests.asan-ubsan", 15, "configure failed")
    elif not build(gate, sanitizer_preset):
        gate.blocked("tests.asan-ubsan", 15, "build failed")
    else:
        tests(gate, sanitizer_preset, "tests.asan-ubsan")

    mutation_preset = "quality-mutation"
    mull_tools = find_mull_tools()
    if mull_tools is None:
        gate.custom(
            "configure.mull",
            10,
            lambda check: check.finish(
                False,
                "required system tool is unavailable",
                tool="matching mull-runner and mull-ir-frontend",
            ),
        )
        gate.blocked("build.mull", 20, "Mull tools are unavailable")
        gate.blocked("mull", 15, "Mull tools are unavailable")
        return
    runner, frontend = mull_tools
    configure_command = [
        "cmake",
        "--preset",
        mutation_preset,
        f"-DAXIOM_MULL_IR_FRONTEND={frontend}",
    ]
    if not gate.command("configure.mull", 10, configure_command):
        gate.blocked("build.mull", 20, "mutation configure failed")
        gate.blocked("mull", 15, "mutation configure failed")
        return
    if not gate.command(
        "build.mull", 20, ["cmake", "--build", "--preset", mutation_preset, "--", "-k", "0"]
    ):
        gate.blocked("mull", 15, "mutation build failed")
        return
    mutation_test(gate, mutation_preset, runner)

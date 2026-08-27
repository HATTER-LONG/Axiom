"""Runtime and test-suite hardening using sanitizers and mutation testing."""

from __future__ import annotations

import platform

from ..cmake import build, configure, tests
from ..model import Gate
from ..mutation import find_mull_tools, mutation_test


def run(gate: Gate) -> None:
    sanitizer_preset = "quality-hardening"
    if not configure(gate, sanitizer_preset):
        gate.blocked("build", 20, "configure failed")
        gate.blocked("tests.asan-ubsan", 15, "configure failed")
    elif (configure_reason := gate.skip_reason("configure")):
        gate.skipped("build", 20, f"configure skipped: {configure_reason}")
        gate.skipped("tests.asan-ubsan", 15, f"configure skipped: {configure_reason}")
    elif not build(gate, sanitizer_preset):
        gate.blocked("tests.asan-ubsan", 15, "build failed")
    elif (build_reason := gate.skip_reason("build")):
        gate.skipped("tests.asan-ubsan", 15, f"build skipped: {build_reason}")
    else:
        tests(gate, sanitizer_preset, "tests.asan-ubsan")

    mutation_preset = "quality-mutation"
    # Mull 0.34 publishes and tests Linux/macOS builds only.  In particular, its
    # JIT runner and LLVM frontend are not a native Windows toolchain, so do not
    # misreport this as an arbitrary missing executable on Windows.
    if platform.system() == "Windows":
        reason = "Mull is unsupported on native Windows; skipped (run it in WSL, Linux, or macOS)"
        gate.skipped("configure.mull", 10, reason, platform=platform.system())
        gate.skipped("build.mull", 20, reason, platform=platform.system())
        gate.skipped("mull", 15, reason, platform=platform.system())
        return

    mull_tools = find_mull_tools()
    if mull_tools is None:
        reason = "matching Mull tools are unavailable; skipped"
        gate.skipped(
            "configure.mull",
            10,
            reason,
            tool="matching mull-runner and mull-ir-frontend",
        )
        gate.skipped("build.mull", 20, reason)
        gate.skipped("mull", 15, reason)
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
    if configure_reason := gate.skip_reason("configure.mull"):
        gate.skipped("build.mull", 20, f"mutation configure skipped: {configure_reason}")
        gate.skipped("mull", 15, f"mutation configure skipped: {configure_reason}")
        return
    if not gate.command(
        "build.mull", 20, ["cmake", "--build", "--preset", mutation_preset, "--", "-k", "0"]
    ):
        gate.blocked("mull", 15, "mutation build failed")
        return
    if build_reason := gate.skip_reason("build.mull"):
        gate.skipped("mull", 15, f"mutation build skipped: {build_reason}")
        return
    mutation_test(gate, mutation_preset, runner)

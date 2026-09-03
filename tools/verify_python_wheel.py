"""Build an axiom wheel and verify install, relocation, and import behavior.

Everything happens inside one temporary directory that is removed on exit:
the wheel is built into a temporary wheelhouse, installed into a temporary
venv, and consumed by a consumer compiled against the installed package. The
script never installs into the interpreter running the test and never writes
into the source tree. The CMake wheel build tree is moved aside before any
import so nothing can silently fall back to build-tree libraries.
"""

from __future__ import annotations

import argparse
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
import zipfile

LINUX_CORE_LIBRARY = re.compile(r"^axiom/libaxiom\.so(\.\d+)*$")
LINUX_HOST_LIBRARY = re.compile(r"^axiom/libaxiom_python_host\.so(\.\d+)*$")
LINUX_SOVERSIONED = re.compile(r"^axiom/libaxiom\.so\.\d+\.\d+$")
WINDOWS_CORE_LIBRARY = re.compile(r"^axiom/axiom\.dll$")
WINDOWS_HOST_LIBRARY = re.compile(r"^axiom/axiom_python_host\.dll$")


def resolve_uv() -> str:
    located = shutil.which("uv")
    if located:
        return located
    raise SystemExit(
        "uv was not found on PATH. Install it from https://docs.astral.sh/uv/"
    )


def run(arguments: list[str], cwd: pathlib.Path, env: dict[str, str] | None = None) -> str:
    completed = subprocess.run(
        arguments, cwd=cwd, env=env, check=False, capture_output=True, text=True
    )
    if completed.returncode != 0:
        raise RuntimeError(
            f"command failed ({completed.returncode}): {arguments}\n"
            f"stdout:\n{completed.stdout}\nstderr:\n{completed.stderr}"
        )
    return completed.stdout.strip()


def venv_python(venv_directory: pathlib.Path) -> pathlib.Path:
    if sys.platform == "win32":
        return venv_directory / "Scripts" / "python.exe"
    return venv_directory / "bin" / "python"


def expected_python_tag() -> str:
    return f"cp{sys.version_info[0]}{sys.version_info[1]}"


def check_wheel_tags(wheel: pathlib.Path) -> None:
    # Wheel names look like axiom-0.1.0-cp312-cp312-linux_x86_64.whl.
    parts = wheel.stem.split("-")
    if len(parts) < 5 or parts[0] != "axiom":
        raise SystemExit(f"unexpected wheel name: {wheel.name}")
    python_tag, abi_tag, platform_tag = parts[-3], parts[-2], parts[-1]
    expected = expected_python_tag()
    if python_tag != expected:
        raise SystemExit(f"wheel targets {python_tag}; support matrix requires {expected}")
    if abi_tag != expected:
        raise SystemExit(f"wheel abi tag {abi_tag} must equal {expected} (no abi3 support)")
    if sys.platform == "win32":
        if platform_tag != "win_amd64":
            raise SystemExit(f"wheel platform {platform_tag} is not win_amd64")
    elif not (platform_tag.startswith("linux_") or platform_tag.startswith("manylinux")):
        raise SystemExit(f"wheel platform {platform_tag} is not a Linux tag")


def check_wheel_contents(wheel: pathlib.Path) -> None:
    with zipfile.ZipFile(wheel) as archive:
        names = archive.namelist()

    for name in names:
        lowered = name.lower()
        if lowered.endswith((".c", ".cc", ".cpp", ".h", ".hpp")):
            raise SystemExit(f"wheel contains a source or header file: {name}")
        if lowered.endswith(".a"):
            raise SystemExit(f"wheel contains a static archive: {name}")
        if lowered.endswith(".lib") and sys.platform != "win32":
            raise SystemExit(f"wheel contains an import library on a non-Windows build: {name}")
        if "tests" in name.split("/") or name.startswith("python/") or "pybind11" in name:
            raise SystemExit(f"wheel contains test or project files: {name}")

    extensions = [name for name in names if re.match(r"^axiom/_axiom\..*\.(so|pyd)$", name)]
    if len(extensions) != 1:
        raise SystemExit(f"wheel must contain exactly one extension, found {extensions}")

    if sys.platform == "win32":
        if not any(WINDOWS_CORE_LIBRARY.match(name) for name in names):
            raise SystemExit("wheel does not contain axiom.dll")
        if not any(WINDOWS_HOST_LIBRARY.match(name) for name in names):
            raise SystemExit("wheel does not contain axiom_python_host.dll")
        return

    core_entries = [name for name in names if LINUX_CORE_LIBRARY.match(name)]
    host_entries = [name for name in names if LINUX_HOST_LIBRARY.match(name)]
    if not core_entries:
        raise SystemExit("wheel does not contain the shared Axiom library")
    if not host_entries:
        raise SystemExit("wheel does not contain the shared PythonHost library")
    if not any(LINUX_SOVERSIONED.match(name) for name in core_entries):
        raise SystemExit(
            f"wheel lacks the SOVERSIONed Axiom install file; found {core_entries}"
        )
    versioned_core = [name for name in core_entries if re.search(r"\.so\.\d+\.\d+$", name)]
    if len(versioned_core) != 1:
        raise SystemExit(f"wheel must ship exactly one versioned Core library: {core_entries}")


def check_installed_package(python: pathlib.Path) -> pathlib.Path:
    package_dir = pathlib.Path(
        run([str(python), "-c", "import axiom, os; print(os.path.dirname(axiom.__file__))"],
            cwd=python.parent)
    )
    required = [
        package_dir / "_axiom",
    ]
    if sys.platform == "win32":
        required += [package_dir / "axiom.dll", package_dir / "axiom_python_host.dll"]
    else:
        soversioned = [path for path in package_dir.iterdir()
                       if re.match(r"libaxiom\.so\.\d+\.\d+$", path.name)]
        host_versioned = [path for path in package_dir.iterdir()
                          if re.match(r"libaxiom_python_host\.so\.\d+\.\d+$", path.name)]
        if not soversioned or not host_versioned:
            raise SystemExit(
                f"installed package lacks SOVERSIONed shared libraries in {package_dir}"
            )
        required += [package_dir / "libaxiom.so", package_dir / "libaxiom_python_host.so"]
    for path in required:
        if path.suffix == "" and path.name == "_axiom":
            if not any(package_dir.glob("_axiom*.so")) and not (package_dir / "_axiom.pyd").exists():
                raise SystemExit(f"installed package misses the extension in {package_dir}")
        elif not path.exists():
            raise SystemExit(f"installed package misses {path}")
    return package_dir


def check_runtime_resolution(consumer: pathlib.Path, package_dir: pathlib.Path) -> None:
    if sys.platform == "win32":
        return
    listing = run(["ldd", str(consumer)], cwd=consumer.parent)
    for line in listing.splitlines():
        if "libaxiom" not in line:
            continue
        resolved = line.split("=>")[-1].strip().split(" ")[0]
        if not resolved.startswith(str(package_dir)):
            raise SystemExit(f"consumer resolves an Axiom library outside the wheel: {line}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--consumer", required=True, help="wheel consumer CMake project")
    parser.add_argument("--source", required=True, help="Axiom source repository root")
    parser.add_argument("--build-tree", required=True, help="CMake wheel build tree to quarantine")
    parser.add_argument("--compiler", required=True, help="C++ compiler matching the build ABI")
    parser.add_argument("--dispatch", required=True, help="dispatch pytest file")
    parser.add_argument("--closed", required=True, help="closed-host pytest file")
    parser.add_argument("--facade", required=True, help="facade pytest file")
    parser.add_argument("--package", required=True, help="python/ project directory")
    arguments = parser.parse_args()

    package = pathlib.Path(arguments.package).resolve()
    source = pathlib.Path(arguments.source).resolve()
    consumer_project = pathlib.Path(arguments.consumer).resolve()
    build_tree = pathlib.Path(arguments.build_tree).resolve()

    env = dict(os.environ)
    env.pop("PIP_REQUIRE_VIRTUALENV", None)

    with tempfile.TemporaryDirectory(prefix="axiom-wheel-verify-") as scratch:
        root = pathlib.Path(scratch)
        wheelhouse = root / "wheelhouse"
        run(
            [
                sys.executable,
                "-m",
                "pip",
                "wheel",
                ".",
                "--no-build-isolation",
                "--no-deps",
                "-w",
                str(wheelhouse),
                f"--config-settings=build-dir={root / 'skbuild'}",
            ],
            cwd=package,
            env=env,
        )
        wheels = list(wheelhouse.glob("axiom-*.whl"))
        if len(wheels) != 1:
            raise SystemExit(f"expected exactly one wheel, found {wheels}")
        wheel = wheels[0]
        check_wheel_tags(wheel)
        check_wheel_contents(wheel)

        quarantined: pathlib.Path | None = None
        if build_tree.exists():
            # Same filesystem as the build tree so the quarantine rename is atomic.
            quarantined = build_tree.with_name(build_tree.name + "-quarantined")
            shutil.rmtree(quarantined, ignore_errors=True)
            build_tree.rename(quarantined)
        try:
            uv = resolve_uv()
            venv_directory = root / "venv"
            sync_env = dict(env)
            sync_env["UV_PROJECT_ENVIRONMENT"] = str(venv_directory)
            run(
                [uv, "sync", "--project", str(package), "--frozen", "--python", sys.executable],
                cwd=source,
                env=sync_env,
            )
            python = venv_python(venv_directory)
            run(
                [uv, "pip", "install", "--python", str(python), "--no-deps", str(wheel)],
                cwd=root,
                env=env,
            )
            run(
                [str(python), "-c",
                 "import axiom; from axiom import _axiom; assert axiom.Host"],
                cwd=root,
                env=env,
            )
            package_dir = check_installed_package(python)

            consumer_build = root / "consumer-build"
            pybind11_cmake_dir = run(
                [sys.executable, "-c", "import pybind11; print(pybind11.get_cmake_dir(), end='')"],
                cwd=root,
            )
            configure = [
                "cmake",
                "-S",
                str(consumer_project),
                "-B",
                str(consumer_build),
                "-G",
                "Ninja",
                f"-DCMAKE_CXX_COMPILER={arguments.compiler}",
                "-DCMAKE_BUILD_TYPE=Release",
                f"-DAXIOM_SOURCE_DIR={source}",
                f"-DAXIOM_WHEEL_PACKAGE_DIR={package_dir}",
                f"-DAXIOM_PYBIND11_CMAKE_DIR={pybind11_cmake_dir}",
                f"-DPython_EXECUTABLE={python}",
            ]
            run(configure, cwd=root, env=env)
            run(["cmake", "--build", str(consumer_build)], cwd=root, env=env)
            consumer = consumer_build / (
                "axiom_wheel_consumer.exe" if sys.platform == "win32" else "axiom_wheel_consumer"
            )
            check_runtime_resolution(consumer, package_dir)

            site_packages = package_dir.parent
            consumer_env = dict(env)
            consumer_env["AXIOM_PYTHON_EXECUTABLE"] = str(python)
            consumer_env["AXIOM_PYTHON_HOME"] = run(
                [str(python), "-c", "import sys; print(sys.base_prefix, end='')"],
                cwd=root,
            )
            site_lib = run(
                [
                    str(python),
                    "-c",
                    "import sysconfig; print(sysconfig.get_path('purelib'), end='')",
                ],
                cwd=root,
            )
            existing_pythonpath = consumer_env.get("PYTHONPATH", "")
            consumer_env["PYTHONPATH"] = (
                site_lib
                if not existing_pythonpath
                else f"{site_lib}{os.pathsep}{existing_pythonpath}"
            )
            if sys.platform == "win32":
                python_home = consumer_env["AXIOM_PYTHON_HOME"]
                consumer_env["PATH"] = (
                    f"{package_dir}{os.pathsep}{python_home}{os.pathsep}"
                    f"{consumer_env.get('PATH', '')}"
                )
            for test_file in (arguments.dispatch, arguments.closed, arguments.facade):
                run(
                    [str(consumer), test_file, str(site_packages), str(site_packages)],
                    cwd=root,
                    env=consumer_env,
                )
        finally:
            if quarantined is not None:
                shutil.rmtree(build_tree, ignore_errors=True)
                quarantined.rename(build_tree)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

# Axiom

A minimal CMake project skeleton with a reusable `core` library and a `demo`
executable. The framework is intentionally empty so new modules can be added
incrementally.

[中文文档](README.zh-CN.md)

## Build

```sh
cmake --preset dev
cmake --build --preset dev
```

## Quality gates

Run gates through uv so agents get one compact JSON result rather than compiler
logs. A non-zero exit code always means the selected gate failed.

```sh
uv run --quiet python tools/check.py fast
uv run --quiet python tools/check.py full
uv run --quiet python tools/check.py hardening
```

Add `-v` or `--verbose` before or after the gate name to print each executed
command, its combined output, and its exit code to stderr. The JSON report stays
on stdout for machine consumption.

Use `--report <path>` when a later agent needs the same immutable report file.
`fast` applies complexity, cppcheck, and clang-tidy only to source files
recompiled by its incremental build. Its inexpensive architecture rule scan
remains whole-project so header-only violations are not missed. Formatting is
intentionally deferred to `full`, which also runs cppcheck, clang-tidy, and
Include-What-You-Use over every project compilation unit. `hardening` builds and
runs the test suite with AddressSanitizer and UndefinedBehaviorSanitizer enabled.

`fast` and `full` build with LLVM source-based coverage instrumentation and fail
when the test suite's line coverage drops below 90%; the report also records
region and branch coverage and writes `coverage-export.json` into the build
directory. Add `--coverage-html` to additionally render the browsable
`llvm-cov` HTML report into `coverage-html/` next to it.

For diagnosis only, without claiming a gate passed:

```sh
uv run --quiet python tools/check.py inspect format
uv run --quiet python tools/check.py inspect tests --preset quality-fast
uv run --quiet python tools/check.py inspect coverage
uv run --quiet python tools/check.py inspect cppcheck
uv run --quiet python tools/check.py inspect clang-tidy
uv run --quiet python tools/check.py inspect iwyu
uv run --quiet python tools/check.py --list
```

The two analyzer inspections and the full gate scan every project compilation
unit from CMake's compilation database.

## Developer tools

Install the following tools yourself before using the matching gate or feature.
They are not downloaded by CPM.

| Tool                        | Used for                                                                                          | When required                                    |
| --------------------------- | ------------------------------------------------------------------------------------------------- | ------------------------------------------------ |
| uv and Python 3.10+         | Run the quality-check script                                                                      | Quality gates                                    |
| CMake 3.25+ and Ninja       | Configure and build                                                                               | Builds and quality gates                         |
| A C++20 compiler            | Build the project                                                                                 | Builds                                            |
| LLVM/Clang                  | `clang++`, `clang-tidy`, `clang-format`, clangd, AddressSanitizer, and UndefinedBehaviorSanitizer | All quality gates                                |
| `llvm-profdata`, `llvm-cov` | Test coverage measurement                                                                         | `fast`, `full`, and the coverage inspection      |
| cppcheck                    | Static analysis                                                                                   | `fast`, `full`, and its inspection               |
| Include-What-You-Use (IWYU) | Include analysis                                                                                  | `full` and its inspection                        |
| Doxygen                     | API documentation                                                                                 | `-DAXIOM_BUILD_DOCS=ON`                          |
| ccache                      | Compiler cache                                                                                    | Optional: `-DAXIOM_USE_CCACHE=ON`                |
| Lizard                      | Cyclomatic-complexity analysis                                                                    | `fast`, `full`, and its inspection               |

`clang-tidy`, `clang-format`, `llvm-profdata`, and `llvm-cov` are distributed as
part of LLVM. `clang-format` is an editor/command-line formatting tool and is not
run automatically by a CMake preset.

The `quality-hardening` configuration uses LLVM's compiler and runtime. On Windows, the build
copies the AddressSanitizer runtime DLL next to the executable when available.

### IWYU and LLVM compatibility

IWYU is tightly coupled to LLVM/Clang internals. Use an IWYU release or branch
that matches the installed Clang version. For example, Clang 22 requires IWYU
0.26 or the `clang_22` branch. The `full` gate and `inspect iwyu` expect
`include-what-you-use` and `iwyu_tool.py` to be available on `PATH`.

Official IWYU releases are source releases. Building it standalone requires an
LLVM development installation that exports `LLVMConfig.cmake`, in addition to
the Clang libraries and headers. The standard Windows LLVM installer used by
this project provides the compiler tools but not that CMake package, so it
cannot build IWYU standalone by itself. Use a full LLVM build/development
package, then build IWYU against it:

```sh
git clone --branch clang_21 https://github.com/include-what-you-use/include-what-you-use.git
cmake -S include-what-you-use -B iwyu-build -G Ninja -DCMAKE_PREFIX_PATH=<llvm-development-prefix> -DCMAKE_INSTALL_PREFIX=<iwyu-install-prefix>
cmake --build iwyu-build
cmake --install iwyu-build
```

Alternatively, build LLVM, Clang, compiler-rt, and IWYU in one tree. The
following reference configuration targets LLVM/Clang 22 and IWYU 0.26; replace
the source and install paths with local paths:

```sh
cmake -S <llvm-source-dir>/llvm -B <llvm-build-dir> -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=<llvm-install-prefix> -DLLVM_TARGETS_TO_BUILD=X86 -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;lld" -DLLVM_ENABLE_RUNTIMES=compiler-rt -DLLVM_EXTERNAL_PROJECTS=iwyu -DLLVM_EXTERNAL_IWYU_SOURCE_DIR=<iwyu-source-dir> -DLLVM_INCLUDE_TESTS=OFF -DCLANG_INCLUDE_TESTS=OFF -DLLVM_INCLUDE_BENCHMARKS=OFF -DLLVM_INCLUDE_EXAMPLES=OFF
cmake --build <llvm-build-dir>
cmake --install <llvm-build-dir>
```

### Lizard complexity analysis

Lizard is a Python-based complexity analyzer for C/C++ that does not require a
complete include graph. Install it with Python 3.10 or later:

```sh
python -m pip install lizard
```

Analyze the project sources and tests with the same thresholds used by the
quality gates:

```sh
lizard -l cpp -C 10 -L 80 src apps tests
```

Use `-C <limit>` to set the cyclomatic-complexity threshold and `-L <limit>`
to set the function-length threshold. Violations produce a non-zero exit status.

### CPM-managed dependencies

CPM downloads only project dependencies and CMake integrations into `.cache/`:

- GoogleTest 1.18.0, when `BUILD_TESTING=ON`;
- CPM.cmake itself when a CPM-managed feature is configured;
- the `cmake-scripts` sanitizer integration when `AXIOM_SANITIZERS` is enabled;
- the `Ccache.cmake` integration when `AXIOM_USE_CCACHE=ON`.

These downloads do not install the compiler, analysis tools, Doxygen, or the
`ccache` executable.

### CMake presets

```sh
cmake --preset quality-fast
cmake --build --preset quality-fast
ctest --preset quality-fast
```

The quality script is the preferred entry point because it combines these
commands with the architecture, complexity, formatting, and analyzer checks.

### Sanitizer demo paths

The demo contains intentional failure paths for validating an already-built
`quality-hardening` configuration:

```sh
uv run --quiet python tools/check.py hardening
./build-quality/hardening/apps/demo/axiom_demo --memory-error
```

On Windows, run `build-quality/hardening/apps/demo/axiom_demo.exe` instead. The
`--memory-error` argument is required; the default demo path is unaffected.

To test UBSan specifically:

```sh
./build-asan-ubsan/apps/demo/axiom_demo --ubsan-error
```

This triggers a signed integer overflow. On Windows, append `.exe` to the
executable path.

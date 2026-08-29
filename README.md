# Axiom

A compact C++20 application framework built with CMake. It provides an
installable `Axiom::Core` library, a small demo executable, integration tests,
and reproducible quality gates. The public core API is deliberately small so
new modules can be added incrementally without expanding the framework surface
prematurely.

[中文文档](README.zh-CN.md)

## Build

```sh
cmake --preset dev
cmake --build --preset dev
```

The demo prints `Axiom`. Use the `debug` preset when you need an unoptimized
debug build:

```sh
cmake --preset debug
cmake --build --preset debug
```

The installed-package consumer test is disabled during normal development because
it performs a separate install, configure, and build. Enable it for release
validation with `-DAXIOM_BUILD_INSTALL_TEST=ON`.

## Use as a CMake package

Install the library from a configured build directory:

```sh
cmake --install build --prefix <install-prefix>
```

Consumers can discover and link the exported target with CMake 3.25 or newer:

```cmake
find_package(Axiom CONFIG REQUIRED)

target_link_libraries(my_target PRIVATE Axiom::Core)
```

The public header is available as:

```cpp
#include <axiom/core/core.hpp>
```

## Project layout

| Path | Purpose |
| ---- | ------- |
| `src/core` | Installable `Axiom::Core` library and its public headers. |
| `apps/demo` | Minimal executable that consumes the core library. |
| `tests` | Unit and installed-package integration tests. |
| `checkflow.json` | Versioned CheckFlow quality-gate flow definitions. |
| `.checkflow/tools` | Axiom-specific architecture, formatting, and mutation-quality Tools. |
| `quality` | Versioned quality profile and architecture policy. |

## Quality gates

Run gates through the globally installed `checkflow` command so agents get one
compact JSON result rather than compiler logs. A non-zero exit code always means
the selected gate failed.

```sh
checkflow fast
checkflow full
checkflow hardening
checkflow doctor
```

Add `-v` or `--verbose` before or after the gate name to print each executed
command, its combined output, and its exit code to stderr. The JSON report stays
on stdout for machine consumption.

`fast` validates architecture, configures and builds `quality-fast`, runs
complexity, cppcheck, clang-tidy, CTest, and LLVM coverage. `full` adds a
non-mutating clang-format check and the installed-package test. `hardening`
runs ASan/UBSan tests plus a separate Mull build with a 90% mutation threshold.
Use `checkflow doctor` to validate the declared tools before executing a flow.

`fast` and `full` build with LLVM source-based coverage instrumentation and fail
when the test suite's line coverage drops below 90%; the report also records
region and branch coverage and writes a compact, agent-oriented
`coverage-export.json` into the build directory. It uses repository-relative
source paths, run-length encoded line hit counts, grouped branch hit counts,
an inline legend for the array fields, and omits fully covered files while
recording their count. Add `--coverage-html` to additionally
render the browsable
`llvm-cov` HTML report into `coverage-html/` next to it.

For diagnosis, without executing a flow:

```sh
checkflow doctor
checkflow doctor fast
checkflow fast --diagnostic
```

The analyzer inspections and the full gate scan every project compilation
unit from CMake's compilation database.

### Gate results, score, and blocking

Each invocation writes one `axiom-quality/v2` JSON report. Every check has a
maximum score and one of four statuses:

| Status | Meaning | Effect on gate and score |
| ------ | ------- | ------------------------ |
| `pass` | The check ran and met its criterion. | Contributes its full score. |
| `fail` | The check ran but its command, threshold, or validation failed. | Fails the gate and contributes zero. |
| `blocked` | A prerequisite check failed, so this check was not run. | Fails the gate and contributes zero. |
| `skipped` | A required external tool is absent or unsupported on this platform. | Is reported to stderr, does not fail the gate, and is excluded from the score denominator. |

Thus `passed` is true only when every recorded check is `pass` or `skipped`.
The total score is the sum of passed checks divided by the sum of all checks
except skipped checks. A gate can therefore pass with a reduced maximum score:
this means an optional capability was not verified, not that it passed. A tool
that starts but returns a non-zero exit code is always a `fail`; it is never
converted into a skip.

The gates are sequential where later checks depend on earlier build artifacts.
A failed configure step blocks the build, tests, coverage, and compilation-
database analyzers; a failed build similarly blocks its dependents. If the
required executable for such a prerequisite is unavailable, both it and its
dependents are instead skipped to avoid running against stale build artifacts.
Independent tools (formatting, complexity, and individual analyzers) are
skipped only for their own missing tools.

The nominal point totals, before skips, are 95 for `fast`, 105 for `full`, and
90 for `hardening`. `fast` checks architecture (10), configure (10), build
(20), complexity (10), cppcheck (10), clang-tidy (10), tests (15), and coverage
(10). `full` additionally checks whole-project formatting. `hardening`
contains a 45-point ASan/UBSan configure-build-test path and a separate 45-point
Mull configure-build-mutation path.

### Architecture rules

[`quality/architecture_rules.json`](quality/architecture_rules.json) is a small,
versioned policy file consumed by the `architecture` check. Its current rule
forbids files below `src/core/` from directly including headers below `apps/` or
`tests/`. This preserves the intended dependency direction: reusable production
code may be used by the demo and tests, but cannot depend on either of them.

For this small project, the rule is a sensible, cheap guard: it is declarative,
runs without a compiler database, scans headers as well as implementation files,
and reports the offending file, line, include, and rule ID. It deliberately
checks only direct textual `#include` directives, however; it does not prove a
complete dependency graph, detect transitive dependencies, or validate every
possible module boundary. As the project grows, add similarly narrow rules for
each stable layer, and use the compilation database or a dedicated dependency
analysis tool when transitive architectural enforcement becomes necessary.

## Developer tools

Install the following tools yourself before using the matching gate or feature.
They are not downloaded by CPM.

| Tool                        | Used for                                                                                          | When required                                    |
| --------------------------- | ------------------------------------------------------------------------------------------------- | ------------------------------------------------ |
| CheckFlow                   | Run the repository quality gates                                                                   | Quality gates                                    |
| CMake 3.25+ and Ninja       | Configure and build                                                                               | Builds and quality gates                         |
| A C++20 compiler            | Build the project                                                                                 | Builds                                            |
| LLVM/Clang                  | `clang++`, `clang-tidy`, `clang-format`, clangd, AddressSanitizer, and UndefinedBehaviorSanitizer | All quality gates                                |
| `llvm-profdata`, `llvm-cov` | Test coverage measurement                                                                         | `fast`, `full`, and the coverage inspection      |
| cppcheck                    | Static analysis                                                                                   | `fast`, `full`, and its inspection               |
| Mull                        | C++ mutation testing                                                                              | `hardening`                                      |
| Doxygen                     | API documentation                                                                                 | `-DAXIOM_BUILD_DOCS=ON`                          |
| ccache                      | Compiler cache                                                                                    | Optional: `-DAXIOM_USE_CCACHE=ON`                |
| Lizard                      | Function complexity, length, and parameter-count thresholds                                       | `fast`, `full`, and its inspection               |

`clang-tidy`, `clang-format`, `llvm-profdata`, and `llvm-cov` are distributed as
part of LLVM. `full` and `inspect format` run non-mutating `--dry-run --Werror`
diagnostics. Their JSON result lists files requiring formatting without embedding
clang-format's line-by-line diagnostic output.

The `quality-hardening` configuration uses LLVM's compiler and runtime. On Windows, the build
copies the AddressSanitizer runtime DLL next to the executable when available. The separate
`quality-mutation` configuration uses Mull's LLVM IR frontend. Matching unversioned tools or pairs such
as `mull-runner-22` and `mull-ir-frontend-22` are detected from `PATH`; Mull must match the LLVM toolchain.
Mull 0.34 does not support native Windows: run the `hardening` gate in WSL, Linux, or macOS. For
this machine, install a Linux distribution for WSL first, then build/install the Mull source there
and run `checkflow hardening` from the Linux checkout of Axiom. The
versioned executable and frontend plugin must use the same LLVM major version as `clang++`.

### Lizard complexity analysis

Lizard is a Python-based complexity analyzer for C/C++ that does not require a
complete include graph. Install it with Python 3.10 or later:

```sh
python -m pip install lizard
```

Analyze the project sources and tests with the same thresholds used by the
quality gates:

```sh
lizard -l cpp -C 10 -L 80 -a 5 src apps tests
```

Use `-C <limit>` to set the cyclomatic-complexity threshold and `-L <limit>`
to set the function-length threshold; `-a <limit>` controls parameter count.
Violations produce a non-zero exit status. These syntax-level quantitative limits
complement clang-tidy: `readability-function-size` retains only nesting depth,
`readability-function-cognitive-complexity` measures human-oriented control-flow
difficulty, and `misc-include-cleaner` performs semantic include diagnostics.

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

CheckFlow is the preferred entry point because it combines these
commands with the architecture, complexity, formatting, and analyzer checks.

### Gate validation

Run `checkflow doctor` after changing `checkflow.json` or a
project Tool, then run `fast`, `full`, or `hardening` as appropriate.

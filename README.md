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
| `tools/check.py` | JSON-reporting quality-gate entry point. |
| `quality` | Versioned quality profile and architecture policy. |

## Quality gates

Run gates through uv so agents get one compact JSON result rather than compiler
logs. A non-zero exit code always means the selected gate failed.

```sh
uv run --quiet python tools/check.py fast
uv run --quiet python tools/check.py full
uv run --quiet python tools/check.py hardening
```

### Reusable check fixture

[`tools/check_test`](tools/check_test/README.md) is a standalone C++20/CMake
demo project that exercises the public `check.py` contract.  It has its own
source, tests, CMake presets, and architecture policy; it does not link to
Axiom.  Use it to verify the Python report parsers and interception semantics
without changing Axiom sources:

```sh
uv run --quiet python tools/check_test/verify.py \
  --report build-quality/reports/check-test-integration.json
uv run --quiet python tools/check.py --project-root tools/check_test fast
```

`--project-root` makes command execution, source discovery, build artifacts,
and reports relative to the selected CMake project. Each project owns a
`quality/check_profile.json` that declares its source roots, preset names,
build directories, policy path, cache options, schemas, and quality thresholds.
The fixture intentionally uses names different from Axiom. It runs clean
`full` and `hardening` baselines, then compiles isolated real failure sources
for every supported tool, including ASan and UBSan. The final report separates
supported tools, detected failures, clean passes, unsupported capabilities, and
unexpected failures. Use `--quick` when only the dependency-free contract suite
is needed.

Add `-v` or `--verbose` before or after the gate name to print each executed
command, its combined output, and its exit code to stderr. The JSON report stays
on stdout for machine consumption.

Use `--report <path>` when a later agent needs the same immutable report file.
`fast` applies complexity, cppcheck, and clang-tidy only to source files
recompiled by its incremental build. Its inexpensive architecture rule scan
remains whole-project so header-only violations are not missed. Formatting is
intentionally deferred to `full`, which reports only the source files requiring
clang-format without modifying them. `full` also runs cppcheck and clang-tidy over
every project compilation unit. `hardening` builds and
runs the test suite with AddressSanitizer and UndefinedBehaviorSanitizer enabled,
then uses a separate instrumented build and requires a Mull mutation score of at
least 90 for project source code. Mull's Mutation Testing Elements report is
written under `build-quality/mutation/mull/`.

`fast` and `full` build with LLVM source-based coverage instrumentation and fail
when the test suite's line coverage drops below 90%; the report also records
region and branch coverage and writes a compact, agent-oriented
`coverage-export.json` into the build directory. It uses repository-relative
source paths, run-length encoded line hit counts, grouped branch hit counts,
an inline legend for the array fields, and omits fully covered files while
recording their count. Add `--coverage-html` to additionally
render the browsable
`llvm-cov` HTML report into `coverage-html/` next to it.

For diagnosis only, without claiming a gate passed:

```sh
uv run --quiet python tools/check.py inspect format
uv run --quiet python tools/check.py inspect tests --preset quality-fast
uv run --quiet python tools/check.py inspect coverage
uv run --quiet python tools/check.py inspect cppcheck
uv run --quiet python tools/check.py inspect clang-tidy
uv run --quiet python tools/check.py --list
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
| uv and Python 3.10+         | Run the quality-check script                                                                      | Quality gates                                    |
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
and run `uv run --quiet python tools/check.py hardening` from the Linux checkout of Axiom. The
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

The quality script is the preferred entry point because it combines these
commands with the architecture, complexity, formatting, and analyzer checks.

### Gate regression tests

Intentional analyzer and orchestration failures live in `tools/check_test`, not
in production demo sources. Run `python tools/check_test/verify.py` to validate
the negative cases, then run `python tools/check.py full` for the real-project
baseline.

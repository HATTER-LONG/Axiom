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
intentionally deferred to `full`, which also runs cppcheck, clang-tidy, and
Include-What-You-Use over every project compilation unit. `hardening` builds and
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
uv run --quiet python tools/check.py inspect iwyu
uv run --quiet python tools/check.py --list
```

The two analyzer inspections and the full gate scan every project compilation
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

The nominal point totals, before skips, are 95 for `fast`, 125 for `full`, and
90 for `hardening`. `fast` checks architecture (10), configure (10), build
(20), complexity (10), cppcheck (10), clang-tidy (10), tests (15), and coverage
(10). `full` additionally performs whole-project formatting and IWYU. `hardening`
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
| Include-What-You-Use (IWYU) | Include analysis                                                                                  | `full` and its inspection                        |
| Mull                        | C++ mutation testing                                                                              | `hardening`                                      |
| Doxygen                     | API documentation                                                                                 | `-DAXIOM_BUILD_DOCS=ON`                          |
| ccache                      | Compiler cache                                                                                    | Optional: `-DAXIOM_USE_CCACHE=ON`                |
| Lizard                      | Cyclomatic-complexity analysis                                                                    | `fast`, `full`, and its inspection               |

`clang-tidy`, `clang-format`, `llvm-profdata`, and `llvm-cov` are distributed as
part of LLVM. `clang-format` is an editor/command-line formatting tool and is not
run automatically by a CMake preset.

The `quality-hardening` configuration uses LLVM's compiler and runtime. On Windows, the build
copies the AddressSanitizer runtime DLL next to the executable when available. The separate
`quality-mutation` configuration uses Mull's LLVM IR frontend. Matching unversioned tools or pairs such
as `mull-runner-22` and `mull-ir-frontend-22` are detected from `PATH`; Mull must match the LLVM toolchain.
Mull 0.34 does not support native Windows: run the `hardening` gate in WSL, Linux, or macOS. For
this machine, install a Linux distribution for WSL first, then build/install the Mull source there
and run `uv run --quiet python tools/check.py hardening` from the Linux checkout of Axiom. The
versioned executable and frontend plugin must use the same LLVM major version as `clang++`.

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

### Gate regression tests

Intentional analyzer and orchestration failures live in `tools/check_test`, not
in production demo sources. Run `python tools/check_test/verify.py` to validate
the negative cases, then run `python tools/check.py full` for the real-project
baseline.

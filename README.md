# Axiom

A compact C++20 application framework built with CMake. Its current core is an
installable synchronous capability runtime: typed C++ callables are registered
as described `Module`/`Action` entries, discovered through metadata, and invoked
through the dynamic `Value` boundary with structured `Result` errors. It also
ships a small demo executable, tests, and reproducible quality gates.

[中文文档](README.zh-CN.md)

## Build

```sh
cmake --preset dev
cmake --build --preset dev
```

The demo walks through Core actions, logging, and resource behavior. `debug` uses
an unoptimized build. Both development presets build tests; run them with
`ctest --preset dev` or `ctest --preset debug`.

Core is static by default. To select a shared library, use a separate build
with `-DBUILD_SHARED_LIBS=ON`; the demo always links `Axiom::Core`.
`BUILD_TESTING=OFF` disables tests. Axiom does not enable CTest when embedded
with `add_subdirectory()`.

Installation testing is opt-in (`AXIOM_BUILD_INSTALL_TEST=ON`) and requires an
uninstrumented build. The test installs and relocates the package, then configures,
builds and runs a standalone consumer with the same toolchain and configuration.

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

## Core runtime model

The public API is centered on `Value`/`Arguments` for ordered dynamic values,
`ModuleBuilder` for staging typed callable Actions, `Runtime` for module
registration, discovery and synchronous invocation, and `Error`/`Result<T>` for
structured failures at the boundary. Action identifiers use canonical
`module.action` syntax, with lowercase ASCII letters, digits, and underscores
in each component.

Supported callable conversions include `bool`, signed integers, floating-point
values, `std::string`, recursive `std::vector<T>`, and string-keyed
`std::map`/`std::unordered_map`. Integer inputs may widen to floating point;
lossy and implicit string conversions are rejected. Optional parameters use
validated `param(..., default_value)` values. The runtime is intentionally not
thread-safe: registration, discovery, and invocation must not run concurrently.
Core does not provide protocol adapters, JSON serialization, networking, plugins,
authorization, or asynchronous execution.

## Project layout

| Path | Purpose |
| ---- | ------- |
| `src/core` | Installable `Axiom::Core` library and public headers. |
| `apps/demo` | Application consuming the same Core target as other clients. |
| `tests` | GoogleTest suites and installed-package consumer. |
| `cmake` | Target policy, runtime deployment and build verification. |
| `checkflow.json` | Quality flow ordering and thresholds (the single policy source). |
| `CMakePresets.json` | Compiler, library kind, instrumentation and build directories. |
| `.checkflow/tools` | Architecture and non-mutating source-format adapters. |
| `quality/architecture_rules.json` | Direct-include dependency policy. |

## Quality gates

Use the globally installed `checkflow`; no project-local Python environment is required.

```sh
checkflow doctor
checkflow fast
checkflow hardening
checkflow full
```

| Flow | Checks |
| ---- | ------ |
| `fast` | Architecture, incremental static build, GoogleTest, LLVM coverage and mapping integrity. |
| `full` | Clean static coverage build, architecture, formatting, complexity, cppcheck, clang-tidy, plus uninstrumented static/shared tests and installed consumers. |
| `hardening` | Static ASan/UBSan tests, including a UBSan termination regression, then a separate static Mull build and report-integrity check. |

Coverage must reach **90% for each of lines, regions and branches**. The test executable
links the entire static Core archive so unreferenced translation units cannot disappear
from the denominator. CheckFlow discovers the executable through CTest; no platform
filenames are embedded in the flow. An additional check rejects LLVM diagnostics
(including mismatched profiles) and missing Core implementation files. Failed coverage
produces `coverage-export.json` in the build directory.

Mutation testing must reach **90%** and its report must include implementation mutants
under `src/core/src`, not only instantiated headers. Coverage and Mull intentionally
require static Core. The independent `quality-shared` preset validates the DLL boundary
using the existing tests; it uses Ninja Multi-Config and Release to exercise configuration
selection during installation. Internal Registry/Dispatcher tests compile their private
implementations locally in shared builds; public Runtime tests still execute the DLL.

UBSan is configured to stop the process on a finding. Sanitizer availability and the
selected combination are checked at configure time; unsupported combinations fail.
MemorySanitizer also needs an appropriately instrumented dependency/standard-library
toolchain; a successful compiler probe alone does not guarantee runtime usability.

Commands return compact JSON reports. `--verbose` reports progress; `--diagnostic`
includes raw execution details. A failed prerequisite prevents its dependent checks
from running. Inspect skipped/unavailable tools: a skipped capability is not verified.
Reports use the installed CheckFlow schema; there is no separate repository point-score
system or secondary quality profile.

For explicit build/test runs:

```sh
cmake --preset quality-shared
cmake --build --preset quality-shared
ctest --preset quality-shared
```

Build parallelism is not fixed by the repository. Use `CMAKE_BUILD_PARALLEL_LEVEL`,
`cmake --build ... --parallel N`, or a local `CMakeUserPresets.json`.

## Build policy and dependencies

The public target requires C++20. Strict standard conformance and baseline warnings
are always enabled for Axiom-owned targets. `AXIOM_WARNINGS_AS_ERRORS` defaults on for
standalone builds and off when embedded. Standard CMake analyzer/launcher settings
are respected; Axiom never deletes caller-owned analyzer cache entries. Optional examples:

```sh
cmake --preset dev -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
cmake --preset dev -DCMAKE_CXX_CLANG_TIDY=clang-tidy
cmake --preset dev -DAXIOM_BUILD_DOCS=ON
cmake --build --preset dev --target docs
```

The former `AXIOM_BUILD_TESTS`, `AXIOM_STRICT_LANGUAGE_MODE`, `AXIOM_STATIC_ANALYZERS`
and `AXIOM_USE_CCACHE` switches were removed. Use `BUILD_TESTING`, the fixed conformance
policy, standard CMake analyzer variables and `CMAKE_CXX_COMPILER_LAUNCHER` respectively.
Recreate old build directories when migrating away from historical global tool flags.

CPM pins spdlog 1.17.0 and GoogleTest 1.18.0. spdlog and bundled fmt are private
header dependencies; Core retains their `Threads::Threads` requirement. GoogleTest
is test-only and explicitly static, regardless of Core's library kind. No sanitizer
or ccache CMake integration is downloaded.

CPM honors `CPM_SOURCE_CACHE` from CMake or the environment. Defaults are
`%LOCALAPPDATA%/CPM` on Windows and `$XDG_CACHE_HOME/CPM` or `~/.cache/CPM` on Unix;
without a user cache location, CPM uses its build-tree fallback. Compiler and analysis
tools are installed separately, not downloaded by CPM.

Core uses hidden visibility with explicit API exports. PIC remains enabled for static
Core so consumers can embed it in a shared object. During the 0.x development series,
the shared-library ABI identity is major.minor (currently 0.1); package compatibility
is limited to the same minor version. Binary consumers must use a compatible C++ ABI,
standard library and runtime. No cross-toolchain ABI stability is promised.

## Developer tools

| Tool | Required for |
| ---- | ------------ |
| CMake 3.25+, Ninja, C++20 compiler | Builds; shared validation also uses Ninja Multi-Config. |
| CheckFlow | Quality orchestration. |
| Clang, llvm-profdata, llvm-cov | Quality builds and coverage. |
| clang-format, clang-tidy, cppcheck, Lizard | Full static checks. |
| Mull matching the Clang major version | Mutation testing in hardening. |
| Doxygen | Optional API documentation. |
| ccache | Optional standard compiler launcher. |

Mull support is platform-dependent; use a supported Linux/macOS environment (or WSL)
for the mutation gate. On Windows, sanitizer builds locate the runtime using the selected
Clang driver and target architecture, and fail if its required DLL is missing.

The architecture policy checks direct textual includes in source, application and test
headers/implementations. It enforces declared layer boundaries but is not a transitive
C++ dependency analysis. See `quality/architecture_rules.json` for the actual rules.

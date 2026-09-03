# Axiom

A compact C++20 application infrastructure library built with CMake. Its current
core models application capabilities as described `Module`/`Action` entries,
invoked synchronously through the dynamic `Value` boundary with structured
`Result` errors. Typed host resources, structured logging, events, asynchronous
execution and tracked tasks provide the surrounding runtime infrastructure, while
`IntrospectionService` exposes owning read-only discovery across Actions,
Resources and Tasks. It also ships a small demo executable, tests, and
reproducible quality gates.

## Build

```sh
cmake --preset dev
cmake --build --preset dev
```

The demo prints the framework name, completes an answer task and cancels a
long operation at a deterministic batch boundary. `debug` uses an unoptimized build. Both
presets build tests; run them with `ctest --preset dev` or `ctest --preset debug`.
Run `build/apps/demo/axiom_demo` (`axiom_demo.exe` on Windows) for the demo.

Axiom library is static by default. To select a shared library, use a separate build
with `-DBUILD_SHARED_LIBS=ON`; the demo always links `Axiom::Axiom`.
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

target_link_libraries(my_target PRIVATE Axiom::Axiom)
```

The public header is available as:

```cpp
#include <axiom/axiom.hpp>
```

## Axiom library runtime model

The public API is centered on `Value`/`Arguments` for ordered dynamic values,
`ModuleBuilder` for staging typed callable Actions, `Runtime` for module
registration, discovery and synchronous invocation, and `Error`/`Result<T>` for
structured failures at the boundary. `ActionOptions` supplies Action `version`,
`tags`, and `metadata` at registration without additional `add()` overloads.
Action identifiers use canonical
`module.action` syntax, with lowercase ASCII letters, digits, and underscores
in each component.

Supported callable conversions include `bool`, signed integers, floating-point
values, `std::string`, recursive `std::vector<T>`, and string-keyed
`std::map`/`std::unordered_map`. Integer inputs may widen to floating point;
out-of-range integer narrowing and implicit string conversions are rejected.
Floating-point conversions follow C++ numeric conversion semantics. Optional parameters use
validated `param(..., default_value)` values.

Runtime registration, discovery, lookup and invocation support concurrent use.
Registration commits are serialized and atomically publish immutable registry
states; discovery and invocation operate from acquired snapshots. Runtime never
holds a registration or discovery lock while executing user code. Invocations of
the same Action may overlap and share the single stored callable instance, so the
callable and any mutable state it captures must provide their own synchronization.
Runtime destruction still requires caller synchronization with concurrent access.

Axiom library provides a protocol-independent Command boundary through
`command::CommandDispatcher`. A CPython adapter exposes the same nine methods;
other protocol adapters (MCP, HTTP, and so on) are not provided yet. Independent
`async::Executor` and `async::Scheduler` services provide asynchronous work;
Action invocation itself remains synchronous.

## Runtime discovery and introspection

`axiom::introspection::IntrospectionService` is a read-only, non-owning aggregation
layer over an existing `Runtime`, `resource::ResourceRegistry` and
`task::TaskRegistry`. It provides owning copies of Module, Action, Resource and
Task descriptors, plus `ActionQuery` / `ResourceQuery` / `TaskQuery` filters and a
combined `RuntimeSnapshot`. Existing `actions(module)` and `resources(type)`
overloads delegate to those Query values. Snapshot always contains every
discoverable object and does not accept a Query.

`logging::LogQuery` can additionally filter retained records by exact `request_id`,
`trace_id`, `action_id` (the reserved log field `action`), and `task_id`. Non-empty
conditions are AND-ed; `limit` still applies after filtering.

The service is uncached and does not own registry state. Its three sources must
outlive it, and source destruction must not overlap a query. A combined snapshot
samples Actions, Resources and Tasks in a fixed sequence and is therefore not a
globally atomic snapshot across all sources.

## Command dispatch

`axiom::command::CommandDispatcher` is the closed, protocol-independent dynamic
boundary for external callers. It is constructed from an existing `Runtime`,
`resource::ResourceRegistry`, and `task::TaskRegistry`, and it creates an internal
`IntrospectionService` over those same sources. The dispatcher does not own them;
they must outlive it, and their destruction must not overlap `dispatch()`.

```cpp
axiom::command::CommandDispatcher dispatcher{runtime, resources, tasks};
auto listed = dispatcher.dispatch(axiom::command::action_list, {}, {});
```

The nine MVP methods are `action.list`, `action.describe`, `action.invoke`,
`resource.list`, `resource.describe`, `task.list`, `task.describe`, `task.cancel`,
and `system.snapshot`. Command structure is validated strictly before any source
call. Unknown methods return `ErrorCode::UnknownCommand`. Successful `*.list` and
`*.describe` results are owned Values; `action.invoke` returns the Runtime Value
unchanged; `task.cancel` returns null. `system.snapshot` is still a sequential
observation, not a globally atomic snapshot.

## Python binding

The `axiom` Python package exposes the same nine Command methods through a typed
facade over the compiled `axiom._axiom` extension. Python never owns the Core:
a C++ embedding application creates its `Runtime`, `ResourceRegistry` and
`TaskRegistry`, wraps the combination in a revocable
`axiom::python::HostBridge`, and attaches one `HostHandle`:

```cpp
axiom::python::HostBridge bridge{runtime, resources, tasks};
// pass bridge.attach() to Python once; Host._attach(handle) wraps it
```

`Host.dispatch(method, params, context)` is the single stable entry; `Host.actions`,
`Host.resources`, `Host.tasks` and `Host.snapshot()` merely assemble Commands.
Host closed or expired sessions fail with `AxiomHostClosedError`; Core `Result`
failures map to `AxiomError` with stable lowercase `code`, `message`, `path` and
`details`; Python/Value conversion failures raise `AxiomConversionError(ValueError)`.
Support matrix, host lifetime contract, build, wheel and relocation verification
are documented in `docs/python-binding-usage.md` and
`docs/python-binding-support-matrix.md`.

## Tracked asynchronous tasks

`axiom::task::TaskRegistry` adds identity, progress, cooperative cancellation,
repeatable result reads and change notifications to work submitted through an
existing `async::Executor`. It is independent of the synchronous Action runtime.
A callable takes `task::TaskContext&` and returns `Result<T>`; `T` may be a
copyable value or `void`. Move-only callables are supported.

```cpp
axiom::async::Executor executor{1};
axiom::task::TaskRegistry tasks;
auto submitted = tasks.submit(executor, "answer", [](axiom::task::TaskContext&) {
    return axiom::Result<int>::success(42);
});
if (submitted) {
    auto handle = submitted.value();
    executor.close(); // This example drains work; result() itself never waits.
    auto result = handle.result();
    if (result && result->hasValue()) {
        const int answer = result->value();
        // Use answer (42).
    }
    auto removed = tasks.remove(handle.id());
    // The handle remains usable after removal.
}
```

Keep the RAII subscription returned by `tasks.onChanged(callback)` alive to
receive Running, progress and terminal descriptor snapshots. Pending tasks are
queryable but do not emit a creation event. A callback may query or cancel tasks
and disconnect itself; callbacks for different tasks may run concurrently.
Observer exceptions are isolated and diagnosed. Disconnection and Registry
destruction do not wait for callbacks already captured for delivery.

`handle.cancel()` cancels queued work before it starts or requests cooperation
from running work. Running callables check their context's cancellation token
and return an error with `ErrorCode::Cancelled` to acknowledge cancellation.
A successful return still completes successfully after a cancellation request.
Repeated cancellation and cancellation of terminal tasks have no effect.
Progress accepts finite values in [0, 1], owns its message and may move backwards;
successful completion sets its value to 1. Invalid progress throws
`std::invalid_argument`.

`describe(id)` returns one coherent snapshot; `list()` returns snapshots sorted
by canonical ID text (`task:<nonzero serial>`). Only terminal tasks can be removed.
Registries retain terminal tasks until removal or Registry destruction. Handles
and accepted work survive Registry destruction, which never cancels or waits.
Callers still own the lifetime of references captured by business callables and
observers. Use a Registry constructed with a Logger to associate same-service,
same-thread business logs with `task_id` and `task_name`. Submit with
`task::TaskSubmission` to copy an optional immutable `TaskOrigin`; non-empty
origin fields are also bound as `request_id`, `trace_id`, `caller`, `action`,
and a `module` component only when `action_id` is canonical `module.action`
text. Name-only `submit` remains available and records unknown origin.

Inside the callable, use `context.reportProgress(0.5, "Batch complete")` and
`context.cancellation().requested()`. The runnable example in
`apps/demo/task_demo.cpp` uses promises to pause at a work boundary, observes
progress, requests cancellation, reads the terminal result and removes both
tasks by ID. It requires neither polling nor timing-dependent sleeps.

## Project layout

| Path | Purpose |
| ---- | ------- |
| `include/axiom`, `src` | Public headers and implementations of `Axiom::Axiom`; events are header-only. |
| `include/axiom/python`, `src/python` | CPython adapter contract (`HostBridge`), `_axiom` extension, conversion and error translation. |
| `python` | Pure-Python facade package, typing information, wheel project and facade tests. |
| `apps/demo` | Application consuming the same Axiom library target as other clients. |
| `tests` | GoogleTest suites, installed-package consumer and embedded-interpreter suites. |
| `docs/architecture` | Architecture, dependency and lifetime contracts. |
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
| `fast` | Architecture, incremental static build, GoogleTest, LLVM coverage and mapping integrity, then `uv sync` and the shared Python adapter build with embedding/pytest/mypy/ruff only. |
| `full` | Clean static coverage build, architecture, formatting, complexity, cppcheck, clang-tidy, uninstrumented static/shared tests and installed consumers, then the `quality-wheel` Python build (cppcheck/clang-tidy over the adapter, wheel build/install/relocation/import verification). |
| `hardening` | Static ASan/UBSan tests (including HostBridge lifecycle and concurrency), then a separate static Mull build and report-integrity check. |

The Python gates use the uv-managed interpreter at `python/.venv` (`uv sync
--project python --python 3.12 --frozen`). It must provide `pytest`,
`pytest-cov`, `ruff`, `mypy`, and `pybind11`; the wheel flow additionally uses
`scikit-build-core`. `checkflow fast` runs only `axiom.python_*` after Core
GoogleTest has already passed. Python facade coverage must reach **90%**
statements and branches through `axiom.python_facade`.

Coverage must reach **90% for each of lines, regions and branches**. The test executable
links the entire static Axiom library archive so unreferenced translation units cannot disappear
from the denominator. CheckFlow discovers the executable through CTest; no platform
filenames are embedded in the flow. An additional check rejects LLVM diagnostics
(including mismatched profiles) and missing Axiom library implementation files. Failed coverage
produces `coverage-export.json` in the build directory.

Mutation testing must reach **70%** and its report must include implementation mutants
under this repository’s `src/`, not only instantiated headers or dependency files. Coverage and Mull intentionally
require static Axiom library. The independent `quality-shared` preset validates the DLL boundary
using the existing tests; it uses Ninja Multi-Config and Release to exercise configuration
selection during installation. Internal Registry/ActionInvoker tests compile their private
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
header dependencies; Axiom library retains their `Threads::Threads` requirement. GoogleTest
is test-only and explicitly static, regardless of Axiom library's library kind. No sanitizer
or ccache CMake integration is downloaded.

CPM honors `CPM_SOURCE_CACHE` from CMake or the environment. Defaults are
`%LOCALAPPDATA%/CPM` on Windows and `$XDG_CACHE_HOME/CPM` or `~/.cache/CPM` on Unix;
without a user cache location, CPM uses its build-tree fallback. Compiler and analysis
tools are installed separately, not downloaded by CPM.

Axiom library uses hidden visibility with explicit API exports. PIC remains enabled for static
Axiom library so consumers can embed it in a shared object. During the 0.x development series,
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

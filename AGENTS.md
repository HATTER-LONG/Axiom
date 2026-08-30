# Agent Guide

Build in small, verifiable increments. Reuse existing design, keep public
interfaces small, and never weaken a quality check to make it pass.

## Design and scope

- Prefer **thin interfaces and deep modules**. A public interface should expose only
  the operations, data, and failure modes callers truly need; its implementation
  should absorb coordination, representation choices, caching, and platform detail.
- Judge a module by the complexity it removes from its callers, not by its line
  count. Keep related policy and state behind one cohesive owner rather than making
  callers sequence several shallow helpers correctly.
- Add a public type, method, option, callback, or template parameter only for a
  current caller-facing requirement. Prefer private helpers, value types, and
  implementation details over knobs that reveal internal decisions.
- Public APIs must be minimal, stable, clear, and hide internal complexity. Define
  ownership, lifetime, thread-safety, ordering, error, and no-op contracts at the
  boundary; do not require callers to know internal states or call sequences.
- Avoid leaky abstractions: do not expose storage, third-party types, platform
  handles, mutable internal collections, or implementation-specific error details
  unless the module is explicitly an adapter boundary.
- Put behavior in the module that owns it; keep dependencies one-way: Core must not
  depend on UI, libraries on applications, or production on tests.
- Prefer RAII, explicit ownership, and composition. Do not expose third-party
  implementation types outside an adapter boundary.
- Change implementation before expanding a public API. Avoid speculative abstractions,
  catch-all modules such as `utils`, `common`, or `manager`, and unrelated refactors.
- Before editing, identify the owning module, existing applicable interface,
  necessary API changes, preserved behavior, and tests that prove the change.

## Cross-platform support

Windows, Linux, and macOS are supported targets. Platform-specific code must be
isolated behind a small portability boundary and selected with CMake platform or
compiler predicates (`WIN32`, `APPLE`, `UNIX`, `MSVC`) rather than host assumptions.

- Use standard C++ facilities first; do not add OS headers or APIs unless required.
- Shared-library public symbols use the module export macro. `TESTLIB_CORE_API`
  maps to `__declspec(dllexport/dllimport)` on Windows and default visibility on
  Linux/macOS. Never expose a Windows-only declaration unguarded.
- Keep the static-library default working. When changing build or install behavior,
  also verify `-DBUILD_SHARED_LIBS=ON` and an installed-package consumer.
- Prefer CMake target properties and generator expressions over hard-coded paths,
  shell commands, or file extensions. Treat path separators, executable suffixes,
  dynamic-library loading, text encodings, and runtime library deployment as
  platform differences that require an explicit design and test.

## Implementation and tests

- Implement the smallest correct solution. Keep control flow, state, error handling,
  ownership, and lifetimes explicit; never silently swallow failures without a contract.
- Unit and integration tests use GoogleTest. New behavior needs a test; bug fixes need
  a regression test. Tests must be deterministic, independent, and externally focused.
- Prefer `EXPECT_*`; use `ASSERT_*` only when later test code needs the condition.

## Documentation

Public headers document each public type, free function, and public method using
Doxygen comments that describe its contract. Document non-obvious invariants,
ownership, lifetime, ordering, and state transitions; comments explain why, not
obvious control flow. Keep documentation synchronized with behavior.

```cpp
/**
 * @brief Returns the library ABI version encoded by this build.
 * @return Positive version integer for the current Core ABI.
 */
[[nodiscard]] int version();
```

Use `@file` and `@brief` in public headers and add `@tparam`, `@param`, `@return`,
`@throws`, `@pre`, `@post`, `@note`, or `@warning` whenever they describe a
meaningful part of the contract.

## Quality and workspace

Run commands from the repository root with globally installed `checkflow`; do not
create a project-local Python environment. Run `fast` after each verifiable step,
`hardening` after a substantial implementation, and `full` before delivery.
Inspect and fix the root cause of every failure; never use suppressions, exclusions,
disabled tests, or weaker rules as a workaround. Architecture policy is in
`quality/architecture_rules.json`.

Before modifying files, run `git status --short`. Preserve unrelated changes.
Agents may read and modify `~/.cache/CPM` only as CPM's persistent dependency cache;
do not treat it as project source or delete its contents wholesale.

## Done

Deliver only when the requested behavior is implemented in the correct module, tests
and documentation match it, dependencies and ownership remain clear, and all required
quality gates pass.

# Agent Guide

Build in small, coherent, verifiable increments. Reuse existing design, keep
interfaces small, preserve unrelated behavior, and never weaken, bypass, or
reconfigure validation merely to make a change pass.

Examples clarify intent only; they are not implementation templates.

## 1. Scope

- Implement only what the current task requires.
- Prefer the smallest correct change over broad refactoring.
- Preserve unrelated behavior and compatibility unless explicitly changed.
- Before editing, identify:
  - the owning module;
  - the existing interface or extension point;
  - required externally visible changes;
  - preserved behavior;
  - relevant tests.
- Avoid speculative abstractions, unrelated cleanup, and future-oriented
  extension points.

### Example

```text
Good:
Add the requested filter to the existing query path and update focused tests.

Avoid:
Redesign the query subsystem, rename nearby APIs, and introduce a generic
expression framework while adding one filter.
```

## 2. Design

Prefer **thin interfaces and deep modules**.

- Keep coordination, state, representation, policy, and platform detail inside
  the module that owns them.
- Prefer changing implementation over expanding a public API.
- Add public types, methods, options, callbacks, or template parameters only for
  current caller-facing requirements.
- Make ownership, lifetime, errors, ordering, and thread-safety clear at module
  boundaries.
- Avoid exposing third-party types, storage details, platform handles, mutable
  internals, or implementation-specific errors outside adapter boundaries.
- Keep dependencies directional and avoid cycles.
- Prefer RAII, explicit ownership, value semantics where appropriate, and
  composition.
- Avoid vague catch-all abstractions such as `utils`, `common`, or `manager`.

### Example

Prefer:

```cpp
class Service {
public:
    Result execute(const Request& request);
};
```

when validation, coordination, and internal state can remain hidden.

Avoid requiring callers to manually perform internal phases:

```cpp
service.prepare();
service.validate();
service.execute();
service.finalize();
```

unless those phases are intentionally part of the public contract.

## 3. Cross-platform

When multiple platforms are supported:

- prefer standard language and library facilities first;
- isolate platform-specific behavior behind a small portability boundary;
- keep platform headers and native types out of portable public interfaces;
- use build-system platform/compiler predicates instead of host assumptions;
- avoid hard-coded paths, separators, file extensions, shell behavior, or
  deployment assumptions.

### Example

Prefer a portable API:

```cpp
std::uint64_t currentThreadId();
```

with platform-specific implementation hidden internally.

Avoid exposing `HANDLE`, `pthread_t`, or another native type unless the interface
is explicitly a platform adapter.

## 4. Implementation and Tests

- Implement the smallest complete solution that satisfies the contract.
- Keep control flow, ownership, state transitions, lifetime, and error handling
  explicit.
- Reuse existing abstractions when they already fit.
- Do not silently discard failures unless that behavior is intentional.
- Keep changes localized to the owning module.
- Avoid mixing behavior changes with unrelated renaming, formatting, or
  restructuring.

For tests:

- new behavior needs tests;
- bug fixes should have regression tests;
- prefer externally meaningful behavior over implementation details;
- tests must be deterministic and independent;
- cover important boundaries and failure behavior;
- never weaken or remove tests merely to make a change pass.

### Example

Prefer testing:

```text
Given a query with filter X,
only matching results are returned.
```

over asserting that a private helper was called a specific number of times.

## 5. Documentation

Keep documentation synchronized with externally visible behavior.

Public headers should document public types, free functions, constructors, and
public methods with Doxygen comments describing their contract.

Document meaningful details when relevant:

- purpose;
- parameters and return behavior;
- ownership and lifetime;
- errors and exceptions;
- ordering and thread-safety;
- important preconditions, postconditions, or invariants.

Use `@file` and `@brief` in public headers.

Use `@tparam`, `@param`, `@return`, `@throws`, `@pre`, `@post`, `@note`, or
`@warning` whenever they describe a meaningful part of the contract.

### Example

```cpp
/**
 * @brief Returns the library ABI version encoded by this build.
 * @return Positive version integer for the current ABI.
 */
[[nodiscard]] int version();
```

For an interface with parameters and ownership:

```cpp
/**
 * @brief Registers a handler under the given identifier.
 * @param id Identifier used for registration.
 * @param handler Handler whose ownership transfers on success.
 * @return Success or an error describing why registration failed.
 */
Result registerHandler(Id id, std::unique_ptr<Handler> handler);
```

Comments should explain contracts, intent, or non-obvious constraints rather
than restating obvious code.

Avoid:

```cpp
// Increment index.
++index;
```

Do not add verbose comments to self-explanatory private implementation.

## 6. Quality

Use repository-defined `checkflow` workflows when available.

Typical levels:

```text
fast       focused development validation
hardening  broader semantic/integration validation
full       final delivery validation
```

- Run `fast` after a coherent implementation increment.
- Run `hardening` after substantial or integrated semantic changes.
- Run `full` on the final delivery candidate.
- Do not rerun weaker validation on unchanged code when a stronger Gate already
  covered it.
- Diagnose and fix the root cause of failures.
- Treat repository quality configuration as part of the project contract, not
  as something that may be adjusted merely to accommodate the current change.

### Quality Gate Integrity

Quality Gates must not be bypassed, weakened, narrowed, or reconfigured merely
to make an implementation pass.

Unless the current task explicitly requires a quality-policy change, do not:

- disable, skip, remove, or conditionally bypass tests or validation steps;
- reduce warning levels, enabled checks, sanitizer coverage, thresholds, or
  failure severity;
- change `checkflow` workflows to omit failing tools or stages;
- add or broaden excluded directories, ignored paths, file globs, header
  filters, source filters, or generated-file classifications to hide relevant
  project code from validation;
- remove files from compilation databases or validation inputs to avoid
  diagnostics;
- alter test discovery, test selection, labels, or filters merely to exclude a
  failing test;
- mark failing tests as disabled, skipped, expected-failure, flaky, or
  platform-specific without a real semantic reason;
- introduce compiler pragmas, warning-disable flags, analyzer configuration, or
  equivalent suppression mechanisms merely to silence valid diagnostics;
- add blanket static-analysis suppressions such as `NOLINT`,
  `NOLINTBEGIN`/`NOLINTEND`, `clang-tidy` exclusions, IWYU pragmas, or similar
  annotations merely to make a Gate pass;
- add `NOLINT` or equivalent suppressions to production code unless the
  diagnostic is demonstrably inapplicable, the underlying issue cannot
  reasonably be expressed without the suppression, and the suppression is
  narrow and justified;
- change formatting, complexity, coverage, mutation-testing, lint, or static
  analysis thresholds to accommodate newly introduced violations;
- weaken assertions, expected values, test coverage, or failure checks so that
  incorrect behavior becomes accepted;
- replace a failing validation command with a weaker command that does not
  provide equivalent coverage.

Suppressions are exceptions, not fixes.

When a suppression is genuinely required:

- prefer fixing the code first;
- scope the suppression to the smallest possible statement or declaration;
- name the specific diagnostic being suppressed;
- document why the diagnostic is a false positive or why the construct is
  intentionally required;
- do not use broad wildcard suppressions when a specific suppression exists;
- do not propagate a local exception into repository-wide configuration.

Test code may use narrowly scoped suppressions when required by the testing
framework or by intentionally unusual test constructs, but this must not be used
to hide defects in production behavior.

Changes to quality infrastructure itself — including `.clang-tidy`, compiler
warning flags, sanitizer configuration, CheckFlow configuration, exclusion
lists, coverage thresholds, mutation thresholds, or test discovery rules —
must be treated as explicit task scope. Such changes require an independent
technical justification and must not be introduced as a side effect of fixing
an unrelated Gate failure.

### Example

Prefer:

```text
clang-tidy reports an ownership/lifetime issue
→ understand the diagnostic
→ correct the ownership/lifetime design
→ rerun the relevant Gate
```

Avoid:

```text
clang-tidy reports an ownership/lifetime issue
→ add NOLINT
→ Gate passes
```

Prefer:

```text
new source directory is reported by static analysis
→ fix the reported issues
```

Avoid:

```text
new source directory is reported by static analysis
→ add the directory to an exclusion list
```

Prefer:

```text
implementation + tests + required docs
→ checkflow fast
```

over:

```text
edit one file → fast
edit one test → fast
edit one comment → fast
```

unless an intermediate run is useful for diagnosis.

## 7. Efficiency and Workspace

- Inspect the current working state before editing.
- Preserve unrelated user changes.
- Prefer targeted symbol, caller, and test searches over broad repository reads.
- Reuse already established context instead of rediscovering it.
- Do not create extra Agents, Worktrees, reviews, or validation passes unless
  they provide clear isolation, parallelism, independence, or risk reduction.
- Keep reports concise: changes, API impact, validation, unresolved issues.
- Do not treat generated files, dependency directories, or caches as project
  source unless the task explicitly requires them.

### Example

Prefer:

```text
search symbol
→ inspect owner
→ inspect callers
→ inspect relevant tests
```

before reading an entire repository.

For one localized behavior change, prefer one coherent implementation owner over
separate implementation, test, documentation, and repair Agents.

## 8. Done

A task is complete when:

- requested behavior is implemented in the correct ownership boundary;
- externally visible contracts are correct;
- relevant tests cover the change;
- required documentation matches behavior;
- unrelated behavior remains intact;
- required validation passes;
- no known unresolved issue affecting the requested behavior remains.

A task is **not** complete if its validation only passes because relevant checks,
files, tests, diagnostics, or thresholds were disabled, excluded, suppressed, or
weakened.

Never claim a test, Gate, review, or validation passed unless it actually ran and
passed.

# Axiom Agent Development Guide

This document defines the engineering, design, testing, and quality principles that all Axiom development agents must follow.


## 1. Core Principles

- Keep changes small and focused. Each step should be independently verifiable.
- Prefer incremental development over large upfront designs followed by one-shot implementation.
- Reuse existing designs whenever possible. Avoid unnecessary abstractions and interfaces.
- Prompts guide reasoning and implementation; deterministic tools enforce quality.
- Never skip, disable, bypass, weaken, or lower quality checks merely to make a task pass.

## 2. Code Design

- Prefer **thin interfaces and deep modules**.
- Public APIs should remain minimal, stable, clear, and hide internal complexity.
- Prefer changing internal implementation over expanding the Public API.
- Place behavior in the module that truly owns the responsibility, not wherever it is easiest to modify.
- Preserve one-way dependencies:
  - lower-level modules must not depend on higher-level modules;
  - Domain/Core must not depend on UI;
  - libraries must not depend on applications;
  - production code must not depend on test code.
- Unless a module is explicitly an adapter layer, do not expose implementation types from Qt, OCC, Boost, or other third-party libraries across module boundaries.
- Prefer RAII with explicit ownership and lifetime semantics.
- Prefer composition over complex inheritance.
- Do not introduce abstractions for hypothetical future requirements.
- Avoid broad refactoring unrelated to the current task.
- Avoid vague catch-all modules such as `utils`, `common`, or `manager`.

## 3. Before Modifying Code

Before implementation, determine:

1. Which module owns the requested behavior.
2. Whether existing interfaces already satisfy the requirement.
3. Whether a Public API change is truly necessary.
4. Which existing behaviors must remain unchanged.
5. How tests will prove the change is correct.

Read only the code and context necessary to complete the current task.

## 4. Implementation Principles

- Implement the smallest correct solution that satisfies the current requirement.
- Keep naming, control flow, state, ownership, and lifetime clear.
- When encountering deep nesting, large functions, mixed responsibilities, or complex state, improve the structure rather than increasing complexity thresholds.
- Error handling must be explicit. Do not silently ignore failures or swallow exceptions without a clear contract.
- Comments should explain contracts, constraints, and **why**, rather than restating **what** the code already says.

## 5. Testing Principles

All unit and integration tests use GoogleTest.

- Every new behavior must have corresponding tests.
- Every bug fix must include a regression test that reproduces the original issue.
- Prefer testing externally observable behavior rather than implementation details.
- Each test should focus on one primary behavior, and its name should describe that behavior.
- Prefer `EXPECT_*`; use `ASSERT_*` only when subsequent test logic depends on the condition being true.
- Use the GoogleTest assertion that best matches the comparison semantics.
- Tests must be deterministic, independent, runnable in isolation, and independent of execution order.
- Use fixtures only for genuinely shared test setup; manage test resources with RAII.
- Avoid excessive mocking. Prefer small-scale tests using real objects when practical.
- Coverage is useful for identifying untested code, but does not prove test quality.
- Never add weak or meaningless tests merely to increase coverage.

## 6. Documentation

Public APIs, as well as non-obvious internal types, functions, invariants, ownership rules, lifetime rules, and state transitions, should use Doxygen-compatible documentation.

Documentation should describe API contracts and externally observable behavior.

Use the following where appropriate:

```text
@param
@return
@throws
@pre
@post
@note
@warning
```

Documentation must remain synchronized with actual behavior.

## 7. Static Analysis and Architecture Checks

Static-analysis findings should normally be resolved by improving the code.

Do not use the following merely to make checks pass:

```text
NOLINT
NOLINTNEXTLINE
NOLINTBEGIN
compiler pragmas
```

Suppression is allowed only when the code is known to be correct and the diagnostic is confirmed to be a false positive or genuinely inapplicable.

Any suppression must:

- name the specific check;
- use the smallest possible scope;
- include a clear and verifiable justification.

Architecture rules are defined in:

```text
quality/architecture_rules.json
```

Architecture violations must be resolved by correcting responsibilities, interfaces, or dependency direction. Never bypass architecture checks.

## 8. Quality Gates

CheckFlow is the unified entry point for repository quality validation.

Run its flows from the repository root with the globally installed `checkflow`
command. Do not create a project-local Python environment for CheckFlow.

The authoritative workflow configuration is:

```text
checkflow.json
```

Project-specific tools are located in:

```text
.checkflow/tools/
```

Quality policies are located in:

```text
quality/
```

During development:

- run `fast` after each independently verifiable development step;
- run `hardening` after completing the main implementation of larger tasks;
- run `full` before final delivery or merge.

If a Quality Gate fails:

1. inspect the report;
2. identify the root cause;
3. fix the problem;
4. run the gate again.

Never make a gate pass by weakening rules, skipping tests, or adding exclusions.

## 9. Git and Working Tree

Before modifying the repository, check:

```bash
git status --short
```

Unrelated existing changes must be treated as work owned by the user or another task.

Do not overwrite, revert, or casually modify unrelated work.

## 10. Prohibited Practices

Do not:

- claim completion before the required Quality Gates pass;
- weaken quality standards to make checks pass;
- expand Public APIs without a justified requirement;
- introduce incorrect dependency directions;
- unnecessarily expose third-party implementation types;
- design complex abstractions for hypothetical requirements;
- perform large unrelated refactors;
- write meaningless tests to increase coverage;
- use suppressions to hide real problems;
- modify user work unrelated to the current task.

## 11. Definition of Done

A development goal is complete only when:

- the requested behavior is implemented correctly;
- the change remains small and focused;
- the code belongs to the correct module;
- any Public API change is necessary and justified;
- dependency direction remains correct;
- ownership and lifetime are clear;
- new behavior has corresponding tests;
- bug fixes include regression tests;
- documentation matches actual behavior;
- all required Quality Gates pass.

The goal is not to make an agent generate code as quickly as possible.

The goal is to continuously deliver reliable, maintainable, and evolvable software under clear architectural boundaries, effective tests, and deterministic quality gates.

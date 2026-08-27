# Axiom Agent Development Guide

## 1. Core Principles

- Keep changes small and focused. Every step should be independently verifiable.
- Prefer incremental development over large upfront plans followed by one large implementation.
- Prompts provide direction; deterministic tools enforce quality.
- Never skip, disable, bypass, or weaken quality checks merely to make a task pass.

## 2. Code Design

- Prefer **thin interfaces and deep modules**.
- Public APIs should remain minimal, stable, and clear while hiding internal complexity.
- Prefer modifying internal module implementations instead of expanding the Public API.
- Unless a module is explicitly an adapter layer, do not expose third-party implementation types such as Qt, OCC, or Boost across module boundaries.
- Maintain one-way dependencies:
  - Lower-level modules must not depend on application-level modules.
  - Lower-level modules must not depend on UI code.
  - Production code must not depend on test code.
- Prefer RAII, explicit ownership, and composition over unnecessary complex inheritance.
- Do not introduce complex abstractions for hypothetical future requirements.
- Do not perform large-scale refactoring unrelated to the current task.
- Avoid vague catch-all modules such as `utils`, `common`, or `manager`.

## 3. Before Making Changes

Before writing code, determine:

1. Which module owns the requested behavior.
2. Whether the existing interfaces can already satisfy the requirement.
3. Whether modifying the Public API is truly necessary.
4. Which existing behaviors must remain unchanged.
5. Which tests need to be added or updated.

Read only the code that is actually relevant to the current task. Avoid expanding the working context without a clear reason.

## 4. Development Workflow

Each small development goal should follow this cycle:

```text
Understand the requirement
        ↓
Implement the minimum change
        ↓
Add or update tests
        ↓
Run fast
        ↓
Fix failures
        ↓
Proceed to the next step
```

After completing each independently verifiable goal, run:

```bash
uv run --quiet python tools/check.py fast
```

If it fails:

- Inspect the JSON report.
- Identify and fix the root cause.
- Run `fast` again.
- Do not proceed to the next development stage until it passes.

For larger tasks, run:

```bash
uv run --quiet python tools/check.py hardening
```

Before merging or declaring the overall task complete, run:

```bash
uv run --quiet python tools/check.py full
```

The authoritative definitions of checks, thresholds, and tool parameters live in `tools/check.py` and the configuration under `quality/`.

Do not duplicate those details in this document.

## 5. Testing Principles

- Every new behavior must have corresponding tests.
- Bug fixes should include regression tests.
- Prefer testing externally observable behavior rather than internal implementation details.
- Coverage is only a baseline metric; it does not prove that tests are effective.
- Do not add tests with meaningless or weak assertions merely to increase coverage.

## 6. Architecture Constraints

Architecture dependencies are enforced by:

```text
quality/architecture_rules.json
```

The fundamental rules are:

- Lower-level modules must not depend on higher-level modules.
- Domain code must not depend on UI code.
- Libraries must not depend on applications.
- Production code must not depend on test code.

If an architecture violation occurs, resolve it by correcting responsibilities, extracting interfaces, or applying dependency inversion.

Never bypass or disable architecture checks.

## 7. Multi-Agent Collaboration

For complex tasks, prefer staged execution:

```text
Planner
   ↓
Implementation
   ↓
Cleanup / Review
   ↓
Hardening
```

Different agents should preferably operate with separate, clean contexts.

When handing work from one agent to another, transfer only the information necessary to continue effectively:

- Current task objective
- Acceptance Criteria
- Git Diff
- Test results
- Quality Gate reports
- Remaining unresolved issues

Do not pass the complete long-running context of one agent directly to the next.

The repository state, code changes, tests, and deterministic reports should serve as the primary shared source of truth.

## 8. Prohibited Practices

Do not:

- Claim a task is complete without passing the required Quality Gates.
- Lower quality standards merely to make checks pass.
- Modify the Public API without a justified requirement.
- Introduce incorrect module dependency directions.
- Leak third-party implementation details across module boundaries unnecessarily.
- Create complex abstractions without concrete requirements.
- Perform large unrelated refactoring as part of a focused task.
- Hide real problems by disabling warnings, excluding tests, ignoring rules, or weakening checks.
- Continuously mix large numbers of unrelated tasks within the same long-running Agent context.

## 9. Definition of Done

A task is complete only when:

- The requested behavior has been implemented correctly.
- The scope of changes remains minimal and focused.
- Any Public API changes are necessary and justified.
- Module dependency directions remain correct.
- New behavior has corresponding tests.
- `fast` passes.
- `hardening` passes for larger tasks.
- `full` passes before merge or final delivery.

The goal is not simply for the Agent to **write code**.

The goal is for the Agent to continuously deliver **reliable, maintainable, and evolvable software within clear architectural boundaries and deterministic quality gates**.

---
name: agile-delivery
description: Coordinate complex repository changes with Sol planning and review plus task-scoped Terra implementation and hardening. Use when the user explicitly requests this multi-agent delivery loop; do not use for ordinary small edits.
---

# Agile Delivery

Use this skill for a focused implementation request that benefits from independent
planning, task-scoped development, hardening, and review. Treat the repository's
`AGENTS.md` as the source of truth for architecture, tests, and quality gates.

## Roles

- **Primary agent:** confirm which module owns the requested behavior before
  delegation, orchestrate handoffs, and run the final `full` gate.
- **Planning Sol (read-only):** inspect the confirmed module and produce the complete
  ordered set of independently verifiable development tasks, acceptance criteria,
  preserved behavior, dependencies, and tests.
- **Task Terra:** implement exactly one planned task, add or update behavioral tests,
  and own its `fast` repair loop.
- **Hardening Terra:** inspect the integrated result, run `hardening`, and repair root
  causes until the gate passes.
- **Review Sol (read-only):** use the available review skill to review the complete
  mergeable diff and report only actionable findings.

If a named model is unavailable, use the closest available model and say so in the
final handoff. Do not claim a model assignment that was not actually used.

## Delivery loop

1. The primary agent inspects the request and repository, states the owning module
   and scope, and asks the user only when unresolved ambiguity would materially
   change ownership or the result. Do not delegate implementation before this.
2. Start one fresh, read-only Sol to plan all development tasks. Require a complete,
   dependency-ordered breakdown with acceptance criteria and tests for every task.
3. For each planned task, in order, start a fresh Terra child agent with only that
   task and its relevant handoff. Terra implements the minimum change and runs
   `uv run --quiet python tools/check.py fast`. It diagnoses and repairs failures,
   rerunning `fast` until it passes before the next task starts. Do not use one Terra
   for multiple planned tasks, and do not run dependent tasks concurrently.
4. After every task Terra has completed with a passing `fast`, start one fresh Terra
   for integrated hardening. It runs
   `uv run --quiet python tools/check.py hardening`, repairs any root cause, reruns
   `fast` after each code change, and repeats `hardening` until it passes.
5. Start one fresh, read-only Sol after hardening passes. Require it to use the
   available review skill, inspect the entire mergeable diff against the confirmed
   scope and acceptance criteria, and return only actionable findings. Do not give
   it proposed conclusions.
6. For review findings, start a fresh Terra with only the findings and relevant
   context. It repairs them and reruns `fast`; then a fresh review Sol reviews the
   updated complete diff. Repeat until review is clean.
7. The primary agent runs `uv run --quiet python tools/check.py full` only after a
   clean review. If `full` fails, start a fresh Terra to repair the root cause and
   rerun `fast`, then repeat the review and `full` stages. Never reuse a passing gate
   or review after the diff changes.

Do not start a Luna agent or any separate agent whose only responsibility is running
tests. Test and repair responsibility stays with each task Terra, the hardening Terra,
and the primary agent's final `full` run.

The loop stops when all acceptance criteria, `fast`, `hardening`, clean review, and
`full` pass on the final diff; when the user changes scope; or when progress requires
user authority or information.
Do not retry unchanged failures indefinitely: report the evidence and the precise
blocker when a safe repair cannot be identified.

## Handoff format

Each handoff is concise and contains only: the current task objective, acceptance
criteria, relevant files or diff, test and quality-gate output, and unresolved
issues. The repository state and deterministic reports take precedence over prior
agent narration.

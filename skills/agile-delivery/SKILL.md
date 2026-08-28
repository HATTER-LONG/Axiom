---
name: agile-delivery
description: Coordinate planning, task-scoped delivery, hardening, and review for a complex repository change. Use only when the user explicitly requests this multi-agent delivery loop.
---

# Agile Delivery

This skill is executed by the primary conversation only. The primary agent owns
orchestration: it starts every child, receives its final handoff, and decides the
next sequential or parallel work. Children receive only their assigned task and
handoff; they do not need to load or follow this skill.

Use it for a focused implementation request that benefits from independent planning,
task-scoped development, hardening, and review. Treat the repository's `AGENTS.md`
as the source of truth for architecture, tests, and quality gates.

## Runtime selection

Inspect the available runtime capabilities before delegation.

- If fresh child agents and model assignment are available, prefer Sol for read-only
  planning and review, and Terra for implementation and hardening.
- If fresh child agents are available but model assignment is not, use the same role
  sequence with the runtime's default model; do not require or pretend to switch
  models.
- If child agents are unavailable, the primary executes the stages sequentially.
  State that planning or review was not independent; retain all applicable gates.

Never claim a model assignment, independent review, or gate result that did not
actually occur. Do not start a separate child solely to run tests.

## Roles

- **Primary:** confirm ownership and scope, create all handoffs, integrate results,
  and run the final `full` gate.
- **Planner (read-only):** return a dependency-ordered plan with acceptance criteria,
  preserved behavior, independent tasks, and tests.
- **Task implementer:** implement exactly one planned task, update behavioral tests,
  update Doxygen-compatible documentation for changed contracts and non-obvious
  behavior, and repair `fast` until it passes. Do not suppress clang-tidy unless the
  diagnostic is confirmed inapplicable and the narrow suppression is justified.
- **Hardening implementer:** run `hardening`, repair root causes, and rerun `fast`
  after every code change.
- **Reviewer (read-only):** inspect the complete mergeable diff against confirmed
  scope and acceptance criteria; return only actionable findings.

## Delivery loop

1. The primary confirms the owning module and scope. Ask only if unresolved ambiguity
   would materially change ownership or outcome.
2. Run one fresh, read-only planning role. Do not delegate implementation before its
   dependency-ordered plan is available.
3. Run one fresh task role per planned task. Use the plan's dependency order and
   parallelize only tasks it explicitly identifies as independent. Each task owns its
   tests and `fast` repair loop.
4. After all task roles report passing `fast`, run one fresh hardening role. Repair
   and repeat until `hardening` passes.
5. Run one fresh, read-only review role after hardening passes. If review identifies
   findings, create fresh task roles to repair them, then repeat hardening and review;
   all prior gates and reviews are stale after a code change.
6. Run `full` only after review is clean. If it reports files requiring formatting,
   format only those files, inspect the diff, then repeat hardening, review, and `full`.
   Repair any other failure with a fresh task role and repeat the same sequence.

Stop when acceptance criteria, required gates, and clean review pass; when the user
changes scope; or when progress requires user authority or information. Do not retry
an unchanged failure indefinitely.

## Handoffs

Each primary-to-child handoff contains only the current objective, acceptance
criteria, relevant files or diff, test and quality-gate output, and unresolved
issues. The child returns the same categories that exist for the next stage.

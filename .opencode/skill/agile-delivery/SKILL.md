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

## Quality-gate interaction

For normal implementation work, treat the repository quality runner as a
black-box contract. Task and hardening roles run the required gate and use its
JSON report plus relevant compiler/test output to repair the product code. Do
not assign or perform exploratory reading of `tools/check.py`, `tools/checks/`,
`tools/check_test/`, or `quality/` just to understand a gate or predict a pass.
Read those files only when the assigned work changes the quality system or
policy, a failed report is not actionable, or there is concrete evidence that a
check is defective; keep that inspection scoped to the diagnosis.

## Runtime selection

Inspect the available runtime capabilities before delegation.

- When fresh child agents and model assignment are available, prefer Sol for read-only
  planning and review, and Terra for implementation and hardening.
- When fresh child agents are available without model assignment, use the same role
  sequence with the default model; do not require or pretend to switch models.
- When child agents are unavailable, the primary executes stages sequentially and
  states that planning or review was not independent.

In OpenCode, launch every child through `task` with a fresh session. Use
`subagent_type: "explore"` for read-only planning and review, and
`subagent_type: "general"` for task implementation and hardening. Never reuse a
`task_id`, and do not start a separate child solely to run tests.

## Roles

- **Primary:** confirm ownership and scope, create all handoffs, integrate results,
  and run the final `full` gate.
- **Planner (read-only):** return a dependency-ordered plan with acceptance criteria,
  preserved behavior, independent tasks, and tests.
- **Task implementer:** implement exactly one planned task, update behavioral tests,
  and repair `fast` until it passes.
- **Hardening implementer:** run `hardening`, repair root causes, and rerun `fast`
  after every code change.
- **Reviewer (read-only):** inspect the complete mergeable diff against confirmed
  scope and acceptance criteria; return only actionable findings.

## Delivery loop

1. Confirm the owning module and scope. Ask only if unresolved ambiguity would
   materially change ownership or outcome.
2. Start one fresh, read-only planning role. Do not delegate implementation before its
   dependency-ordered plan is available.
3. Start one fresh task role per planned task. Use the plan's dependency order and
   parallelize only tasks it explicitly identifies as independent. Each task owns its
   tests and `fast` repair loop.
4. After all task roles report passing `fast`, start one fresh hardening role. Repair
   and repeat until `hardening` passes.
5. Start one fresh, read-only review role after hardening passes. If review identifies
   findings, create fresh task roles to repair them, then repeat hardening and review;
   all prior gates and reviews are stale after a code change.
6. Run `full` only after review is clean. If it fails, repair the root cause with a
   fresh task role, then repeat hardening, review, and `full`.

Stop when acceptance criteria, required gates, and clean review pass; when the user
changes scope; or when progress requires user authority or information. Do not retry
an unchanged failure indefinitely.

## Handoffs

Each primary-to-child handoff contains only the current objective, acceptance
criteria, relevant files or diff, test and quality-gate output, and unresolved
issues. The child returns the same categories that exist for the next stage.

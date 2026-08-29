---
name: agile-delivery
description: Coordinate task-scoped implementation, hardening, review, and final validation for a complex repository change. Use only when the user explicitly requests this multi-agent delivery loop.
---

# Agile Delivery

This skill is executed by the primary conversation only.

The primary owns orchestration. It confirms scope, decomposes work into a
dependency-ordered backlog, delegates implementation, runs repository gates,
coordinates repairs, and starts the final review.

Treat `AGENTS.md` as the source of truth for architecture, tests, quality gates,
and repository conventions.

## Roles

### Primary

Responsible for:

- confirming owning module and scope;
- defining acceptance criteria and preserved behavior;
- decomposing work into dependency-ordered subtasks;
- scheduling ready subtasks;
- running `hardening` and `full`;
- routing failures and review findings;
- integrating the final result.

The primary may define boundaries and dependencies, but must not prescribe
implementation details.

### Implementer

One implementer owns one subtask.

Before coding, briefly state:

- affected module/files;
- intended design;
- required Public API changes;
- preserved behavior;
- tests to add or update.

Then:

- implement the assigned scope;
- update tests and Doxygen-compatible documentation;
- run and repair `fast` until it passes;
- run one final `fast` after all changes, then commit the task before returning;
- use a concise commit message that states the task outcome; repair commits must
  identify the repaired problem;
- fix root causes rather than suppress diagnostics.

### Reviewer

One read-only reviewer inspects the committed task range after `hardening` passes.

Review against:

- user scope;
- acceptance criteria;
- architecture rules;
- correctness;
- Public API discipline;
- tests and documentation.

Return actionable findings only.

## Runtime

If child agents and model assignment are available:

- prefer Terra for implementation and repair;
- prefer Sol for review.

Otherwise preserve the same role separation using available capabilities.

Keep the reviewer agent available after it returns findings. Reuse that same reviewer
to verify repair commits and review later commits in the task.

If child agents are unavailable, execute sequentially and state that review was
not independent.

Do not create separate agents for planning, design, tests, or gate execution.

## Gates

`fast`, `hardening`, and `full` are symbolic gate names.

Their exact commands and requirements MUST come from `AGENTS.md` or repository
instructions.

Do not invent missing gate commands.

## Backlog

Each subtask contains only:

- objective;
- acceptance criteria;
- scope boundary;
- preserved behavior;
- dependencies.

Do not include solution design in the backlog.

Start a subtask only after all dependencies pass. Independent ready subtasks may
run in parallel.

## Git Task Protocol

1. Inspect `git status --short` before starting. Do not absorb unrelated work.
2. An implementer may not return, hand off, or switch tasks with its changes
   uncommitted. Each completed subtask is represented by one or more focused commits.
3. The final commit follows a passing final `fast` run. Its concise log states the
   task content; a repair commit also names the defect it repairs.
4. The primary reviews only committed work: inspect `git log` and `git diff
   <base>..HEAD` or `git show`, then record that range in the handoff.
5. After findings, the same reviewer verifies the repair commits and reviews new
   commits since its prior reviewed range. Never base a review on uncommitted diffs.

## Delivery Loop

1. Confirm scope and owning module.
2. Build the dependency-ordered backlog.
3. For each ready subtask:
   - start its implementer;
   - design briefly;
   - implement;
   - update tests/docs;
   - repair until `fast` passes.
4. After all subtasks pass `fast`, run `hardening`.
5. Route failures:
   - clear subtask ownership → original implementer repairs;
   - cross-cutting/integration issue → fresh repair implementer.
6. Rerun affected `fast` and `hardening` until clean.
7. Start one read-only reviewer for the committed task range.
8. Route review findings using the same ownership rule.
9. After semantic changes, rerun `hardening` and review.
10. Run `full` only after review is clean.
11. Repair failures and repeat the required gate sequence until complete.

Do not retry an unchanged failure indefinitely.

## Handoffs

Primary-to-implementer handoffs contain only:

- objective;
- acceptance criteria;
- scope boundary;
- preserved behavior;
- relevant files and committed range;
- relevant gate or review output;
- unresolved issues.

The handoff must not prescribe implementation design.

Implementers return:

- design summary;
- changed files;
- implemented behavior;
- Public API changes;
- tests/docs updated;
- `fast` result;
- commit IDs and concise log summary;
- remaining issues.

## Completion

Stop when:

- acceptance criteria are satisfied;
- all required subtasks are complete;
- required `fast` checks pass;
- `hardening` passes;
- review is clean;
- `full` passes.

Never claim a gate, independent review, or completion that did not actually occur.

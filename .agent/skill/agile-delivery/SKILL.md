---
name: agile-delivery
description: Coordinate complex repository changes using multiple agents, Git worktrees, CheckFlow quality gates, repair loops, and independent review. Use only when the user explicitly requests this delivery workflow.
---

# Agile Delivery

This Skill is executed only by the Primary Agent.

The Primary owns task decomposition, parallel scheduling, Worktree management, integration, quality gates, repair routing, and final review.

Engineering, testing, architecture, and quality principles are defined by `AGENTS.md`.

CheckFlow is installed globally. All Worktrees use the `checkflow` command directly and do not create or depend on a dedicated Python `.venv`.

## 1. Roles

### Primary

Responsible for:

- confirming scope, owning module, acceptance criteria, and preserved behavior;
- decomposing work into dependency-ordered subtasks;
- identifying tasks that can run in parallel;
- creating isolated Worktrees;
- scheduling Implementers;
- integrating committed results;
- running integrated `hardening` and final `full`;
- routing Gate and Review findings;
- coordinating repairs;
- confirming final delivery.

The Primary defines boundaries, dependencies, and constraints, but does not prescribe implementation details.

### Implementer

Each Implementer owns one subtask and works in an isolated Worktree.

Responsibilities:

- analyze the existing design and choose an implementation;
- update implementation, tests, and necessary documentation;
- keep changes small and focused;
- repair failures until `fast` passes;
- run a final `fast` on the completed state;
- commit all task changes before returning.

Return:

- implementation summary;
- Public API changes;
- tests and documentation updated;
- `fast` result;
- commit ID;
- unresolved issues.

### Reviewer

The Reviewer performs read-only review of the committed range already integrated by the Primary and passing `hardening`.

Review:

- user scope and acceptance criteria;
- correctness and edge cases;
- architecture and module ownership;
- Public API discipline;
- test effectiveness;
- documentation consistency.

Return only clear, actionable findings.

Reuse the same Reviewer for repair verification when possible.

## 2. Backlog

Each subtask contains only:

- objective;
- acceptance criteria;
- scope boundary;
- preserved behavior;
- dependencies.

Do not prescribe implementation design in the Backlog.

Tasks may run in parallel only when:

- no dependency exists between them;
- ownership is clear;
- they do not modify the same core interface or data model;
- one task's design decisions cannot change another task's implementation assumptions.

Otherwise, create an explicit dependency order.

## 3. Worktree Protocol

All Worktrees in the same parallel batch must start from the same Primary-approved base commit.

```text
Primary
  ├── worktree/task-a → Implementer A
  ├── worktree/task-b → Implementer B
  └── worktree/task-c → Implementer C
```

Rules:

- one Worktree belongs to one task at a time;
- Implementers must not modify other Worktrees;
- Implementers must not independently merge, rebase, or synchronize other task branches;
- agents exchange results only through committed Git history;
- uncommitted diffs must never be used as task handoff.

The Primary integrates explicit commits, normally through `cherry-pick`.

Do not integrate by copying uncommitted files between Worktrees.

## 4. Worktree Lifecycle

A Worktree is a temporary execution environment, not persistent delivery state.

Normal lifecycle:

```text
create
→ implement
→ fast
→ commit
→ Primary integrate
→ remove
```

After successful integration, the task Worktree may be removed.

If a later Gate or Review finds a problem, create a new Repair Worktree from the current Primary HEAD instead of reusing the old task Worktree.

Persistent shared facts are:

- commits;
- Git history;
- CheckFlow reports;
- Review results.

## 5. CheckFlow

Implementers run from the root of their own Worktree:

```bash
checkflow fast --report build-quality/reports/fast.json
```

After integration, the Primary runs:

```bash
checkflow hardening --report build-quality/reports/hardening.json
```

Before final delivery:

```bash
checkflow full --report build-quality/reports/full.json
```

CheckFlow workflows, project tools, and quality policies are defined by:

```text
checkflow.json
.checkflow/tools/
quality/
```

Do not invent, replace, weaken, or bypass repository-defined Gates.

Worktrees do not need a dedicated `.venv` for CheckFlow.

## 6. Delivery Loop

```text
Confirm scope
    ↓
Build Backlog
    ↓
Start ready tasks
    ↓
Parallel implementation
    ↓
checkflow fast
    ↓
commit
    ↓
Primary integration
    ↓
checkflow hardening
    ↓
Review
    ↓
Repair
    ↓
checkflow full
```

An Implementer's `fast` validates only the subtask against its own base state.

Integrated correctness must be validated again by the Primary.

## 7. Gate Failure Repair

When `hardening` or `full` fails:

- if the failure clearly belongs to an existing subtask, prefer the original Implementer;
- if the failure is cross-cutting, integration-related, or ownership is unclear, create a new Repair Agent.

Every repair must use a new isolated Repair Worktree created from the current Primary HEAD.

```text
Gate Failure
    ↓
Determine ownership
    ↓
Repair Worktree
    ↓
Repair
    ↓
checkflow fast
    ↓
commit
    ↓
Primary integrate
    ↓
Rerun affected Gate
```

Never make a Gate pass by:

- suppressing diagnostics;
- skipping tests;
- weakening rules;
- adding exclusions.

Do not retry an unchanged failure indefinitely.

## 8. Validation Invalidation

Any semantic code change invalidates relevant downstream validation state.

For example, if Review or `full` discovers a defect and code changes:

```text
semantic change
    ↓
checkflow hardening
    ↓
Review again when required
    ↓
checkflow full
```

Do not reuse previous Review or Gate conclusions after a relevant semantic change.

## 9. Review

Start an independent Reviewer only after all planned tasks are integrated and `hardening` passes.

The Reviewer inspects committed work only.

The Primary must record the reviewed commit range.

For findings:

```text
Finding
   ↓
Determine ownership
   ↓
Repair Worktree
   ↓
checkflow fast
   ↓
commit
   ↓
Primary integrate
   ↓
checkflow hardening
   ↓
Same Reviewer checks new commits
```

The Reviewer should verify the repair commits and confirm the original findings are resolved.

Semantic repairs must be reviewed again.

Uncommitted work must not produce Review conclusions.

## 10. Integration

The Primary integrates subtasks according to dependency order.

When conflicts occur:

- first determine whether the conflict is purely textual;
- if behavior, interfaces, or design semantics are involved, route the issue to the relevant Implementer or Repair Agent;
- do not mechanically resolve semantic conflicts and continue.

Integrated behavior must be validated by the Primary's Gates.

## 11. Handoff

Primary-to-Implementer handoff contains only:

- objective;
- acceptance criteria;
- scope boundary;
- preserved behavior;
- dependencies;
- relevant files;
- necessary Gate or Review information.

Do not:

- transfer another Agent's full context;
- prescribe implementation details;
- allow the Implementer to expand task scope without need.

Implementers return:

- implementation summary;
- changed scope;
- Public API changes;
- tests and documentation updated;
- `fast` result;
- commit ID;
- unresolved issues.

## 12. Completion

The delivery is complete only when:

- all acceptance criteria are satisfied;
- all subtasks are complete and integrated;
- each subtask's final `fast` passes;
- integrated `hardening` passes;
- independent Review has no unresolved findings;
- semantic changes after Review have been revalidated;
- final `full` passes.

Never claim that a Gate, Review, test, repair validation, or completion occurred when it did not.

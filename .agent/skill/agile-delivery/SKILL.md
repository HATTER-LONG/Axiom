---
name: agile-delivery
description: Coordinate complex repository changes using multiple agents, Git worktrees, CheckFlow quality gates, repair loops, and independent review. Use only when the user explicitly requests this delivery workflow.
---

# Agile Delivery

This Skill is executed only by the Primary Agent.

The Primary owns task decomposition, parallel scheduling, Worktree management,
integration, quality gates, repair routing, repeated-failure handling, and final
review.

Engineering, testing, architecture, and quality principles are defined by
`AGENTS.md`.

CheckFlow is installed globally. All Worktrees use the `checkflow` command
directly and do not create or depend on a dedicated Python `.venv`.

## 1. Roles

### Primary

Responsible for:

- confirming scope, owning module, acceptance criteria, and preserved behavior;
- decomposing work into dependency-ordered subtasks;
- identifying tasks that may run in parallel;
- creating isolated Worktrees;
- scheduling Implementers;
- integrating committed results;
- running integrated `hardening` and final `full`;
- routing Gate and Review findings;
- applying simple repairs directly;
- coordinating non-simple repairs;
- stopping when the same root cause fails again after repair;
- coordinating final Review and delivery.

The Primary defines boundaries and dependencies, but does not prescribe
implementation details.

### Implementer

Each Implementer owns one subtask in an isolated Worktree.

Responsibilities:

- analyze the existing design and choose an implementation;
- update implementation, tests, and necessary documentation;
- keep changes focused;
- run `fast` and repair failures;
- stop if the same identified failure remains after one repair attempt;
- commit all completed task changes.

Return:

- implementation summary;
- Public API changes;
- tests and documentation updated;
- `fast` result;
- commit ID;
- unresolved issues.

### Reviewer

The Reviewer performs read-only review of the committed range already integrated
by the Primary and passing `hardening`.

Review:

- scope and acceptance criteria;
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
- one task's design decisions cannot affect another task's assumptions.

Otherwise, define an explicit dependency order.

## 3. Worktree Protocol

All Worktrees in the same parallel batch start from the same Primary-approved
base commit.

```text
Primary
  ├── worktree/task-a → Implementer A
  ├── worktree/task-b → Implementer B
  └── worktree/task-c → Implementer C
```

Rules:

- one Worktree belongs to one task at a time;
- concurrent Worktrees must use unique paths and branch names;
- name them from the task id, such as `worktree/task-a`;
- Implementers must not modify or synchronize other Worktrees;
- agents exchange results only through committed Git history;
- uncommitted diffs must never be used as handoff.

The Primary normally integrates explicit commits through `cherry-pick`.

## 4. Worktree Lifecycle

A Worktree is temporary execution state.

```text
create
→ implement
→ fast
→ commit
→ Primary integrate
→ remove
```

After successful integration, remove the Worktree when no longer needed.

Later Gate or Review repairs should use a new Repair Worktree from the current
Primary HEAD rather than reusing the original task Worktree.

## 5. CheckFlow

Implementers run:

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

Repository-defined workflows and quality policy come from:

```text
checkflow.json
.checkflow/tools/
quality/
```

Do not invent, weaken, suppress, skip, exclude, or bypass repository-defined
Gates.

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
fast
    ↓
commit
    ↓
Primary integration
    ↓
hardening
    ↓
Review
    ↓
Repair
    ↓
full
```

An Implementer's `fast` validates only its own task state.

Integrated correctness must always be validated again by the Primary.

## 7. Gate Failure Repair

When `fast`, `hardening`, or `full` fails, identify the concrete failure and root
cause before repairing it.

For integrated failures:

- simple failures may be repaired directly by the Primary;
- non-simple failures belonging to an existing task should preferably return to
  the original Implementer;
- cross-cutting, integration-related, or unclear failures require a new Repair
  Agent.

A failure is simple when it is localized, requires no Public API or ownership
change, and does not need a new task boundary.

Examples include:

- typo;
- missing include;
- one-line test or documentation correction;
- CheckFlow or Skill configuration correction.

If uncertain, treat the failure as non-simple.

Non-simple repairs use a new isolated Repair Worktree from the current Primary
HEAD.

```text
Gate Failure
    ↓
Identify root cause
    ↓
Simple: repair directly
    ↓
Not simple: Repair Worktree + Agent
    ↓
fast
    ↓
commit
    ↓
Primary integrate
    ↓
Rerun affected Gate
```

Never make a Gate pass by suppressing diagnostics, skipping tests, weakening
rules, or adding exclusions.

## 8. Repeated Failure Stop Policy

A specific failure receives at most one automatic repair attempt for the same
identified root cause.

```text
failure A
    ↓
repair A
    ↓
rerun validation
    ↓
same root cause remains
    ↓
STOP
```

A failure is considered repeated when the underlying problem is the same, even
if line numbers, diagnostic wording, paths, or ordering changed.

If the repair resolves A but reveals a different failure B, B may enter the
normal repair flow.

When the same root cause remains after repair:

- stop automatic repair for that delivery path;
- do not retry the same repair;
- do not create another Agent merely to retry the same problem;
- do not modify unrelated code or broaden scope speculatively;
- do not weaken or bypass validation;
- report the unresolved failure to the user.

The report should include:

- failing Gate or Review finding;
- identified root cause;
- repair attempted;
- validation rerun result;
- relevant diagnostics;
- current commit and integration state.

This rule applies to Implementer `fast`, Primary Gates, and Review repair loops.

## 9. Validation Invalidation

Any semantic code change invalidates relevant downstream validation.

```text
semantic change
    ↓
hardening
    ↓
Review again when required
    ↓
full
```

Do not reuse previous Gate or Review conclusions after relevant semantic changes.

## 10. Review

Start an independent Reviewer only after all planned tasks are integrated and
`hardening` passes.

The Reviewer inspects committed work only.

The Primary records the reviewed commit range.

For findings:

- simple findings may be repaired directly by the Primary;
- non-simple findings use a new Repair Worktree and Agent.

```text
Finding
   ↓
Repair
   ↓
fast when Worktree is used
   ↓
commit
   ↓
Primary integrate
   ↓
hardening
   ↓
Same Reviewer verifies repair
```

Semantic repairs must be reviewed again.

If the same Review finding remains after one repair attempt for the same root
cause, follow the Repeated Failure Stop Policy.

## 11. Integration

The Primary integrates subtasks according to dependency order.

When conflicts occur:

- simple textual conflicts may be resolved by the Primary;
- behavioral, interface, ownership, or design conflicts must be routed to the
  relevant Implementer or Repair Agent;
- do not mechanically resolve semantic conflicts.

Integrated behavior must be validated by the Primary's Gates.

## 12. Handoff

Primary-to-Implementer handoff contains only:

- objective;
- acceptance criteria;
- scope boundary;
- preserved behavior;
- dependencies;
- relevant files;
- necessary Gate or Review information.

Do not transfer another Agent's full context or prescribe implementation details.

Implementers return:

- implementation summary;
- changed scope;
- Public API changes;
- tests and documentation updated;
- `fast` result;
- commit ID;
- unresolved issues.

## 13. Completion

Delivery is complete only when:

- all acceptance criteria are satisfied;
- all subtasks are integrated;
- each subtask's final `fast` passes;
- integrated `hardening` passes;
- Review has no unresolved findings;
- semantic repairs have been revalidated;
- no repeated failure is awaiting user intervention;
- final `full` passes.

Never claim that a Gate, Review, test, repair validation, or completion occurred
when it did not.

---
name: agile-delivery
description: Coordinate repository changes with adaptive delegation, Git worktrees, CheckFlow validation, repair loops, and optional independent review. Use only when the user explicitly requests this delivery workflow.
---

# Agile Delivery

This Skill is executed only by the Primary.

`AGENTS.md` defines engineering, testing, documentation, architecture, and
quality principles. This Skill defines only execution and coordination.

Use the **smallest delivery process that safely completes the task**.

Do not create Agents, Worktrees, subtasks, reviews, or validation cycles unless
they provide clear value.

## 1. Execution Model

The Primary owns:

- scope and acceptance criteria;
- task decomposition when useful;
- delegation and scheduling;
- Worktree lifecycle;
- integration;
- integrated validation;
- repair routing;
- optional independent review;
- final delivery.

The default execution mode is:

```text
Primary
→ implement
→ validate
→ deliver
```

Escalate only when needed.

Possible escalation:

```text
Primary
    ↓
Delegate isolated work
    ↓
Parallel Worktrees when beneficial
    ↓
Integrate
    ↓
Hardening
    ↓
Independent Review when warranted
    ↓
Full
```

## 2. When to Delegate

The Primary may implement work directly.

Create an Implementer only when delegation provides at least one clear benefit:

- independent ownership;
- useful parallelism;
- substantial isolated implementation work;
- reduced context pressure for the Primary;
- specialized investigation or implementation.

Do not delegate a small localized change merely because an Agent is available.

Do not split implementation, tests, and documentation into separate tasks when
they belong to one coherent behavior change.

## 3. Decomposition

Decompose only when doing so reduces coupling, context size, integration risk,
or wall-clock execution time.

Each delegated task contains:

- objective;
- acceptance criteria;
- scope boundary;
- preserved behavior;
- dependencies.

Do not prescribe implementation details.

Tasks may run in parallel only when:

- no dependency exists between them;
- ownership is clear;
- they do not compete for the same core interface or data model;
- one task's design decisions cannot invalidate another task's assumptions.

Otherwise, execute them in dependency order.

## 4. Worktrees

Use a Worktree for delegated implementation that needs isolated repository
state.

All Worktrees in the same parallel batch start from the same
Primary-approved base commit.

```text
Primary
  ├── task-a
  ├── task-b
  └── task-c
```

Rules:

- one Worktree owns one active task;
- concurrent Worktrees use unique paths and branches;
- Agents must not modify other Worktrees;
- handoff occurs through committed Git history;
- never integrate uncommitted diffs from another Agent.

The normal lifecycle is:

```text
create
→ implement
→ fast
→ commit
→ integrate
→ remove
```

Remove completed Worktrees after successful integration unless they are still
needed for immediate investigation.

## 5. Implementer

An Implementer:

1. reads only the context needed for its assigned task;
2. follows `AGENTS.md`;
3. implements the complete coherent change, including relevant tests and
   documentation;
4. runs `checkflow fast`;
5. repairs ordinary localized failures;
6. commits completed work.

Return only:

```text
commit: <id>
summary: <short description>
api: changed | unchanged
tests: <short description>
fast: pass | fail
issues: none | <short description>
```

Avoid long implementation narratives unless required to explain an unresolved
design issue.

## 6. Integration

The Primary integrates committed results according to dependency order,
normally through explicit commits.

For conflicts:

- resolve simple textual conflicts directly;
- route semantic, ownership, API, or behavioral conflicts back to appropriate
  implementation or repair work;
- never mechanically resolve a conflict whose intended behavior is unclear.

After integrating a coherent batch of semantic changes, run the appropriate
integrated validation.

## 7. CheckFlow

Use repository-defined CheckFlow workflows.

Typical levels:

```bash
checkflow fast
checkflow hardening
checkflow full
```

Validation strategy:

### Implementer

Run `fast` once the delegated task reaches a coherent implementation state.

### Primary

Run `hardening` after integrating substantial semantic work.

Run `full` once on the final delivery candidate.

Do not mechanically rerun:

```text
fast → hardening → fast → hardening
```

on unchanged code.

A stronger successful Gate supersedes weaker validation for the same unchanged
state unless the repository explicitly defines otherwise.

Any relevant semantic change invalidates downstream validation.

## 8. Gate Failures

When a Gate fails, first inspect the concrete failure.

Repair directly in the Primary when the failure is localized and its ownership
is clear.

Examples:

- missing include;
- typo;
- formatting issue;
- localized warning;
- small test correction;
- documentation mismatch.

Create a Repair Agent only when the repair is substantial or benefits from
isolated context, for example:

- cross-module behavior;
- unclear ownership;
- architectural conflict;
- significant implementation change;
- integration issue spanning delegated tasks.

Repair Agents use a new Worktree from the current Primary HEAD.

Do not create a new Agent merely because a diagnostic requires thought.

Never bypass or weaken a Gate.

## 9. Repeated Failure

A specific root cause receives at most one automatic repair attempt after it has
been clearly identified.

```text
failure A
→ repair A
→ rerun
→ same root cause remains
→ stop
```

If the repair resolves A and reveals a different failure B, B may enter the
normal repair flow.

When stopping, report concisely:

- failing validation;
- root cause;
- attempted repair;
- rerun result;
- current repository state.

Do not retry the same repair through additional Agents.

## 10. Review

Independent Review is optional, not a mandatory workflow stage.

Use a Reviewer when independent reasoning provides meaningful risk reduction,
such as for:

- public API changes;
- ownership or lifetime changes;
- concurrency;
- persistence or serialization contracts;
- security-sensitive behavior;
- cross-module architecture;
- substantial refactoring;
- large or high-risk semantic changes.

Localized internal implementation changes may be reviewed directly by the
Primary.

Review begins only after the relevant integrated `hardening` passes.

The Reviewer:

- reads committed changes only;
- checks correctness, contracts, architecture, tests, and important edge cases;
- returns actionable findings only.

If no issue exists, return:

```text
PASS
```

Do not return a general implementation summary.

## 11. Review Repair

Localized findings may be repaired directly by the Primary.

Substantial findings may use a Repair Agent and Worktree.

After semantic repair:

```text
repair
→ affected validation
→ Reviewer recheck when the finding required independent review
```

Reuse the same Reviewer when practical.

If the same finding remains after one repair for the same identified root cause,
stop according to the repeated-failure policy.

## 12. Context Discipline

Agent communication should contain only information required to continue the
task.

Primary-to-Agent handoff should prefer:

```text
TASK:
OBJECTIVE:
ACCEPT:
SCOPE:
PRESERVE:
DEPEND:
RELEVANT:
```

Do not transmit:

- another Agent's full reasoning;
- full repository summaries;
- unrelated diagnostics;
- repeated engineering rules already defined by `AGENTS.md`.

Agents should discover implementation details from the repository rather than
receiving a prescribed implementation from the Primary.

Reuse established findings instead of repeatedly rediscovering unchanged
context.

## 13. Completion

Delivery is complete when:

- acceptance criteria are satisfied;
- delegated work is integrated;
- required validation for the final state passes;
- required Review has no unresolved finding;
- no repeated failure is awaiting user intervention.

For normal substantial delivery, the final validation is:

```text
checkflow full
```

Do not claim completion, validation, review, or repair success unless it
actually occurred.

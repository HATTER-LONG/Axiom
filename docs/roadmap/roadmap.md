# Axiom AI-First Development Roadmap

> Status: Draft roadmap
> Scope: Post-foundation development
> Principle: Keep Axiom domain-agnostic and AI-first without coupling core modules to any specific LLM SDK or protocol.

## 1. Goal

Axiom should evolve from a set of reusable runtime primitives into a coherent application infrastructure layer that is easy for C++, Python, automation systems and AI agents to discover, invoke, observe and extend.

The long-term model is:

```text
Application / Python / Agent / Remote Client
                  |
                  v
        Adapters and Integrations
                  |
                  v
          Introspection + Command + Runtime
          /        |        \
      Action    Resource    Task
          \        |        /
            Foundation
        Logging / Events / Async
```

Core modules define stable application semantics. Agent, protocol and vendor integrations remain adapters above the core.

## 2. Development Principles

- Keep public APIs small, stable and domain-agnostic.
- Prefer machine-readable descriptions over AI-specific abstractions.
- Keep Action invocation synchronous; asynchronous work remains represented by Task.
- Keep Task independent from Action ownership.
- Keep Resource independent from Action ownership.
- Do not introduce a global service locator or God context.
- Do not make core modules depend on OpenAI, MCP, Python, HTTP, JSON-RPC or other external protocols.
- Add adapters only after the underlying Axiom semantics are stable.
- Prefer explicit lifetime and concurrency contracts.
- Preserve deterministic quality gates for every new subsystem.

---

# Phase 1 - Execution Correlation

## 3. Invocation Context and Execution Correlation

### Goal

Create a consistent execution identity that can be propagated across Action invocation, Logging and Task creation.

### Scope

- Extend and stabilize `InvocationContext` semantics.
- Standardize fields such as:
  - request ID;
  - trace ID;
  - caller identity/type;
  - metadata.
- Automatically associate Runtime logs with invocation information.
- Allow Task submission to optionally record its invocation origin.
- Expose correlation metadata through Task descriptors and Introspection.

### Constraints

- Task must not depend on Action runtime ownership.
- Invocation context must not become a service locator.
- Business code must not be forced to depend on logging or task services.

### Outcome

A caller can correlate:

```text
request -> action -> task -> logs
```

---

# Phase 2 - Machine-Readable Discovery

## 4. Canonical Introspection Schema

### Goal

Make Axiom descriptions complete enough for tools and agents to understand available capabilities without depending on C++ implementation details.

### Scope

- Stabilize canonical descriptors for:
  - Modules;
  - Actions;
  - parameters;
  - return values;
  - Resources;
  - Tasks.
- Ensure descriptors include machine-useful metadata such as descriptions, types, defaults, tags and versions where appropriate.
- Define deterministic serialization-friendly semantics without requiring JSON in the core.
- Add schema validation tests.

### Outcome

One canonical Axiom description can later drive:

```text
Python API
CLI help
Agent tool schema
MCP
RPC
Documentation
```

---

## 5. Introspection Query Improvements

### Goal

Make runtime discovery efficient and practical for large applications.

### Scope

- Filtering by module, tag, type and capability category.
- Stable ordering and pagination-friendly semantics where needed.
- Optional runtime summary views.
- Preserve read-only and non-owning service behavior.

### Not in Scope

- Search ranking based on LLM embeddings.
- AI-specific relevance scoring.

---

# Phase 3 - Language Integration

## 6. Python Binding

### Goal

Expose Axiom runtime capabilities to Python while preserving the same semantic model as C++.

### Scope

- Value and Result conversion.
- Runtime discovery and invocation.
- Resource discovery and typed-safe handle representation where practical.
- Task observation, cancellation and result access.
- Introspection APIs.
- Exception mapping that preserves Axiom errors.

### Constraints

- Python must remain an adapter over the C++ runtime.
- Do not duplicate business registration models in Python unless required later.
- Keep the C++ API authoritative.

### Outcome

Python can discover and operate the same application capabilities as native C++ callers.

---

# Phase 4 - Agent Integration

## 7. Generic Tool Adapter

### Goal

Provide a protocol-neutral translation from Axiom Action descriptors to generic tool definitions and tool calls.

### Scope

- Convert Action descriptors into a generic tool schema.
- Convert tool-call arguments into Axiom `Arguments`.
- Build `InvocationContext` for agent-originated calls.
- Convert `Result<Value>` into adapter-level responses.
- Surface Resource and Task identities without hiding their Axiom semantics.

### Constraints

- No dependency on a specific LLM vendor.
- No model orchestration logic in core.
- No prompt management in Action or Introspection modules.

### Outcome

Axiom becomes directly usable as an agent-facing application capability runtime.

---

## 8. Agent-Oriented Discovery Views

### Goal

Provide compact machine-consumable discovery output suitable for tool selection and planning.

### Scope

- Capability summaries derived from canonical descriptors.
- Optional module/category filtering.
- Compact Resource and active Task views.
- Correlated diagnostic context.

### Not in Scope

- LLM ranking or autonomous planning algorithms.
- Model-specific token optimization inside the core.

---

# Phase 5 - Protocol Adapters

## 9. MCP Adapter

### Goal

Expose Axiom capabilities through MCP without changing Axiom core semantics.

### Scope

- Action-to-tool mapping.
- Resource exposure where compatible.
- Error and Task identity mapping.
- Request correlation.

---

## 10. CLI Adapter

### Goal

Provide a deterministic command-line interface generated from Introspection.

### Scope

- List Modules and Actions.
- Describe capability schemas.
- Invoke Actions.
- Inspect Resources and Tasks.
- Cancel Tasks.

The CLI should primarily validate that Axiom descriptors are sufficient for non-C++ consumers.

---

## 11. RPC / Remote Adapter

### Goal

Allow external processes to discover and invoke Axiom capabilities.

### Scope

- Protocol-neutral request/response boundary first.
- Concrete transport selected separately.
- Remote discovery.
- Action invocation.
- Task observation and cancellation.
- Resource identity transport rules.
- Request correlation and structured errors.

### Constraints

Networking must remain outside core runtime modules.

---

# Phase 6 - Security and Extension Boundaries

## 12. Authorization and Capability Policy

### Goal

Allow applications to control which callers may discover or invoke capabilities.

### Scope

- Caller identity abstraction.
- Capability-level authorization checks.
- Discovery filtering.
- Resource access policy hooks.
- Audit-friendly decision records.

### Constraints

- Authentication mechanisms belong to adapters.
- Core should model policy decisions, not OAuth/JWT/vendor-specific authentication.

---

## 13. Plugin and Extension Model

### Goal

Support dynamically delivered application capabilities after core ABI and lifetime rules are sufficiently stable.

### Scope

- Plugin identity and metadata.
- Explicit registration/unregistration lifecycle.
- Module, Resource and optional adapter contribution points.
- Failure isolation rules.
- Version and compatibility checks.

### Defer Until

- Public Action/Resource/Task contracts are stable.
- Shared-library ABI policy is mature enough.

Plugin loading should not be treated as an early prerequisite for AI-first capability discovery.

---

# Phase 7 - Persistence and Recovery

## 14. Persistent Metadata and Runtime State

### Goal

Allow selected runtime information to survive process restarts where the application requires it.

### Candidates

- Resource metadata.
- Task history.
- Audit records.
- Runtime configuration.

### Constraints

- Persistence must be opt-in.
- In-memory registries remain valid standalone implementations.
- Do not persist arbitrary C++ object ownership transparently.

---

## 15. Task Recovery Model

### Goal

Define how restartable or externally managed long-running work can be represented.

### Scope

- Distinguish ephemeral Tasks from recoverable jobs.
- Persistent task metadata.
- Resume/reconcile hooks.
- Explicit external execution ownership.

### Defer Until

The existing local Task lifecycle is proven stable in real applications.

---

# Phase 8 - Observability

## 16. Trace Model

### Goal

Extend structured logging correlation into a first-class execution trace model when real use cases justify it.

### Scope

- Trace/span identity.
- Action invocation spans.
- Task execution spans.
- Adapter request spans.
- Export hooks.

### Constraints

- Logging remains usable without tracing.
- OpenTelemetry or other tracing systems should be adapters/exporters, not core dependencies.

---

## 17. Runtime Diagnostics

### Goal

Provide structured information for humans, tools and agents to understand runtime failures and health.

### Scope

- Registration failures.
- Invocation diagnostics.
- Task failures.
- Resource lifetime/access failures.
- Optional runtime health summary.

---

# Phase 9 - Workflow and Automation

## 18. Workflow Composition

### Goal

Allow higher-level orchestration of existing Actions and Tasks without changing their core semantics.

### Scope

- Sequential composition.
- Dependency graphs.
- Conditional steps.
- Task-based long-running steps.
- Structured workflow state.

### Constraints

- Workflow is a higher-level module.
- Do not embed workflow semantics into Action Runtime or Task Registry.

---

## 19. Automation and Scheduling

### Goal

Build reusable automation on top of Scheduler, Actions and Tasks.

### Scope

- Scheduled Action invocation.
- Recurring operations.
- Trigger hooks.
- Observable automation state.

### Defer Until

Invocation correlation, task tracking and authorization semantics are stable.

---

# Phase 10 - Remote and Distributed Execution

## 20. Remote Execution Model

### Goal

Allow an Action request or Task to execute in another process or host while preserving Axiom identities and diagnostics.

### Scope

- Remote executor abstraction.
- Serializable invocation boundary.
- Task identity mapping.
- Cancellation propagation.
- Result and error transport.
- Trace correlation.

### Constraints

- Local execution remains the default.
- Distributed execution must not leak transport-specific concepts into Foundation or Action descriptors.

---

## 21. Distributed Resource References

### Goal

Define explicit semantics for Resources that are owned outside the current process.

### Scope

- Remote resource identity.
- Lifetime/lease semantics.
- Capability-safe access.
- Failure behavior when the owner disappears.

### Defer Until

Remote invocation and local Resource semantics are both mature.

---

# 22. Recommended Implementation Order

```text
1. Invocation Context / Execution Correlation
2. Canonical Introspection Schema
3. Introspection Query Improvements
4. Command Dispatch
5. Python Binding
6. Generic Agent Tool Adapter
7. Agent-Oriented Discovery Views
8. CLI Adapter
9. MCP Adapter
10. RPC / Remote Adapter
11. Authorization / Capability Policy
12. Runtime Trace and Diagnostics
13. Plugin / Extension Model
14. Persistent Metadata
15. Task Recovery
16. Workflow Composition
17. Automation / Scheduling
18. Remote Execution
19. Distributed Resources
```

The ordering is intentional: first make local application capabilities describable, correlated and language-accessible; then add AI and protocol adapters; only after those contracts stabilize should Axiom take on security, plugins, persistence, workflows and distributed execution.

---

# 23. Near-Term Focus

The immediate development backlog should remain limited to:

1. Command dispatch over existing Runtime, Introspection, Resource, and Task sources.
2. Python binding through the Command boundary.
3. Generic agent tool adapter.

Avoid starting plugin loading, distributed execution, persistence or workflow engines before these foundations have been exercised by real applications.

---

# 24. AI-First Definition for Axiom

Axiom is AI-first when an AI system can use the same stable runtime interfaces as any other caller to answer four questions:

```text
What can I do?       -> Action + Introspection
What objects exist?  -> Resource + Introspection
What is running?     -> Task + Introspection
What happened?       -> Logging + execution correlation
```

AI-first does not mean placing LLM logic inside the core. It means the application is inherently describable, invocable, observable and controllable through stable machine-readable runtime contracts.

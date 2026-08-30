# Axiom architecture

Axiom is one installable C++20 library exported as `Axiom::Axiom`. Its public
entry point is `<axiom/axiom.hpp>`; there is no `Core` component or compatibility
namespace.

```text
foundation <- action -> logging
foundation <- resource
foundation <- events
foundation <- async
```

`foundation` owns the only dynamic boundary model: `Value`, `Error`, `Result`,
and `TypeDescriptor`. `action` describes, registers, discovers, and synchronously
invokes C++ callables across that value boundary. Registry and dispatch machinery remain uninstalled implementation details.
Callable adaptation templates are installed under `axiom/action/detail/` because
consumer callables must instantiate them. They are not supported extension APIs;
library code and consumers use the same definitions.

`logging` is structured and independent from Action except that `Runtime` may
emit diagnostic records. `resource` owns typed host registrations and does not
depend on Action or logging. `events::Signal` is a thread-safe, ordered snapshot
publisher. `async::Executor` drains submitted work; `async::Scheduler` dispatches
timed callbacks through an Executor and requires that Executor to outlive it.

The library is static by default and supports shared builds. Public symbols use
`AXIOM_API`; dependencies, implementation headers, application code, and tests
are not part of the installed interface.

Async cancellation prevents future dispatches; work already accepted by an Executor
is drained by that Executor. A Scheduler does not own its Executor or wait for
callbacks on destruction. Captured state must outlive accepted work. Periodic
callbacks preserve their state and may overlap on multiple Executor workers.
Signal snapshots similarly retain the connected callable without copying its state;
concurrent emissions require callable-side synchronization. Callback and sink
ownership is released outside registration locks to allow destructor re-entry.

The quality adapters scan both public headers and implementations. Coverage
integrity checks every implementation file under `src/`; mutation integrity checks
that the report contains implementation mutants from this repository. The latter
is a presence check, not proof of complete per-file mutation coverage. Internal
Registry/Dispatcher unit tests compile those sources into the shared-build test
executable rather than exporting private library symbols.

# Axiom architecture

Axiom is one installable C++20 library exported as `Axiom::Axiom`. Its public
entry point is `<axiom/axiom.hpp>`; there is no `Core` component or compatibility
namespace.

```text
foundation <- action -> logging
foundation <- resource
foundation <- events
foundation <- async
task -> foundation / async / events / logging
introspection -> action / resource / task
```

`foundation` owns the only dynamic boundary model: `Value`, `Error`, `Result`,
and `TypeDescriptor`. `action` describes, registers, discovers, and synchronously
invokes C++ callables across that value boundary. Registry and dispatch machinery remain uninstalled implementation details.
Callable adaptation templates are installed under `axiom/action/detail/` and
`axiom/task/detail/` because consumer callables must instantiate them. They are
not supported extension APIs; library code and consumers use the same
definitions.

`introspection` is a top-level, read-only, non-owning, uncached aggregation layer
over Action, Resource, and Task discovery. Every query returns independently owned
values. `RuntimeSnapshot` samples module/action, then resource, then task in a
fixed order; it is not a global atomic snapshot across those sources. The three
sources must outlive an `IntrospectionService`, and source destruction must not
overlap a Service query.

`logging` is structured and independent from Action except that `Runtime` may
emit diagnostic records. `resource` owns typed host registrations and does not
depend on Action or logging. `events::Signal` is a thread-safe, ordered snapshot
publisher. `async::Executor` drains submitted work; `async::Scheduler` dispatches
timed callbacks through an Executor and requires that Executor to outlive it.

Runtime registration, discovery, lookup, and invocation support concurrent use;
ResourceRegistry operations likewise support concurrent use. Destruction of either
object still requires caller synchronization. Runtime resolves an Action before
executing it and never holds a registration or discovery lock around user code.
Calls to one Action may overlap and share its one stored callable instance, so the
callable and every mutable capture must provide their own synchronization.

Runtime publishes immutable registry States to make discovery and invocation
lock-free after snapshot acquisition. The current implementation copies the maps
when registering a Module, giving a sequence of registrations O(n²) aggregate map
copy work. This preserves all-or-nothing publication and stable descriptor
lifetimes; replacing it with shared-mutex-protected append-only maps is a future
performance option, provided lookup releases its lock before user invocation.

`task` owns tracked asynchronous work independently of Action invocation. A
`task::TaskRegistry` submits directly to an existing Executor and retains task
identities, coherent descriptor snapshots and terminal results until explicit
removal. It does not own or close the Executor, add another scheduler, or depend
on Resource. Task IDs are unique across registries in the loaded module, not
across processes or restarts. Handles retain execution state after removal or
Registry destruction; dropping a handle or Registry neither cancels nor joins
accepted work.

Task state is Pending, Running, Completed, Failed or Cancelled. Cancelling Pending
work prevents the queued wrapper from invoking its callable. Cancelling Running
work only sets a cooperative flag: the callable's returned Result decides its
terminal state. Success wins even after a cancellation request. Terminal state,
result and error are published together and never change again. Result queries
do not wait and return independent copies without executing user copies under
state locks. Progress owns its message and accepts finite values in [0, 1]; only
successful completion normalizes it to 1. The noncopyable TaskContext is valid
only during invocation, and progress reporting belongs to that execution thread.

Registry operations and handle observation/cancellation support concurrency;
destruction, assignment and moving an object require caller synchronization.
Registry and state locks never enclose business calls, notification callbacks,
logging sinks or destruction of user-owned objects. Per-task change snapshots
are queued in state-update order and delivered serially after committing state.
Different tasks can notify concurrently. Observers may query, cancel or disconnect
reentrantly; a fresh query can see a later state than the delivered snapshot.
The subscription adapter isolates each observer exception without changing
Signal's propagation contract. Registry destruction closes subscription entry;
already acquired notification snapshots may finish without a join.

Task execution installs scoped `task_id` and `task_name` fields on its execution
thread for the associated LoggingService. The scope restores previous context,
does not propagate to user-created threads, and follows normal logging field
precedence. Value and Resource handles are ordinary typed results: returning a
Resource handle does not extend its host registration lifetime.

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

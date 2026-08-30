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
invokes C++ callables across that value boundary. Its conversion and registry
machinery is implementation detail and is never installed.

`logging` is structured and independent from Action except that `Runtime` may
emit diagnostic records. `resource` owns typed host registrations and does not
depend on Action or logging. `events::Signal` is a thread-safe, ordered snapshot
publisher. `async::Executor` drains submitted work; `async::Scheduler` dispatches
timed callbacks through an Executor and requires that Executor to outlive it.

The library is static by default and supports shared builds. Public symbols use
`AXIOM_API`; dependencies, implementation headers, application code, and tests
are not part of the installed interface.

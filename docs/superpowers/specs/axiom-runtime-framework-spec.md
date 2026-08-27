# Axiom Module + Action Runtime Framework Specification

> Status: Draft v1.0
> Scope: MVP (Phase 1)
> Language: C++20
> Module: `src/runtime` → `Axiom::Runtime`

---

## 1. Overview

### 1.1 Goal

Axiom Runtime is a domain-agnostic application capability runtime. It organizes,
describes, registers, discovers, and invokes application capabilities through a
unified model:

- `Module` — capability namespace and organizational unit
- `Action` — minimal invocable capability
- `Descriptor` — declarative capability metadata
- `Value` — unified dynamic data model
- `Registry` — registration and discovery
- `Dispatcher` — invocation pipeline
- `InvocationContext` — per-call environment metadata
- `Result<T>` / `Error` — unified error model

Capabilities defined once can be exposed to any frontend (C++, Python, CLI, RPC,
Agent) without reimplementation.

### 1.2 Non-Goals

The Runtime does NOT know:

- What the application domain is (CAD, IDE, game, etc.)
- Who the caller is (Python, Agent, HTTP, test)
- What serialization protocol is used externally

### 1.3 Core Principle

> Define Once, Describe Once, Expose Everywhere.

---

## 2. Design Principles

### 2.1 Caller Agnosticism

Runtime receives only `ActionId + Arguments + InvocationContext`. No caller-specific
code paths exist:

```text
WRONG: invokeFromAgent(), invokeFromPython(), AgentAction, HttpAction
RIGHT: runtime.invoke(action_id, arguments, context)
```

### 2.2 Protocol Independence

JSON is the recommended external representation but NOT the internal data model.
All external formats convert to `Value` before entering the Runtime.

### 2.3 Two-Layer Interface

```text
Strongly Typed C++ Developer API   (compile-time safety, IDE support)
            │ adapter
Dynamic Runtime ABI                (cross-language, discovery, remote)
```

### 2.4 Synchronous-First

Actions are synchronous by default. No implicit threading, async, or concurrency.

---

## 3. Architecture

```text
                   Application
                       │ define
                       ▼
              ┌──────────────────┐
              │      Module      │
              │  Action × N      │
              └────────┬─────────┘
                       │ register
                       ▼
┌──────────────────────────────────────────────────────┐
│                    Axiom Runtime                     │
│                                                      │
│  ┌─────────────────┐    ┌────────────────────────┐  │
│  │ Module Registry │    │    Action Registry     │  │
│  └─────────────────┘    └────────────────────────┘  │
│                                                      │
│  ┌────────────────────────────────────────────────┐ │
│  │                Descriptor Model                │ │
│  └────────────────────────────────────────────────┘ │
│                                                      │
│  ┌────────────────────────────────────────────────┐ │
│  │                Dispatcher                      │ │
│  │  Resolve → Validate → Convert → Invoke → Wrap │ │
│  └────────────────────────────────────────────────┘ │
│                                                      │
│  ┌────────────────────────────────────────────────┐ │
│  │ Value / Arguments / Context / Error            │ │
│  └────────────────────────────────────────────────┘ │
└───────────────────────┬──────────────────────────────┘
                        │
          ┌─────────────┼──────────────┐
          ▼             ▼              ▼
        C++           Python          RPC/CLI/Agent (future)
```

### 3.1 Dependency Direction

```text
Frontend → Binding/Adapter → Runtime Core
```

Runtime Core MUST NOT depend on: Python, HTTP, Qt, Agent SDK, RPC libraries,
or any application-level module.

### 3.2 Module Placement (Project Convention)

```text
src/runtime/
├── CMakeLists.txt
├── include/axiom/runtime/
│   ├── runtime.hpp              # Runtime public facade
│   ├── value.hpp                # Value dynamic type
│   ├── result.hpp               # Result<T> + Error
│   ├── descriptor.hpp           # ActionDescriptor, ModuleDescriptor, ParameterDescriptor
│   ├── action.hpp               # IAction interface
│   ├── module.hpp               # Module, ModuleBuilder
│   ├── invocation_context.hpp   # InvocationContext
│   └── detail/
│       ├── value_converter.hpp  # ValueConverter<T> specializations
│       ├── function_traits.hpp  # Lambda/function signature introspection
│       └── action_adapter.hpp   # TypedActionAdapter<F>
└── src/
    ├── runtime.cpp
    ├── registry.cpp
    ├── dispatcher.cpp
    ├── value.cpp
    └── descriptor.cpp
```

Tests:

```text
tests/
└── runtime_test.cpp
```

---

## 4. Core Data Model

### 4.1 Value

Namespace: `axiom::runtime`

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace axiom::runtime {

enum class ValueType { Null, Boolean, Integer, Number, String, Array, Object };

class Value {
public:
    using Array = std::vector<Value>;
    using Object = std::unordered_map<std::string, Value>;

    Value() noexcept;
    Value(std::nullptr_t) noexcept;
    Value(bool value) noexcept;
    Value(std::int64_t value) noexcept;
    Value(int value) noexcept;
    Value(double value) noexcept;
    Value(std::string value) noexcept;
    Value(const char* value);
    Value(Array value) noexcept;
    Value(Object value) noexcept;

    [[nodiscard]] ValueType type() const noexcept;

    [[nodiscard]] bool isNull() const noexcept;
    [[nodiscard]] bool isBoolean() const noexcept;
    [[nodiscard]] bool isInteger() const noexcept;
    [[nodiscard]] bool isNumber() const noexcept;
    [[nodiscard]] bool isString() const noexcept;
    [[nodiscard]] bool isArray() const noexcept;
    [[nodiscard]] bool isObject() const noexcept;

    [[nodiscard]] bool asBoolean() const;
    [[nodiscard]] std::int64_t asInteger() const;
    [[nodiscard]] double asNumber() const;
    [[nodiscard]] const std::string& asString() const;
    [[nodiscard]] const Array& asArray() const;
    [[nodiscard]] const Object& asObject() const;

    [[nodiscard]] bool operator==(const Value& other) const noexcept;
    [[nodiscard]] bool operator!=(const Value& other) const noexcept;

private:
    using Storage = std::variant<
        std::nullptr_t,
        bool,
        std::int64_t,
        double,
        std::string,
        Array,
        Object>;

    Storage m_storage;
};

} // namespace axiom::runtime
```

Supported logical types (MVP):

| ValueType | C++ Storage     | JSON Equivalent |
| --------- | --------------- | --------------- |
| Null      | `nullptr_t`     | `null`          |
| Boolean   | `bool`          | `true/false`    |
| Integer   | `int64_t`       | `number` (int)  |
| Number    | `double`        | `number`        |
| String    | `string`        | `string`        |
| Array     | `vector<Value>` | `array`         |
| Object    | `unordered_map` | `object`        |

NOT included in MVP: binary, pointers, QObject*, shared_ptr, arbitrary C++ objects.

### 4.2 Arguments

```cpp
using Arguments = Value::Object;
```

Named-parameter protocol only. Positional (`std::vector<Value>`) is rejected for
better API evolution, error reporting, and cross-language compatibility.

---

## 5. Result / Error Model

### 5.1 Error

```cpp
#pragma once

#include <string>
#include <axiom/runtime/value.hpp>

namespace axiom::runtime {

enum class ErrorCode {
    ActionNotFound,
    InvalidArgument,
    MissingArgument,
    TypeMismatch,
    InvocationFailed,
    InternalError
};

struct Error {
    ErrorCode m_code = ErrorCode::InternalError;
    std::string m_message;
    std::string m_path;
    Value m_details;
};

} // namespace axiom::runtime
```

### 5.2 Result<T>

C++20 does not provide `std::expected`. A minimal `Result<T>` is implemented
internally using `std::variant`:

```cpp
#pragma once

#include <variant>
#include <axiom/runtime/error.hpp>

namespace axiom::runtime {

template<typename T>
class Result {
public:
    Result(T value);
    Result(Error error);

    [[nodiscard]] bool hasValue() const noexcept;
    explicit operator bool() const noexcept;

    [[nodiscard]] const T& value() const&;
    [[nodiscard]] T& value() &;
    [[nodiscard]] T&& value() &&;

    [[nodiscard]] const Error& error() const&;

private:
    std::variant<T, Error> m_storage;
};

template<>
class Result<void> {
public:
    Result();
    Result(Error error);

    [[nodiscard]] bool hasValue() const noexcept;
    explicit operator bool() const noexcept;

    [[nodiscard]] const Error& error() const&;

private:
    std::optional<Error> m_error;
};

} // namespace axiom::runtime
```

Design notes:

- `Result` is the ONLY error-propagation mechanism across the Runtime ABI boundary.
- No C++ exceptions may escape `IAction::invoke()` or `Runtime::invoke()`.
- When C++23 migration occurs, `Result<T>` can be aliased to `std::expected<T, Error>`.

---

## 6. Descriptor Model

### 6.1 ParameterDescriptor

```cpp
struct ParameterDescriptor {
    std::string m_name;
    ValueType m_type = ValueType::Null;
    std::string m_description;
    bool m_required = true;
    std::optional<Value> m_default_value;
};
```

### 6.2 ActionDescriptor

```cpp
struct ActionDescriptor {
    std::string m_name;
    std::string m_description;
    std::vector<ParameterDescriptor> m_parameters;
    ValueType m_return_type = ValueType::Null;
    std::string m_return_description;
    std::string m_version;
    std::vector<std::string> m_tags;
};
```

### 6.3 ModuleDescriptor

```cpp
struct ModuleDescriptor {
    std::string m_name;
    std::string m_description;
    std::string m_version;
};
```

Note: `ModuleDescriptor` does NOT embed `ActionDescriptor` list. Actions are
owned by the Registry. This avoids Module becoming a God Object.

---

## 7. ActionId

```cpp
#pragma once

#include <string>
#include <string_view>

namespace axiom::runtime {

class ActionId {
public:
    explicit ActionId(std::string_view full_id);
    ActionId(std::string_view module, std::string_view action);

    [[nodiscard]] std::string_view module() const noexcept;
    [[nodiscard]] std::string_view action() const noexcept;
    [[nodiscard]] const std::string& toString() const noexcept;
    [[nodiscard]] bool isValid() const noexcept;

    [[nodiscard]] bool operator==(const ActionId& other) const noexcept;

private:
    std::string m_module;
    std::string m_action;
};

} // namespace axiom::runtime
```

Format: `module.action` (e.g., `math.add`, `geometry.create_box`).

Naming convention: `lower_snake_case` for both module and action names.

Validation rules:

- Exactly one `.` separator
- Both parts non-empty
- Characters: `[a-z0-9_]`

---

## 8. IAction (Runtime ABI)

```cpp
#pragma once

#include <axiom/runtime/descriptor.hpp>
#include <axiom/runtime/invocation_context.hpp>
#include <axiom/runtime/result.hpp>
#include <axiom/runtime/value.hpp>

namespace axiom::runtime {

using Arguments = Value::Object;

class IAction {
public:
    virtual ~IAction() = default;

    [[nodiscard]] virtual const ActionDescriptor& descriptor() const noexcept = 0;

    virtual Result<Value> invoke(
        const Arguments& arguments,
        const InvocationContext& context) = 0;
};

} // namespace axiom::runtime
```

This is THE stable internal ABI. All typed implementations adapt to this interface.

---

## 9. InvocationContext

```cpp
#pragma once

#include <string>
#include <axiom/runtime/value.hpp>

namespace axiom::runtime {

struct InvocationContext {
    std::string m_request_id;
    std::string m_trace_id;
    std::string m_caller;
    Value::Object m_metadata;
};

} // namespace axiom::runtime
```

`m_caller` is informational metadata only. Runtime NEVER branches on it.

---

## 10. Registry

Internal implementation (not public API):

```cpp
namespace axiom::runtime::detail {

class Registry {
public:
    Result<void> registerAction(
        const ActionId& id,
        std::unique_ptr<IAction> action);

    [[nodiscard]] IAction* find(const ActionId& id) noexcept;
    [[nodiscard]] const IAction* find(const ActionId& id) const noexcept;

    [[nodiscard]] const ActionDescriptor* describe(const ActionId& id) const noexcept;

    [[nodiscard]] std::vector<ActionId> listActions() const;
    [[nodiscard]] std::vector<ActionId> listActions(std::string_view module) const;

    Result<void> registerModule(const ModuleDescriptor& descriptor);
    [[nodiscard]] const ModuleDescriptor* findModule(std::string_view name) const noexcept;

private:
    std::unordered_map<std::string, std::unique_ptr<IAction>> m_actions;
    std::unordered_map<std::string, ModuleDescriptor> m_modules;
};

} // namespace axiom::runtime::detail
```

Responsibilities:

- Registration with conflict detection (duplicate ActionId → error)
- Lookup by ActionId
- Descriptor queries
- Ownership of Action instances
- Module metadata tracking

NOT responsible for: parameter conversion, action execution, error mapping.

---

## 11. Dispatcher

Internal implementation (not public API):

```cpp
namespace axiom::runtime::detail {

class Dispatcher {
public:
    explicit Dispatcher(Registry& registry);

    Result<Value> invoke(
        const ActionId& id,
        const Arguments& arguments,
        const InvocationContext& context);

private:
    Registry& m_registry;
};

} // namespace axiom::runtime::detail
```

Invocation pipeline:

```text
invoke("math.add", args, ctx)
    │
    ├── 1. Resolve: Registry::find(id) → IAction*
    │       fail → Error{ActionNotFound}
    │
    ├── 2. Validate: check required params present, no unknown params
    │       fail → Error{MissingArgument, path="param_name"}
    │
    ├── 3. Delegate to IAction::invoke(args, ctx)
    │       (typed adapter handles Value→T conversion internally)
    │
    ├── 4. Exception boundary: catch all → Error{InvocationFailed}
    │
    └── 5. Return Result<Value>
```

The Dispatcher does NOT perform type conversion itself. Conversion lives in the
TypedActionAdapter (Section 13). Dispatcher handles: resolution, presence
validation, exception boundary, error normalization.

---

## 12. Runtime (Public Facade)

```cpp
#pragma once

#include <memory>
#include <string_view>
#include <axiom/runtime/descriptor.hpp>
#include <axiom/runtime/invocation_context.hpp>
#include <axiom/runtime/module.hpp>
#include <axiom/runtime/result.hpp>
#include <axiom/runtime/value.hpp>

namespace axiom::runtime {

class Runtime {
public:
    Runtime();
    ~Runtime();

    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    ModuleBuilder module(std::string name, std::string description = {});

    [[nodiscard]] const ActionDescriptor* describe(std::string_view action_id) const noexcept;

    Result<Value> invoke(
        std::string_view action_id,
        const Arguments& arguments = {},
        InvocationContext context = {});

    [[nodiscard]] std::vector<ActionId> listActions() const;
    [[nodiscard]] std::vector<ActionId> listActions(std::string_view module) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace axiom::runtime
```

Public API surface: `module()`, `describe()`, `invoke()`, `listActions()`.
Everything else is internal.

---

## 13. Typed Action Adapter

### 13.1 ValueConverter

```cpp
namespace axiom::runtime::detail {

template<typename T>
struct ValueConverter {
    static Result<T> fromValue(const Value& value, std::string_view path);
    static Value toValue(T value);
};

// Specializations for: bool, int, int64_t, double, std::string,
//                      std::vector<T>, std::unordered_map<std::string, T>

} // namespace axiom::runtime::detail
```

Each specialization produces precise `Error::m_path` on failure (e.g., `"a"`,
`"shape.size.x"` for nested objects).

### 13.2 FunctionTraits

```cpp
namespace axiom::runtime::detail {

template<typename F>
struct FunctionTraits;

// Extracts: ReturnType, Args... from:
//   - function pointers: R(*)(A, B)
//   - member function pointers: R(C::*)(A, B) [const]
//   - lambdas/functors: via operator() introspection

} // namespace axiom::runtime::detail
```

### 13.3 TypedActionAdapter

```cpp
namespace axiom::runtime::detail {

template<typename F, typename... Params>
class TypedActionAdapter final : public IAction {
public:
    TypedActionAdapter(
        ActionDescriptor descriptor,
        F callable,
        std::tuple<Params...> params);

    [[nodiscard]] const ActionDescriptor& descriptor() const noexcept override;

    Result<Value> invoke(
        const Arguments& arguments,
        const InvocationContext& context) override;

private:
    ActionDescriptor m_descriptor;
    F m_callable;
    std::tuple<Params...> m_params;
};

} // namespace axiom::runtime::detail
```

`invoke()` implementation:

1. For each parameter: extract `arguments.at(name)` → `ValueConverter<P>::fromValue()`
2. Apply default values for missing optional params
3. Call `m_callable(converted_args...)`
4. Wrap return via `ValueConverter<R>::toValue()`
5. Return `Result<Value>`
6. Exception boundary: catch `std::exception` → `InvocationFailed`, catch `...` → `InternalError`

### 13.4 Parameter Builder

```cpp
namespace axiom::runtime {

template<typename T>
ParameterDescriptor param(std::string name, std::string description = {}) {
    return ParameterDescriptor{
        .m_name = std::move(name),
        .m_type = ValueTypeOf<T>,
        .m_description = std::move(description),
        .m_required = true};
}

template<typename T>
ParameterDescriptor optional_param(std::string name, std::string description = {}) {
    return ParameterDescriptor{
        .m_name = std::move(name),
        .m_type = ValueTypeOf<T>,
        .m_description = std::move(description),
        .m_required = false};
}

} // namespace axiom::runtime
```

`ValueTypeOf<T>` is a compile-time mapping trait:

| T                            | ValueType |
| ---------------------------- | --------- |
| `bool`                       | Boolean   |
| `int`, `int64_t`             | Integer   |
| `double`, `float`            | Number    |
| `std::string`, `const char*` | String    |
| `std::vector<T>`             | Array     |
| `std::unordered_map<...>`    | Object    |

---

## 14. Module & ModuleBuilder

```cpp
#pragma once

#include <axiom/runtime/descriptor.hpp>
#include <axiom/runtime/result.hpp>

namespace axiom::runtime {

class ModuleBuilder {
public:
    template<typename F, typename... Params>
    Result<void> action(
        std::string name,
        std::string description,
        F callable,
        Params... params);
};

} // namespace axiom::runtime
```

Registration example:

```cpp
auto math = runtime.module("math", "Basic mathematical operations");

math.action(
    "add",
    "Add two numbers",
    [](double a, double b) { return a + b; },
    param<double>("a", "First value"),
    param<double>("b", "Second value"));
```

Member function registration:

```cpp
geometry.action(
    "translate",
    "Translate an object",
    &GeometryService::translate,
    param<std::string>("object_id"),
    param<double>("x"),
    param<double>("y"),
    param<double>("z"));
```

Note: For member function pointers, the ModuleBuilder must be bound to an object
instance. This is resolved via a capturing overload:

```cpp
template<typename C, typename R, typename... Args, typename... Params>
Result<void> action(
    std::string name,
    std::string description,
    C& instance,
    R (C::*method)(Args...),
    Params... params);
```

---

## 15. Invocation Flow (Complete)

```text
runtime.invoke("math.add", {{"a", 10}, {"b", 20}})
    │
    ▼
Runtime::invoke
    │
    ▼
Dispatcher::invoke(ActionId("math", "add"), args, ctx)
    │
    ├── Registry::find → IAction* (TypedActionAdapter)
    │       not found → Result(Error{ActionNotFound, "math.add"})
    │
    ├── Validate presence: "a" ✓, "b" ✓
    │       missing → Result(Error{MissingArgument, path="a"})
    │
    ▼
TypedActionAdapter::invoke(args, ctx)
    │
    ├── ValueConverter<double>::fromValue(args["a"]) → 10.0
    │       type error → Result(Error{TypeMismatch, path="a"})
    ├── ValueConverter<double>::fromValue(args["b"]) → 20.0
    │
    ├── callable(10.0, 20.0) → 30.0
    │       exception → Result(Error{InvocationFailed, e.what()})
    │
    ├── ValueConverter<double>::toValue(30.0) → Value{30.0}
    │
    ▼
Result<Value>{Value{30.0}}
```

---

## 16. Exception Boundary

No C++ exception escapes the Runtime ABI:

```cpp
Result<Value> TypedActionAdapter::invoke(const Arguments& arguments,
                                         const InvocationContext& context) {
    try {
        // ... conversion + call ...
    } catch(const std::exception& e) {
        return Error{ErrorCode::InvocationFailed, e.what(), {}, {}};
    } catch(...) {
        return Error{ErrorCode::InternalError, "Unknown action failure", {}, {}};
    }
}
```

---

## 17. Error Path Precision

Validation errors MUST include precise `m_path`:

```text
Input:  {"shape": {"size": {"x": "abc"}}}
Error:  {m_code=TypeMismatch, m_message="Expected number", m_path="shape.size.x"}
```

For top-level parameters: `m_path = "param_name"`.

---

## 18. Thread Model (MVP)

- All invocations are synchronous on the calling thread.
- Runtime itself is NOT thread-safe for concurrent registration + invocation.
- Registration must complete before concurrent invocations begin.
- Thread-safe invocation of registered actions: guaranteed (actions are immutable
  post-registration; `invoke()` is logically `const`).

Future (Phase 3): `AsyncAction`, `CancellationToken`, `Progress`.

---

## 19. Lifecycle & Ownership

```text
Runtime (owns) → Registry (owns) → IAction instances (unique_ptr)
```

- Actions are owned by the Registry via `std::unique_ptr<IAction>`.
- ModuleBuilder holds a non-owning reference back to Runtime's Registry.
- Runtime must outlive all invocations.
- No global singletons, no dangling references, no shared_ptr ownership ambiguity.

---

## 20. Naming Conventions

| Element         | Convention         | Example               |
| --------------- | ------------------ | --------------------- |
| Module names    | `lower_snake_case` | `geometry`, `math`    |
| Action names    | `lower_snake_case` | `create_box`, `add`   |
| Full ActionId   | `module.action`    | `geometry.create_box` |
| Parameter names | `lower_snake_case` | `object_id`, `angle`  |

---

## 21. Build Integration

### 21.1 CMake Target

```cmake
# src/runtime/CMakeLists.txt
add_library(axiom_runtime STATIC
    src/runtime.cpp
    src/registry.cpp
    src/dispatcher.cpp
    src/value.cpp
    src/descriptor.cpp)
add_library(Axiom::Runtime ALIAS axiom_runtime)

axiom_configure_language_diagnostics(axiom_runtime)
axiom_configure_static_analyzers(axiom_runtime)
axiom_configure_sanitizers(axiom_runtime)
axiom_configure_coverage(axiom_runtime)
axiom_configure_mutation_testing(axiom_runtime)
target_compile_features(axiom_runtime PUBLIC cxx_std_20)
target_include_directories(
    axiom_runtime
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
           "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
target_compile_definitions(axiom_runtime PRIVATE AXIOM_RUNTIME_BUILD)

set_target_properties(axiom_runtime PROPERTIES EXPORT_NAME Runtime
                                               POSITION_INDEPENDENT_CODE ON)
```

### 21.2 Root CMakeLists Integration

```cmake
add_subdirectory(src/core)
add_subdirectory(src/runtime)   # NEW
```

Install rules extended for `axiom_runtime` target and headers.

### 21.3 Architecture Rules

Add to `quality/architecture_rules.json`:

```json
{
    "id": "runtime-must-not-depend-on-app-or-test",
    "from": ["src/runtime/"],
    "forbidden": ["apps/", "tests/"]
},
{
    "id": "runtime-must-not-depend-on-other-modules",
    "from": ["src/runtime/"],
    "forbidden": ["src/core/"]
}
```

Runtime is a standalone lower-level module with no dependency on `core` or
application code.

### 21.4 Dependencies

MVP has ZERO third-party production dependencies. `Value` is implemented with
`std::variant`. No nlohmann::json, no Boost.

---

## 22. Testing Strategy

Test file: `tests/runtime_test.cpp`

```cpp
#include <axiom/runtime/runtime.hpp>
#include <gtest/gtest.h>
```

### 22.1 Test Categories

| Category              | Tests                                                       |
| --------------------- | ----------------------------------------------------------- |
| Value                 | Construction, type queries, accessors, equality, nesting    |
| Result                | Value path, error path, void specialization                 |
| ActionId              | Parsing, validation, module/action extraction               |
| Descriptor            | ParameterDescriptor, ActionDescriptor construction          |
| Registry              | Register, find, conflict rejection, list                    |
| Dispatcher            | Resolve, validate, invoke, error paths                      |
| TypedActionAdapter    | Lambda adaptation, member fn adaptation, conversion errors  |
| Runtime (integration) | End-to-end: module → action → invoke → result               |
| Exception boundary    | Throwing action → InvocationFailed, unknown → InternalError |
| Error path precision  | Nested path reporting                                       |

### 22.2 Naming Convention

```cpp
TEST(RuntimeValue, ConstructFromDoubleReportsNumberType) { ... }
TEST(RuntimeInvoke, MissingRequiredArgumentReturnsError) { ... }
```

---

## 23. Public API Summary

### Exposed (stable surface):

```text
Runtime::module()
Runtime::describe()
Runtime::invoke()
Runtime::listActions()
ModuleBuilder::action()
param<T>() / optional_param<T>()
Value, Arguments
Result<T>, Error, ErrorCode
ActionDescriptor, ParameterDescriptor, ModuleDescriptor
InvocationContext
ActionId
ValueType
```

### Internal (detail namespace, no stability guarantee):

```text
detail::Registry
detail::Dispatcher
detail::ValueConverter<T>
detail::FunctionTraits<F>
detail::TypedActionAdapter<F, Params...>
```

---

## 24. JSON Representation (Phase 2, Reference)

Descriptor → JSON Schema output (future `toJson()` utility):

```json
{
  "module": "math",
  "name": "add",
  "description": "Add two numbers",
  "parameters": {
    "type": "object",
    "properties": {
      "a": { "type": "number", "description": "First value" },
      "b": { "type": "number", "description": "Second value" }
    },
    "required": ["a", "b"]
  },
  "returns": { "type": "number" }
}
```

NOT part of MVP. Included for architectural direction only.

---

## 25. Phased Delivery

### Phase 1 — MVP (This Spec)

- [x] Value (7 JSON-compatible types)
- [x] Result<T> / Error
- [x] ActionId
- [x] Descriptors (Parameter, Action, Module)
- [x] IAction interface
- [x] InvocationContext
- [x] Registry (internal)
- [x] Dispatcher (internal)
- [x] ValueConverter (bool, int64, double, string, vector, map)
- [x] FunctionTraits
- [x] TypedActionAdapter (lambdas + member functions)
- [x] ModuleBuilder + param<T>()
- [x] Runtime facade (module, describe, invoke, listActions)
- [x] Exception boundary
- [x] Error path precision
- [x] C++ invoke end-to-end

### Phase 2 — Extensions

- JSON serialization of Value/Descriptor
- Python binding (pybind11/nanobind)
- Python natural API (`axiom.math.add(a=10, b=20)`)
- JSON Schema generation
- Enum support (string-based)
- `std::optional<T>` parameters
- Struct/Object mapping

### Phase 3 — Advanced

- CLI frontend
- RPC/HTTP adapter
- Agent tool schema generation
- Async actions, cancellation, progress
- Permission system
- Tracing/metrics
- Action middleware

---

## 26. Constraints & Quality Gates

All code must pass:

```bash
uv run --quiet python tools/check.py fast       # per step
uv run --quiet python tools/check.py hardening  # task complete
uv run --quiet python tools/check.py full       # before merge
```

Specific constraints:

- Cyclomatic complexity ≤ 10 per function
- Function length ≤ 80 statements
- Parameters ≤ 5 per function (variadic templates exempt by clang-tidy counting)
- clang-format enforced (LLVM style, 4-space, 100-col, no space before parens)
- Naming: classes `CamelCase`, functions `camelBack`, members `m_camelBack`,
  params/locals `lower_case`, constants `UPPER_CASE`
- Coverage ≥ 90% line coverage
- No warnings with `-Wall -Wextra -Wpedantic -Werror`
- No exceptions across ABI boundary

---

## 27. Implementation Order

Recommended incremental build sequence (each step independently verifiable):

1. **Value** — type, constructors, accessors, equality → `fast`
2. **Result/Error** — Result<T>, ErrorCode, Error → `fast`
3. **ActionId** — parsing, validation → `fast`
4. **Descriptors** — Parameter/Action/Module descriptors → `fast`
5. **ValueConverter** — all MVP type specializations → `fast`
6. **FunctionTraits** — lambda/member fn introspection → `fast`
7. **TypedActionAdapter** — core adaptation logic → `fast`
8. **Registry** — register/find/list/conflict → `fast`
9. **Dispatcher** — resolve/validate/invoke/exception boundary → `fast`
10. **Runtime + ModuleBuilder** — public facade, param<T>() → `fast`
11. **Integration tests** — end-to-end scenarios → `hardening`
12. **Build/install integration** — CMake, architecture rules → `full`

---

## 28. One-Line Architecture

> Axiom Runtime is a `Module + Action` based universal application capability
> runtime. Applications organize capabilities via Modules, define minimal
> invocable units as Actions. The Runtime uses unified Descriptor, Value,
> Arguments, Context, and Error models for registration, discovery, and
> dispatch, exposing the same capability definition to C++, Python, RPC, CLI,
> and Agent through adapters.

Core invariants:

```cpp
// THE stable entry point:
Result<Value> Runtime::invoke(
    std::string_view action_id,
    const Arguments& arguments,
    InvocationContext context);

// THE stable action ABI:
Result<Value> IAction::invoke(
    const Arguments& arguments,
    const InvocationContext& context);
```

> The Runtime does not know what the business is, nor who the caller is.

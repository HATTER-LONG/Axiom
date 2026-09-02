"""Action runtime surface: discovery, describe, invoke, errors, context and lifecycle."""

import os
import sys
import threading

import axiom

sys.path.insert(0, os.environ["AXIOM_PYTHON_TEST_HOST_DIR"])
import axiom_python_test_host


def expect_axiom_error(operation, code=None):
    try:
        operation()
    except axiom.AxiomError as error:
        assert str(error) == error.message
        if code is not None:
            assert error.code == code, (error.code, code)
        return error
    raise AssertionError("expected AxiomError")


def expect_raises(exc_type, operation):
    try:
        operation()
    except exc_type as error:
        return error
    raise AssertionError(f"expected {exc_type}")


expected_codes = {
    "InvalidArgument",
    "MissingArgument",
    "UnknownArgument",
    "TypeMismatch",
    "NotFound",
    "AlreadyExists",
    "InvalidDescriptor",
    "InvocationFailed",
    "InternalError",
    "Cancelled",
}
members = set(axiom.ErrorCode.__members__)
assert members == expected_codes, members

action_id = axiom.ActionId("mesh.rebuild")
assert action_id.module == "mesh"
assert action_id.action == "rebuild"
assert str(action_id) == "mesh.rebuild"
assert repr(action_id) == "ActionId('mesh.rebuild')"
expect_axiom_error(lambda: axiom.ActionId("not canonical"), axiom.ErrorCode.InvalidArgument)
expect_axiom_error(lambda: axiom.ActionId.parse("nope"), axiom.ErrorCode.InvalidArgument)

try:
    axiom.Runtime()
except TypeError:
    pass
else:
    raise AssertionError("Runtime must be supplied by a C++ host")

expect_raises(ValueError, lambda: axiom._attach(None, None, None))
expect_raises(TypeError, lambda: axiom._attach(1, 2, 3))

host = axiom_python_test_host.create_host()
runtime = host.runtime

expect_raises(TypeError, lambda: axiom._attach(runtime, runtime, runtime))

namespaces = {module.namespace_name for module in runtime.modules()}
assert namespaces == {"math", "mesh"}, namespaces
mesh_module = runtime.describe_module("mesh")
assert mesh_module.description == "Test mesh actions"
assert mesh_module.version == "1.0.0"
assert mesh_module.tags == ["geometry"]
assert runtime.describe_module("math").version is None
expect_axiom_error(lambda: runtime.describe_module("missing"), axiom.ErrorCode.NotFound)

action_ids = {str(action.id) for action in runtime.actions()}
assert "math.add" in action_ids and "mesh.rebuild" in action_ids

rebuild = runtime.describe_action("mesh.rebuild")
assert rebuild.description == "Rebuilds the mesh"
assert rebuild.version == "1.2.0"
assert rebuild.tags == ["geometry", "write"]
assert rebuild.metadata == {"author": "test"}
assert len(rebuild.parameters) == 1
quality = rebuild.parameters[0]
assert quality.name == "quality"
assert quality.required is False
assert quality.has_default_value is True
assert quality.default_value == 0.5
assert quality.type.kind == axiom.TypeDescriptor.Kind.Number
return_type = rebuild.return_type
assert return_type.kind == axiom.TypeDescriptor.Kind.Object
assert return_type.fields == {}
assert return_type.value_type.kind == axiom.TypeDescriptor.Kind.Array
assert return_type.value_type.element_type.kind == axiom.TypeDescriptor.Kind.Integer

add = runtime.describe_action(axiom.ActionId("math.add"))
assert all(parameter.required for parameter in add.parameters)
assert all(not parameter.has_default_value for parameter in add.parameters)
assert all(parameter.default_value is None for parameter in add.parameters)

# Returned descriptor containers are copies; mutating them must not leak into Core.
rebuild.tags.append("polluted")
rebuild.metadata["polluted"] = "yes"
fresh = runtime.describe_action("mesh.rebuild")
assert fresh.tags == ["geometry", "write"]
assert fresh.metadata == {"author": "test"}

expect_axiom_error(lambda: runtime.describe_action("missing.action"), axiom.ErrorCode.NotFound)

assert runtime.invoke("math.add", {"left": 2, "right": 3}) == 5
assert runtime.invoke(axiom.ActionId("math.add"), {"left": -2, "right": 3}) == 1
assert runtime.invoke("mesh.rebuild", {}) == {"points": [5, 2, 3]}
assert runtime.invoke("mesh.rebuild", {"quality": 0.9}) == {"points": [9, 2, 3]}

expect_axiom_error(lambda: runtime.invoke("math.add", {"left": 1}),
                   axiom.ErrorCode.MissingArgument)
expect_axiom_error(lambda: runtime.invoke("math.add", {"left": 1, "right": 2, "extra": 3}),
                   axiom.ErrorCode.UnknownArgument)
error = expect_axiom_error(lambda: runtime.invoke("math.add", {"left": "x", "right": 1}),
                           axiom.ErrorCode.TypeMismatch)
assert error.path == "left"
expect_axiom_error(lambda: runtime.invoke("missing.action", {}), axiom.ErrorCode.NotFound)
expect_axiom_error(lambda: runtime.invoke("not canonical", {}), axiom.ErrorCode.InvalidArgument)
expect_raises(TypeError, lambda: runtime.invoke("math.add", [("left", 1)]))
expect_raises(TypeError, lambda: runtime.invoke("math.add", {1: 2}))

error = expect_axiom_error(lambda: runtime.invoke("math.fail_details", {}),
                           axiom.ErrorCode.InvocationFailed)
assert error.message == "structured failure"
assert error.path == "shape.size.x"
assert error.details == {"actual": "string", "expected": "integer"}
assert error.has_details is True

error = expect_axiom_error(lambda: runtime.invoke("math.fail_null_details", {}),
                           axiom.ErrorCode.InvalidArgument)
assert error.details is None
assert error.has_details is True
assert error.path is None

error = expect_axiom_error(lambda: runtime.invoke("math.fail_plain", {}),
                           axiom.ErrorCode.AlreadyExists)
assert error.details is None
assert error.has_details is False

expect_axiom_error(lambda: runtime.invoke("math.throw_runtime", {}),
                   axiom.ErrorCode.InvocationFailed)
expect_axiom_error(lambda: runtime.invoke("math.throw_unknown", {}),
                   axiom.ErrorCode.InternalError)

context = axiom.InvocationContext(
    request_id="req-1", trace_id="trace-9", caller="python", metadata={"k": "v", "a": "b"})
assert context.request_id == "req-1"
echoed = runtime.invoke("math.context_echo", {}, context)
assert echoed == {
    "action_id": "math.context_echo",
    "request_id": "req-1",
    "trace_id": "trace-9",
    "caller": "python",
    "meta.a": "b",
    "meta.k": "v",
}
echoed_default = runtime.invoke("math.context_echo", {})
assert echoed_default["request_id"] == ""
assert echoed_default["caller"] == ""
expect_raises(TypeError, lambda: axiom.InvocationContext(metadata={"a": 1}))

axiom_python_test_host.schedule_invoke_gate_release()
assert runtime.invoke("math.wait_gate", {}) == "done"

# Multiple Python threads share one Runtime; every call must stay correct even
# though the GIL serializes the calls.
failures = []
barrier = threading.Barrier(4)


def worker(index):
    try:
        barrier.wait()
        for step in range(50):
            result = runtime.invoke("math.add", {"left": index, "right": step})
            assert result == index + step
            modules = runtime.modules()
            assert {module.namespace_name for module in modules} == {"math", "mesh"}
    except Exception as failure:  # noqa: BLE001 - report across threads
        failures.append(failure)


threads = [threading.Thread(target=worker, args=(index,)) for index in range(4)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join()
assert failures == [], failures

# Wrappers keep the host context (and therefore the Core sources) alive after the
# Host object itself is released.
introspection = host.introspection
del host
assert {module.namespace_name for module in runtime.modules()} == {"math", "mesh"}
assert len(introspection.actions()) == len(runtime.actions())

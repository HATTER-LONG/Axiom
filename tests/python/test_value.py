"""Value conversion boundary checks across the invoke surface."""

import math
import os
import sys

import axiom

sys.path.insert(0, os.environ["AXIOM_PYTHON_TEST_HOST_DIR"])
import axiom_python_test_host

host = axiom_python_test_host.create_host()
runtime = host.runtime


def invoke(action, arguments):
    return runtime.invoke(action, arguments)


def expect_raises(exc_type, operation):
    try:
        operation()
    except exc_type as error:
        return error
    raise AssertionError(f"expected {exc_type}")


assert invoke("math.echo_bool", {"flag": True}) is True
assert invoke("math.echo_bool", {"flag": False}) is False

assert invoke("math.add", {"left": 2, "right": 3}) == 5
assert invoke("math.add", {"left": -(2**63), "right": 0}) == -(2**63)
assert invoke("math.add", {"left": 2**63 - 1, "right": 0}) == 2**63 - 1
expect_raises(OverflowError, lambda: invoke("math.add", {"left": 2**63, "right": 0}))
expect_raises(OverflowError, lambda: invoke("math.add", {"left": -(2**63) - 1, "right": 0}))

assert invoke("math.echo_number", {"x": 3.25}) == 3.25
assert math.isinf(invoke("math.echo_number", {"x": math.inf}))
assert math.isnan(invoke("math.echo_number", {"x": math.nan}))

assert invoke("math.echo_string", {"text": "héllo 中文 🚀"}) == "héllo 中文 🚀"
assert invoke("math.echo_string", {"text": ""}) == ""

assert invoke("math.echo_list", {"values": []}) == []
assert invoke("math.echo_list", {"values": [1, 2, 3]}) == [1, 2, 3]
assert invoke("math.echo_list", {"values": [-(2**63), 2**63 - 1]}) == [-(2**63), 2**63 - 1]
expect_raises(OverflowError, lambda: invoke("math.echo_list", {"values": [2**63]}))

assert invoke("math.echo_map", {"table": {}}) == {}
assert invoke("math.echo_map", {"table": {"a": "x", "b": "y"}}) == {"a": "x", "b": "y"}

nested = {"points": [1, 2, 3], "empty": []}
assert invoke("math.echo_nested", {"table": nested}) == nested

shared = [1, 2]
assert invoke("math.echo_nested", {"table": {"a": shared, "b": shared}}) == {"a": [1, 2], "b": [1, 2]}

# Python bool is a distinct Value Boolean, never an Integer for integer targets.
error = expect_raises(axiom.AxiomError, lambda: invoke("math.add", {"left": True, "right": 1}))
assert error.code == axiom.ErrorCode.TypeMismatch

expect_raises(TypeError, lambda: invoke("math.echo_list", {"values": (1, 2)}))


class Custom:
    pass


expect_raises(TypeError, lambda: invoke("math.echo_number", {"x": Custom()}))
expect_raises(TypeError, lambda: invoke("math.echo_map", {"table": {1: "x"}}))
expect_raises(TypeError, lambda: invoke("math.add", {1: 2}))

cycle = []
cycle.append(cycle)
expect_raises(ValueError, lambda: invoke("math.echo_list", {"values": cycle}))
dict_cycle = {}
dict_cycle["self"] = dict_cycle
expect_raises(ValueError, lambda: invoke("math.echo_map", {"table": dict_cycle}))
indirect = []
inner = [indirect]
indirect.append(inner)
expect_raises(ValueError, lambda: invoke("math.echo_nested", {"table": {"a": indirect}}))

expect_raises(UnicodeError, lambda: invoke("math.bad_utf8", {}))

# Null round-trips: C++ Null results surface as Python None, and None is rejected
# for non-nullable typed parameters with a Core TypeMismatch.
task = host.task(
    next(t.id for t in host.introspection.tasks() if t.name == "completed-void"))
assert task.result() is None
assert task.state() == axiom.TaskState.Completed
error = expect_raises(axiom.AxiomError, lambda: invoke("math.echo_string", {"text": None}))
assert error.code == axiom.ErrorCode.TypeMismatch

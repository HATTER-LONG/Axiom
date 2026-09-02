"""Public-surface smoke checks for the optional host-injected extension."""

import os
import sys

import axiom

sys.path.insert(0, os.environ["AXIOM_PYTHON_TEST_HOST_DIR"])
import axiom_python_test_host


def expect_axiom_error(operation):
    try:
        operation()
    except axiom.AxiomError as error:
        assert error.code == axiom.ErrorCode.InvalidArgument
        assert str(error) == error.message
        assert error.path is None
        assert error.has_details is False
    else:
        raise AssertionError("expected AxiomError")


assert axiom.ActionId("mesh.rebuild").module == "mesh"
assert str(axiom.ResourceId("mesh:1")) == "mesh:1"
assert str(axiom.TaskId("task:1")) == "task:1"
expect_axiom_error(lambda: axiom.ActionId("not canonical"))

try:
    axiom.Runtime()
except TypeError:
    pass
else:
    raise AssertionError("Runtime must be supplied by a C++ host")

host = axiom_python_test_host.create_host()
namespaces = {item.namespace_name for item in host.runtime.modules()}
assert namespaces == {"math", "mesh"}, namespaces
assert host.runtime.invoke("math.add", {"left": 2, "right": 3}) == 5
assert host.runtime.invoke(axiom.ActionId("math.add"), {"left": -2, "right": 3}) == 1

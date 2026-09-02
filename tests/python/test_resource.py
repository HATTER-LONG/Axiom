"""Resource discovery, describe, and ID value semantics."""

import os
import sys

import axiom

sys.path.insert(0, os.environ["AXIOM_PYTHON_TEST_HOST_DIR"])
import axiom_python_test_host

host = axiom_python_test_host.create_host()
introspection = host.introspection


def expect_axiom_error(operation, code=None):
    try:
        operation()
    except axiom.AxiomError as error:
        if code is not None:
            assert error.code == code, (error.code, code)
        return error
    raise AssertionError("expected AxiomError")


resources = introspection.resources()
assert [(str(resource.id), resource.type) for resource in resources] == [
    ("geometry:3", "geometry"),
    ("widget:1", "widget"),
    ("widget:2", "widget"),
]

widget = introspection.describe_resource(axiom.ResourceId("widget:1"))
assert widget.type == "widget"
assert str(widget.id) == "widget:1"
expect_axiom_error(lambda: introspection.describe_resource(axiom.ResourceId("widget:99")),
                   axiom.ErrorCode.NotFound)

try:
    widget.type = "polluted"
except AttributeError:
    pass
else:
    raise AssertionError("ResourceDescriptor fields must be read-only")

identity = axiom.ResourceId("widget:1")
assert str(identity) == "widget:1"
assert repr(identity) == "ResourceId('widget:1')"
assert axiom.ResourceId.parse("widget:1") == identity
expect_axiom_error(lambda: axiom.ResourceId("not canonical"), axiom.ErrorCode.InvalidArgument)

second = axiom.ResourceId("widget:2")
assert identity == axiom.ResourceId("widget:1")
assert identity != second
assert identity < second
assert second > identity
assert identity <= identity
assert second >= identity
assert sorted([second, identity]) == [identity, second]
assert hash(identity) == hash(axiom.ResourceId("widget:1"))
assert len({identity, axiom.ResourceId("widget:1"), second}) == 2

# Distinct ID types are unequal rather than raising, but remain unordered.
assert (identity == axiom.TaskId("task:1")) is False
assert (identity != axiom.TaskId("task:1")) is True
try:
    identity < axiom.TaskId("task:1")
except TypeError:
    pass
else:
    raise AssertionError("ordering across ID types must be rejected")

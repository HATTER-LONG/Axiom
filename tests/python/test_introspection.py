"""Introspection queries, ordering, deep copies and snapshot semantics."""

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


def action_names(actions):
    return [str(action.id) for action in actions]


assert {module.namespace_name for module in introspection.modules()} == {"math", "mesh"}

everything = introspection.actions()
assert introspection.actions(None) is not None
assert action_names(introspection.actions()) == action_names(everything)
assert len(everything) == 17, action_names(everything)

mesh_actions = introspection.actions(axiom.ActionQuery(module="mesh"))
assert action_names(mesh_actions) == ["mesh.inspect", "mesh.rebuild"]

geometry = introspection.actions(axiom.ActionQuery(tags=["geometry"]))
assert action_names(geometry) == ["mesh.inspect", "mesh.rebuild"]
write_only = introspection.actions(axiom.ActionQuery(tags=["geometry", "write"]))
assert action_names(write_only) == ["mesh.rebuild"]
read_only = introspection.actions(axiom.ActionQuery(tags=["geometry", "read"]))
assert action_names(read_only) == ["mesh.inspect"]
assert introspection.actions(axiom.ActionQuery(module="missing")) == []
assert introspection.actions(axiom.ActionQuery(tags=["missing"])) == []
combined = introspection.actions(axiom.ActionQuery(module="mesh", tags=["read"]))
assert action_names(combined) == ["mesh.inspect"]

# Query objects are mutable value objects copied on every call.
query = axiom.ActionQuery()
assert query.module is None
assert query.tags == []
query.module = "mesh"
query.tags = ["write"]
assert action_names(introspection.actions(query)) == ["mesh.rebuild"]

# Returned descriptors are copies; mutations must not affect later queries.
first = introspection.actions(axiom.ActionQuery(module="mesh"))[0]
first.tags.append("polluted")
first.metadata["polluted"] = "yes"
second = introspection.actions(axiom.ActionQuery(module="mesh"))[0]
assert "polluted" not in second.tags
assert "polluted" not in second.metadata

describe = introspection.describe_action(axiom.ActionId("math.add"))
assert describe.description == "Adds two integers"
expect_axiom_error(lambda: introspection.describe_action(axiom.ActionId("missing.action")),
                   axiom.ErrorCode.NotFound)

resources = introspection.resources()
assert [str(resource.id) for resource in resources] == ["geometry:3", "widget:1", "widget:2"]
widgets = introspection.resources(axiom.ResourceQuery(type="widget"))
assert all(resource.type == "widget" for resource in widgets)
assert len(widgets) == 2
assert len(introspection.resources(axiom.ResourceQuery(type="geometry"))) == 1
assert introspection.resources(axiom.ResourceQuery(type="empty")) == []
expect_axiom_error(lambda: introspection.resources(axiom.ResourceQuery(type="Not Canonical!")),
                   axiom.ErrorCode.InvalidArgument)

tasks = introspection.tasks()
names = {task.name for task in tasks}
assert {"pending-blocked", "cancelled-before-run", "completed-value", "completed-void",
        "failed-task", "opaque-done", "removable-value", "running-gate", "cancel-running",
        "opaque-gate", "void-gate"} <= names, names

pending = introspection.tasks(axiom.TaskQuery(state=axiom.TaskState.Pending))
assert {task.name for task in pending} == {"pending-blocked"}
cancelled = introspection.tasks(axiom.TaskQuery(state=axiom.TaskState.Cancelled))
assert {task.name for task in cancelled} == {"cancelled-before-run"}
by_action = introspection.tasks(axiom.TaskQuery(origin_action_id="math.add"))
assert {task.name for task in by_action} == {"completed-value"}
by_request = introspection.tasks(axiom.TaskQuery(origin_request_id="req-gate"))
assert {task.name for task in by_request} == {"running-gate"}
assert introspection.tasks(axiom.TaskQuery(state=axiom.TaskState.Failed,
                                           origin_request_id="req-gate")) == []

query = axiom.TaskQuery()
query.state = axiom.TaskState.Running
running = introspection.tasks(query)
assert {task.name for task in running} == {"running-gate", "cancel-running", "opaque-gate",
                                           "void-gate"}

descriptor = introspection.describe_task(next(t.id for t in tasks if t.name == "completed-value"))
assert descriptor.state == axiom.TaskState.Completed
expect_axiom_error(lambda: introspection.describe_task(axiom.TaskId("task:999999")),
                   axiom.ErrorCode.NotFound)

snapshot = introspection.snapshot()
assert {module.namespace_name for module in snapshot.modules} == {"math", "mesh"}
assert len(snapshot.actions) == len(everything)
assert len(snapshot.resources) == len(resources)
assert len(snapshot.tasks) == len(tasks)

"""Task describe/state/progress/cancel/result behavior across every result shape."""

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


def by_name(name):
    for descriptor in introspection.tasks():
        if descriptor.name == name:
            return descriptor
    raise AssertionError(f"task {name} not found")


def task_for(name):
    return host.task(by_name(name).id)


completed = task_for("completed-value")
assert completed.id == by_name("completed-value").id
assert completed.state() == axiom.TaskState.Completed
descriptor = completed.describe()
assert descriptor.state == axiom.TaskState.Completed
assert descriptor.progress.value == 1.0
assert descriptor.error is None
origin = descriptor.origin
assert origin.request_id == "req-77"
assert origin.trace_id == "trace-77"
assert origin.caller == "python-test"
assert origin.action_id == "math.add"
assert origin.metadata == {"origin": "fixture"}
assert completed.result() == 42

void_task = task_for("completed-void")
assert void_task.state() == axiom.TaskState.Completed
assert void_task.describe().error is None
assert void_task.describe().origin is None
assert void_task.result() is None

failed = task_for("failed-task")
assert failed.state() == axiom.TaskState.Failed
failure = failed.describe().error
assert failure["code"] == axiom.ErrorCode.InvalidArgument
assert failure["message"] == "bad input"
assert failure["path"] == "input.x"
assert failure["details"] == {"reason": "overflow"}
assert failure["has_details"] is True
error = expect_axiom_error(failed.result, axiom.ErrorCode.InvalidArgument)
assert error.path == "input.x"
assert error.details == {"reason": "overflow"}
assert error.has_details is True

opaque = task_for("opaque-done")
assert opaque.state() == axiom.TaskState.Completed
expect_axiom_error(opaque.result, axiom.ErrorCode.TypeMismatch)

cancelled = task_for("cancelled-before-run")
assert cancelled.state() == axiom.TaskState.Cancelled
cancel_error = cancelled.describe().error
assert cancel_error["code"] == axiom.ErrorCode.Cancelled
assert cancel_error["message"] == "Task was cancelled before execution"
assert cancel_error["path"] is None
assert cancel_error["details"] is None
assert cancel_error["has_details"] is False
error = expect_axiom_error(cancelled.result, axiom.ErrorCode.Cancelled)
assert error.has_details is False

pending = task_for("pending-blocked")
assert pending.state() == axiom.TaskState.Pending
assert pending.progress().value == 0.0
assert pending.progress().message == ""
assert pending.result() is None

running = task_for("running-gate")
assert running.state() == axiom.TaskState.Running
assert running.progress().value == 0.5
assert running.progress().message == "half"
assert running.result() is None
running_descriptor = running.describe()
assert running_descriptor.origin.request_id == "req-gate"
assert running_descriptor.origin.action_id == "mesh.rebuild"

removable = by_name("removable-value")
removable_id = removable.id
axiom_python_test_host.remove_named_task("removable-value")
removed_task = host.task(removable_id)
expect_axiom_error(removed_task.describe, axiom.ErrorCode.NotFound)
expect_axiom_error(removed_task.state, axiom.ErrorCode.NotFound)
expect_axiom_error(removed_task.result, axiom.ErrorCode.NotFound)
expect_axiom_error(removed_task.cancel, axiom.ErrorCode.NotFound)

unknown = host.task(axiom.TaskId("task:999999"))
expect_axiom_error(unknown.describe, axiom.ErrorCode.NotFound)
expect_axiom_error(unknown.result, axiom.ErrorCode.NotFound)
expect_axiom_error(unknown.cancel, axiom.ErrorCode.NotFound)

cancel_running = task_for("cancel-running")
assert cancel_running.state() == axiom.TaskState.Running
cancel_running.cancel()
cancel_running.cancel()

axiom_python_test_host.release_task_gate()
axiom_python_test_host.wait_tasks_settled()

assert running.state() == axiom.TaskState.Completed
assert running.result() == 7
assert cancel_running.state() == axiom.TaskState.Cancelled
error = expect_axiom_error(cancel_running.result, axiom.ErrorCode.Cancelled)
assert error.message == "stopped by caller"
assert task_for("opaque-gate").state() == axiom.TaskState.Completed
expect_axiom_error(task_for("opaque-gate").result, axiom.ErrorCode.TypeMismatch)
void_gate = task_for("void-gate")
assert void_gate.state() == axiom.TaskState.Completed
assert void_gate.result() is None
# The blocked executor is never released by the tests, so this task stays queued.
assert task_for("pending-blocked").state() == axiom.TaskState.Pending

first = axiom.TaskId("task:1")
second = axiom.TaskId("task:2")
assert first == axiom.TaskId("task:1")
assert first != second
assert first < second
assert second > first
assert first <= first
assert second >= first
assert sorted([second, first]) == [first, second]
assert hash(first) == hash(axiom.TaskId("task:1"))
assert {first, axiom.TaskId("task:1"), second} == {first, second}
assert (first == axiom.ResourceId("widget:1")) is False
assert (first != axiom.ResourceId("widget:1")) is True
try:
    first < axiom.ResourceId("widget:1")
except TypeError:
    pass
else:
    raise AssertionError("ordering across ID types must be rejected")

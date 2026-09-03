import pytest
from _axiom import AxiomConversionError, AxiomError
from axiom_test_host import host, last_context


def dispatch(method, params=None, context=None):
    if params is None:
        params = {}
    return host.dispatch(method, params, context)


def test_system_snapshot_has_four_collections():
    snapshot = dispatch("system.snapshot")
    assert set(snapshot.keys()) == {"actions", "modules", "resources", "tasks"}
    assert len(snapshot["modules"]) == 1
    assert snapshot["modules"][0]["namespace"] == "embed"
    assert len(snapshot["resources"]) == 1
    assert snapshot["resources"][0]["type"] == "widget"
    assert len(snapshot["tasks"]) == 1
    assert snapshot["tasks"][0]["state"] == "completed"


def test_action_list_and_describe():
    listed = dispatch("action.list")
    ids = {action["id"] for action in listed}
    assert {"embed.ping", "embed.echoi", "embed.probe"} <= ids
    described = dispatch("action.describe", {"action": "embed.ping"})
    assert described["id"] == "embed.ping"
    assert described["return_type"]["kind"] == "integer"
    assert described["metadata"] == {}


def test_action_invoke_and_context_round_trip():
    assert dispatch("action.invoke", {"action": "embed.add", "arguments": {"left": 2, "right": 3}}) == 5
    assert dispatch("action.invoke", {"action": "embed.ping", "arguments": {}}) == 1
    assert dispatch("action.invoke", {"action": "embed.label", "arguments": {}}) == "none"
    assert dispatch("action.invoke", {"action": "embed.label", "arguments": {"text": "héllo🌍"}}) == "héllo🌍"
    probed = dispatch(
        "action.invoke",
        {"action": "embed.probe", "arguments": {}},
        context={"request_id": "req", "trace_id": "tr", "caller": "py", "metadata": {"k": "v"}},
    )
    assert probed == 1
    assert last_context() == {"request_id": "req", "trace_id": "tr", "caller": "py", "metadata": {"k": "v"}}


def test_int64_boundaries_round_trip():
    assert dispatch("action.invoke", {"action": "embed.echoi", "arguments": {"value": 2**63 - 1}}) == 2**63 - 1
    assert dispatch("action.invoke", {"action": "embed.echoi", "arguments": {"value": -(2**63)}}) == -(2**63)


def test_nested_value_round_trip():
    assert dispatch("action.invoke", {"action": "embed.nested", "arguments": {"values": [[1, 2], [3]]}}) == 2
    assert dispatch("action.invoke", {"action": "embed.fields", "arguments": {"fields": {"a": 1, "b": 2}}}) == 2


def test_core_errors_map_to_axiom_error_fields():
    with pytest.raises(AxiomError) as excinfo:
        dispatch("action.invoke", {"action": "embed.fail", "arguments": {}})
    error = excinfo.value
    assert error.code == "invalid_argument"
    assert error.message == "shape is invalid"
    assert error.path == "shape.size"
    assert error.details is None
    with pytest.raises(AxiomError) as excinfo:
        dispatch("action.describe", {"action": "embed.missing"})
    assert excinfo.value.code == "not_found"
    with pytest.raises(AxiomError) as excinfo:
        dispatch("system.snap", {})
    assert excinfo.value.code == "unknown_command"
    with pytest.raises(AxiomError) as excinfo:
        dispatch("action.invoke", {"arguments": {}})
    error = excinfo.value
    assert error.code == "missing_argument"
    assert error.path == "action"
    with pytest.raises(AxiomError) as excinfo:
        dispatch("action.describe", {"action": 5})
    error = excinfo.value
    assert error.code == "type_mismatch"
    assert error.path == "action"
    assert error.details == {"actual": "integer", "expected": "string"}
    with pytest.raises(AxiomError) as excinfo:
        dispatch("action.invoke", {"action": "embed.boom", "arguments": {}})
    assert excinfo.value.code == "invocation_failed"
    with pytest.raises(AxiomError) as excinfo:
        dispatch("action.invoke", {"action": "embed.explode", "arguments": {}})
    assert excinfo.value.code == "internal_error"


def test_resource_commands():
    listed = dispatch("resource.list")
    assert len(listed) == 1
    assert listed[0]["type"] == "widget"
    described = dispatch("resource.describe", {"resource": listed[0]["id"]})
    assert described == listed[0]
    with pytest.raises(AxiomError) as excinfo:
        dispatch("resource.describe", {"resource": "widget:999999"})
    assert excinfo.value.code == "not_found"


def test_task_commands():
    listed = dispatch("task.list")
    assert len(listed) == 1
    assert listed[0]["state"] == "completed"
    described = dispatch("task.describe", {"task": listed[0]["id"]})
    assert described["name"] == "sequence"
    assert dispatch("task.cancel", {"task": listed[0]["id"]}) is None
    with pytest.raises(AxiomError) as excinfo:
        dispatch("task.describe", {"task": "task:999999"})
    assert excinfo.value.code == "not_found"


def test_dispatch_signature_contract():
    with pytest.raises(TypeError):
        dispatch(1)
    with pytest.raises(TypeError):
        host.dispatch("system.snapshot", [])
    with pytest.raises(TypeError):
        host.dispatch("system.snapshot")


def test_conversion_rejects_with_precise_paths():
    with pytest.raises(AxiomConversionError) as excinfo:
        dispatch("action.invoke", {"action": "embed.echoi", "arguments": {"value": 2**63}})
    assert excinfo.value.path == "arguments.value"
    with pytest.raises(AxiomConversionError) as excinfo:
        dispatch("action.invoke", {"action": "embed.add", "arguments": {"left": (1,), "right": 2}})
    assert excinfo.value.path == "arguments.left"
    with pytest.raises(AxiomConversionError) as excinfo:
        dispatch("action.invoke", {"action": "embed.add", "arguments": {"left": b"x", "right": 2}})
    loop = []
    loop.append(loop)
    with pytest.raises(AxiomConversionError):
        dispatch("action.invoke", {"action": "embed.nested", "arguments": {"values": loop}})
    with pytest.raises(AxiomConversionError) as excinfo:
        dispatch("action.invoke", {"action": "embed.ping", "arguments": {1: 2}})
    assert excinfo.value.path == "arguments"


def test_context_validation_rejects_before_core():
    with pytest.raises(AxiomConversionError) as excinfo:
        dispatch("system.snapshot", {}, context={"request_id": 5})
    assert excinfo.value.path == "context.request_id"
    with pytest.raises(AxiomConversionError) as excinfo:
        dispatch("system.snapshot", {}, context={"extra": 1})
    assert excinfo.value.path == "context.extra"
    with pytest.raises(AxiomConversionError) as excinfo:
        dispatch("system.snapshot", {}, context={"metadata": {"k": 5}})
    assert excinfo.value.path == "context.metadata.k"
    with pytest.raises(AxiomConversionError):
        dispatch("system.snapshot", {}, context={"metadata": {1: "v"}})
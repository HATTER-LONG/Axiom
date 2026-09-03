"""Conversion boundary semantics of the _axiom extension.

Covers the Python <-> Value mapping edges: scalar boundaries, container
round-trips, rejection of unsupported types, depth limits, context field
validation, and Core Error field mapping. Dispatch-level command behavior
lives in test_command_dispatch.py.
"""

import decimal

import pytest
from _axiom import AxiomConversionError, AxiomError
from axiom_test_host import host, last_context


def dispatch(method, params=None, context=None):
    if params is None:
        params = {}
    return host.dispatch(method, params, context)


def invoke(action, arguments=None, context=None):
    if arguments is None:
        arguments = {}
    return dispatch("action.invoke", {"action": action, "arguments": arguments}, context)


def test_none_bool_float_and_unicode_round_trip():
    assert invoke("embed.none") is None
    assert invoke("embed.echob", {"value": True}) is True
    assert invoke("embed.echob", {"value": False}) is False
    assert invoke("embed.echof", {"value": 1.5}) == 1.5
    assert invoke("embed.echof", {"value": -0.25}) == -0.25
    assert invoke("embed.echof", {"value": 0.0}) == 0.0
    assert invoke("embed.label", {"text": "héllo🌍"}) == "héllo🌍"


def test_bool_is_not_treated_as_integer():
    with pytest.raises(AxiomError) as excinfo:
        invoke("embed.echoi", {"value": True})
    error = excinfo.value
    assert error.code == "type_mismatch"
    assert error.path == "value"
    assert error.details == {"actual": "boolean", "expected": "integer"}
    with pytest.raises(AxiomError) as excinfo:
        invoke("embed.echob", {"value": 1})
    assert excinfo.value.details == {"actual": "integer", "expected": "boolean"}


def test_int64_boundaries_round_trip_and_overflow():
    assert invoke("embed.echoi", {"value": 2**63 - 1}) == 2**63 - 1
    assert invoke("embed.echoi", {"value": -(2**63)}) == -(2**63)
    assert invoke("embed.echoi", {"value": 0}) == 0
    for overflow in (2**63, -(2**63) - 1):
        with pytest.raises(AxiomConversionError) as excinfo:
            invoke("embed.echoi", {"value": overflow})
        assert excinfo.value.path == "arguments.value"


def test_integer_widens_to_float_but_bool_does_not():
    # Core deliberately widens int64 to double at the Action boundary.
    assert invoke("embed.echof", {"value": 1}) == 1.0
    with pytest.raises(AxiomError) as excinfo:
        invoke("embed.echof", {"value": True})
    error = excinfo.value
    assert error.code == "type_mismatch"
    assert error.path == "value"
    assert error.details == {"actual": "boolean", "expected": "number"}


def test_empty_containers_round_trip():
    assert invoke("embed.echonested", {"values": []}) == []
    assert invoke("embed.echofields", {"fields": {}}) == {}
    assert invoke("embed.echomixed", {"fields": {}}) == {}


def test_nested_containers_round_trip_completely():
    nested = [[1, 2], [], [3]]
    assert invoke("embed.echonested", {"values": nested}) == nested
    deep = [[[1]], [[], [2, 3]]]
    assert invoke("embed.echodeep", {"values": deep}) == deep
    mixed = {"a": [1], "b": [], "c": [2, 3]}
    assert invoke("embed.echomixed", {"fields": mixed}) == mixed


def test_non_string_dict_keys_are_rejected_with_path():
    with pytest.raises(AxiomConversionError) as excinfo:
        invoke("embed.echofields", {"fields": {1: 2}})
    assert excinfo.value.path == "arguments.fields"
    with pytest.raises(AxiomConversionError) as excinfo:
        invoke("embed.echomixed", {"fields": {"a": [1], 2: [3]}})
    assert excinfo.value.path == "arguments.fields"


@pytest.mark.parametrize(
    "action",
    ["embed.echob", "embed.echoi", "embed.echof", "embed.label"],
)
@pytest.mark.parametrize(
    "unsupported",
    [
        (1, 2),
        {1, 2},
        b"x",
        decimal.Decimal("1.5"),
        object(),
    ],
    ids=["tuple", "set", "bytes", "decimal", "custom-object"],
)
def test_unsupported_types_are_rejected(action, unsupported):
    with pytest.raises(AxiomConversionError) as excinfo:
        invoke(action, {"value": unsupported})
    assert excinfo.value.path == "arguments.value"


def test_implicit_int_and_float_protocols_are_rejected():
    class ImplicitInt:
        def __int__(self):
            return 5

    class ImplicitFloat:
        def __float__(self):
            return 1.5

    with pytest.raises(AxiomConversionError):
        invoke("embed.echoi", {"value": ImplicitInt()})
    with pytest.raises(AxiomConversionError):
        invoke("embed.echof", {"value": ImplicitFloat()})


def test_self_referencing_containers_are_rejected():
    loop = [[]]
    loop[0].append(loop)
    with pytest.raises(AxiomConversionError):
        invoke("embed.echonested", {"values": loop})
    cyclic = {}
    cyclic["self"] = cyclic
    with pytest.raises(AxiomConversionError):
        invoke("embed.echomixed", {"fields": {"a": [cyclic]}})


def test_containers_beyond_max_depth_are_rejected():
    deep = []
    current = deep
    for _ in range(150):
        child = []
        current.append(child)
        current = child
    with pytest.raises(AxiomConversionError):
        invoke("embed.echonested", {"values": deep})


def test_context_fields_round_trip_and_validate():
    context = {
        "request_id": "req",
        "trace_id": "trace",
        "caller": "py",
        "metadata": {"k": "v"},
    }
    assert invoke("embed.probe", context=context) == 1
    assert last_context() == context
    with pytest.raises(AxiomConversionError) as excinfo:
        dispatch("system.snapshot", {}, context={"request_id": 5})
    assert excinfo.value.path == "context.request_id"
    with pytest.raises(AxiomConversionError) as excinfo:
        dispatch("system.snapshot", {}, context={"extra": 1})
    assert excinfo.value.path == "context.extra"
    with pytest.raises(AxiomConversionError) as excinfo:
        dispatch("system.snapshot", {}, context={"metadata": {"k": 5}})
    assert excinfo.value.path == "context.metadata.k"
    with pytest.raises(AxiomConversionError) as excinfo:
        dispatch("system.snapshot", {}, context={"metadata": {1: "v"}})
    assert excinfo.value.path == "context.metadata"


def test_core_error_fields_cross_the_boundary():
    with pytest.raises(AxiomError) as excinfo:
        invoke("embed.fail")
    error = excinfo.value
    assert error.code == "invalid_argument"
    assert error.message == "shape is invalid"
    assert error.path == "shape.size"
    assert error.details is None
    with pytest.raises(AxiomError) as excinfo:
        invoke("embed.faildetails")
    error = excinfo.value
    assert error.code == "invalid_argument"
    assert error.message == "shape is invalid"
    assert error.path == "shape.size"
    assert error.details == {"hint": "retry later"}


def test_params_and_context_shapes_are_enforced():
    with pytest.raises(TypeError):
        host.dispatch("system.snapshot", [])
    with pytest.raises(TypeError):
        host.dispatch(b"system.snapshot", {})
    with pytest.raises(TypeError):
        dispatch("system.snapshot", {}, context=[("request_id", "x")])
    assert dispatch("system.snapshot", {}, context=None).keys() >= {"actions", "modules"}


def test_container_and_string_subclasses_are_rejected():
    class StringSubclass(str):
        pass

    class DictSubclass(dict):
        pass

    class ListSubclass(list):
        pass

    with pytest.raises(AxiomConversionError):
        invoke("embed.label", {"text": StringSubclass("value")})
    with pytest.raises(AxiomConversionError):
        invoke("embed.echonested", {"values": ListSubclass([[1]])})
    with pytest.raises(TypeError):
        host.dispatch("system.snapshot", DictSubclass())

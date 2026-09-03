"""Facade behavior over the same embedded fixture as direct dispatch tests."""

import pytest
from axiom_test_host import last_context, raw_handle

from axiom import AxiomConversionError, AxiomError, Host

host = Host._attach(raw_handle())


def test_snapshot_matches_direct_dispatch():
    assert host.snapshot() == host.dispatch("system.snapshot", {})


def test_actions_facade():
    listed = host.actions.list()
    assert {action["id"] for action in listed} >= {"embed.ping", "embed.echoi", "embed.probe"}
    filtered = host.actions.list(module="embed")
    assert filtered == listed
    assert host.actions.list(tags=["read"]) == [host.actions.describe("embed.label")]
    assert host.actions.list(tags=["missing"]) == []
    assert host.actions.list(module="missing") == []
    described = host.actions.describe("embed.ping")
    assert described["id"] == "embed.ping"
    assert described["return_type"]["kind"] == "integer"
    assert host.actions.invoke("embed.add", {"left": 2, "right": 3}) == 5
    assert host.actions.invoke("embed.label") == "none"


def test_resources_facade():
    listed = host.resources.list()
    assert len(listed) == 1
    assert listed[0]["type"] == "widget"
    assert host.resources.describe(listed[0]["id"]) == listed[0]
    assert host.resources.list(type="widget") == listed
    assert host.resources.list(type="geometry") == []


def test_tasks_facade():
    listed = host.tasks.list()
    assert len(listed) == 1
    assert host.tasks.list(state="completed") == listed
    assert host.tasks.list(origin_action="embed.ping") == listed
    assert host.tasks.list(origin_request="req") == listed
    assert host.tasks.describe(listed[0]["id"])["name"] == "sequence"
    assert host.tasks.cancel(listed[0]["id"]) is None


def test_context_reaches_action():
    host.actions.invoke(
        "embed.probe",
        {},
        context={"request_id": "r", "trace_id": "t", "caller": "c", "metadata": {"m": "v"}},
    )
    assert last_context() == {
        "request_id": "r",
        "trace_id": "t",
        "caller": "c",
        "metadata": {"m": "v"},
    }


def test_errors_propagate_unchanged():
    with pytest.raises(AxiomError) as excinfo:
        host.actions.describe("embed.missing")
    error = excinfo.value
    assert error.code == "not_found"
    assert error.path is None
    with pytest.raises(AxiomError) as excinfo:
        host.actions.invoke("embed.fail")
    assert excinfo.value.path == "shape.size"
    with pytest.raises(AxiomError) as excinfo:
        host.actions.invoke("embed.faildetails")
    assert excinfo.value.details == {"hint": "retry later"}
    with pytest.raises(AxiomConversionError) as excinfo:
        host.actions.invoke("embed.echoi", {"value": 2**63})
    assert excinfo.value.path == "arguments.value"


def test_scalar_and_null_mapping():
    assert host.actions.invoke("embed.none") is None
    assert host.actions.invoke("embed.echob", {"value": True}) is True
    assert host.actions.invoke("embed.echof", {"value": -0.25}) == -0.25
    assert host.actions.invoke("embed.echonested", {"values": [[1], []]}) == [[1], []]

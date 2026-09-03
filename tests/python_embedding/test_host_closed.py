import pytest
from _axiom import AxiomHostClosedError
from axiom_test_host import close, host, raw_handle

from axiom import AxiomHostClosedError as FacadeClosed
from axiom import Host

# Attached while the bridge is still open; every later call must keep failing
# deterministically once the host application closes the session.
facade = Host._attach(raw_handle())


def test_close_then_every_direct_call_fails_deterministically():
    close()
    with pytest.raises(AxiomHostClosedError) as excinfo:
        host.dispatch("system.snapshot", {})
    error = excinfo.value
    assert error.code == "host_closed"
    assert error.message == "Host session is closed"
    assert error.path is None
    assert error.details is None
    with pytest.raises(AxiomHostClosedError):
        host.dispatch("action.list", {})
    close()


def test_facade_calls_fail_after_close():
    with pytest.raises(FacadeClosed):
        facade.snapshot()
    with pytest.raises(FacadeClosed):
        facade.dispatch("action.list", {})
    with pytest.raises(FacadeClosed):
        facade.actions.list()


def test_attach_rejects_closed_handle():
    from _axiom import attach
    from axiom_test_host import closed_handle

    with pytest.raises(AxiomHostClosedError) as excinfo:
        attach(closed_handle())
    assert "closed host handle" in str(excinfo.value)
    with pytest.raises(FacadeClosed):
        Host._attach(raw_handle())


def test_attach_rejects_empty_handle():
    from _axiom import attach
    from axiom_test_host import empty_handle

    with pytest.raises(AxiomHostClosedError) as excinfo:
        attach(empty_handle())
    assert "empty host handle" in str(excinfo.value)

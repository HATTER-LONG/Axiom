"""Typed surface of the compiled Axiom extension submodule."""

from collections.abc import Mapping

class _HostHandle:
    """Adapter-owned shared session observation; never constructed from Python."""

class AxiomError(Exception):
    """Structured Core failure with code, message, path, and details."""

    code: str
    message: str
    path: str | None
    details: object

class AxiomHostClosedError(AxiomError):
    """Raised when any API is used after the host session closed."""

class AxiomConversionError(ValueError):
    """Raised when a Python object cannot convert one-to-one to a Value."""

    path: str

class _Host:
    """Python-facing dispatch capability over one attachable handle."""

    def dispatch(
        self,
        method: str,
        params: dict[str, object],
        context: Mapping[str, object] | None = None,
    ) -> object: ...

def attach(handle: _HostHandle) -> _Host: ...

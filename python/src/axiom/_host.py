"""Host facade over the ``_axiom`` dispatch extension.

The facade only assembles method/params/context and forwards them; validation,
descriptor encoding, error semantics, and lifetime logic stay in Core or in the
extension.
"""

from __future__ import annotations

from typing import cast

from . import _axiom
from .actions import Actions
from .resources import Resources
from .tasks import Tasks
from .types import CommandParams, InvocationContext, Snapshot


class Host:
    """Session facade over one host-application bridge handle."""

    def __init__(self, adapter: _axiom._Host) -> None:
        self._adapter = adapter
        self._actions: Actions | None = None
        self._resources: Resources | None = None
        self._tasks: Tasks | None = None

    @classmethod
    def _attach(cls, handle: _axiom._HostHandle) -> Host:
        """Attaches an adapter-owned handle; only the embedding host uses this."""
        return cls(_axiom.attach(handle))

    def dispatch(
        self,
        method: str,
        params: CommandParams,
        context: InvocationContext | None = None,
    ) -> object:
        """Routes one Command method through the shared host session."""
        return self._adapter.dispatch(method, params, context)

    def snapshot(self) -> Snapshot:
        """Collects a sequential, non-atomic snapshot of discoverable objects."""
        return cast("Snapshot", self.dispatch("system.snapshot", {}))

    @property
    def actions(self) -> Actions:
        """Read-only Action facade over this host session."""
        if self._actions is None:
            self._actions = Actions(self)
        return self._actions

    @property
    def resources(self) -> Resources:
        """Read-only Resource facade over this host session."""
        if self._resources is None:
            self._resources = Resources(self)
        return self._resources

    @property
    def tasks(self) -> Tasks:
        """Read-only Task facade over this host session."""
        if self._tasks is None:
            self._tasks = Tasks(self)
        return self._tasks

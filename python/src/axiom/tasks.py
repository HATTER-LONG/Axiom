"""Read-only Task commands over one host session."""

from __future__ import annotations

from typing import TYPE_CHECKING, cast

from .types import TaskDescriptor

if TYPE_CHECKING:
    from ._host import Host


class Tasks:
    """Task list/describe/cancel assembled as Command dispatch calls."""

    def __init__(self, host: Host) -> None:
        self._host = host

    def list(
        self,
        *,
        state: str | None = None,
        origin_action: str | None = None,
        origin_request: str | None = None,
    ) -> list[TaskDescriptor]:
        """Lists Tasks matching optional state and origin filters."""
        params: dict[str, object] = {}
        if state is not None:
            params["state"] = state
        if origin_action is not None:
            params["origin_action"] = origin_action
        if origin_request is not None:
            params["origin_request"] = origin_request
        return cast("list[TaskDescriptor]", self._host.dispatch("task.list", params))

    def describe(self, task: str) -> TaskDescriptor:
        """Returns one Task descriptor by canonical identifier."""
        return cast("TaskDescriptor", self._host.dispatch("task.describe", {"task": task}))

    def cancel(self, task: str) -> None:
        """Requests cancellation for a known Task."""
        self._host.dispatch("task.cancel", {"task": task})

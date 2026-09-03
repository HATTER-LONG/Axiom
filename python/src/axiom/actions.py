"""Read-only Action commands over one host session."""

from __future__ import annotations

from typing import TYPE_CHECKING, cast

from .types import ActionDescriptor, CommandParams, InvocationContext

if TYPE_CHECKING:
    from ._host import Host


class Actions:
    """Action list/describe/invoke assembled as Command dispatch calls."""

    def __init__(self, host: Host) -> None:
        self._host = host

    def list(
        self, *, module: str | None = None, tags: list[str] | None = None
    ) -> list[ActionDescriptor]:
        """Lists Actions matching optional module and tag filters."""
        params: CommandParams = {}
        if module is not None:
            params["module"] = module
        if tags is not None:
            params["tags"] = list(tags)
        return cast("list[ActionDescriptor]", self._host.dispatch("action.list", params))

    def describe(self, action: str) -> ActionDescriptor:
        """Returns one Action descriptor by canonical identifier."""
        return cast("ActionDescriptor", self._host.dispatch("action.describe", {"action": action}))

    def invoke(
        self,
        action: str,
        arguments: dict[str, object] | None = None,
        *,
        context: InvocationContext | None = None,
    ) -> object:
        """Invokes a registered Action with a dynamic argument object."""
        params: CommandParams = {
            "action": action,
            "arguments": arguments if arguments is not None else {},
        }
        return self._host.dispatch("action.invoke", params, context)

"""Read-only Resource commands over one host session."""

from __future__ import annotations

from typing import TYPE_CHECKING, cast

from .types import ResourceDescriptor

if TYPE_CHECKING:
    from ._host import Host


class Resources:
    """Resource list/describe assembled as Command dispatch calls."""

    def __init__(self, host: Host) -> None:
        self._host = host

    def list(self, *, type: str | None = None) -> list[ResourceDescriptor]:
        """Lists Resources matching an optional type filter."""
        params: dict[str, object] = {}
        if type is not None:
            params["type"] = type
        return cast("list[ResourceDescriptor]", self._host.dispatch("resource.list", params))

    def describe(self, resource: str) -> ResourceDescriptor:
        """Returns one Resource descriptor by canonical identifier."""
        return cast(
            "ResourceDescriptor",
            self._host.dispatch("resource.describe", {"resource": resource}),
        )

"""Type information for the Axiom Python binding.

Descriptors are returned as plain dicts; these TypedDict shapes document the
frozen Command protocol without becoming a runtime validation authority.
"""

from __future__ import annotations

from typing import NotRequired, TypedDict

CommandParams = dict[str, object]


class InvocationContext(TypedDict, total=False):
    request_id: str
    trace_id: str
    caller: str
    metadata: dict[str, str]


class TypeDescriptor(TypedDict):
    description: str
    kind: str
    nullable: bool
    element_type: NotRequired[TypeDescriptor]
    fields: NotRequired[dict[str, TypeDescriptor]]
    value_type: NotRequired[TypeDescriptor]


class ParameterDescriptor(TypedDict):
    name: str
    description: str
    required: bool
    type: TypeDescriptor
    default: NotRequired[object]


class ActionDescriptor(TypedDict):
    id: str
    description: str
    parameters: list[ParameterDescriptor]
    return_type: TypeDescriptor
    tags: list[str]
    metadata: dict[str, str]
    version: NotRequired[str]


class ModuleDescriptor(TypedDict):
    namespace: str
    description: str
    tags: list[str]
    metadata: dict[str, str]
    version: NotRequired[str]


class ResourceDescriptor(TypedDict):
    id: str
    type: str


class TaskProgress(TypedDict):
    message: str
    value: float


class TaskOrigin(TypedDict):
    request_id: str
    trace_id: str
    caller: str
    action_id: str
    metadata: dict[str, str]


class ErrorDict(TypedDict):
    code: str
    message: str
    path: NotRequired[str]
    details: NotRequired[object]


class TaskDescriptor(TypedDict):
    id: str
    name: str
    state: str
    progress: TaskProgress
    error: NotRequired[ErrorDict]
    origin: NotRequired[TaskOrigin]


class Snapshot(TypedDict):
    actions: list[ActionDescriptor]
    modules: list[ModuleDescriptor]
    resources: list[ResourceDescriptor]
    tasks: list[TaskDescriptor]

"""Public registry of quality gate and inspection modes."""

from __future__ import annotations

from collections.abc import Callable

from ..model import Gate
from .fast import run as fast
from .full import run as full
from .hardening import run as hardening
from .inspect import INSPECTIONS, PRESETS
from .inspect import run as inspect

GATES: dict[str, Callable[[Gate], None]] = {
    "fast": fast,
    "full": full,
    "hardening": hardening,
}

__all__ = ["GATES", "INSPECTIONS", "PRESETS", "inspect"]

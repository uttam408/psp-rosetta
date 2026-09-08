"""Game-specific front-ends. Each adapter turns a source tree into IR assets."""

from __future__ import annotations

from .base import Adapter
from .flash import FlashAdapter
from .java import JavaAdapter

_REGISTRY: dict[str, type[Adapter]] = {
    FlashAdapter.name: FlashAdapter,
    JavaAdapter.name: JavaAdapter,
}


def get_adapter(name: str) -> Adapter:
    try:
        return _REGISTRY[name]()
    except KeyError:
        raise SystemExit(
            f"unknown adapter {name!r}; known: {', '.join(sorted(_REGISTRY))}"
        )


__all__ = ["Adapter", "FlashAdapter", "JavaAdapter", "get_adapter"]

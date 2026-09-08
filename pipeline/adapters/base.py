from __future__ import annotations

import abc
import fnmatch
from pathlib import Path
from collections.abc import Iterator

from ..ir import Asset, Manifest


class Adapter(abc.ABC):
    """Discovers and describes assets in one game's source. Never encodes anything."""

    name: str = "base"

    @abc.abstractmethod
    def discover(self, source_root: Path) -> Iterator[Asset]:
        """Yield one Asset per logical asset found under ``source_root``."""
        raise NotImplementedError

    def crawl(self, game: str, source_root: Path,
              exclude: list[str] | None = None) -> Manifest:
        source_root = source_root.resolve()
        if not source_root.exists():
            raise SystemExit(f"source not found: {source_root}")
        exclude = exclude or []

        def keep(a: Asset) -> bool:
            hay = (a.id, a.source.name, str(a.source))
            return not any(fnmatch.fnmatch(h, pat) for pat in exclude for h in hay)

        assets = sorted((a for a in self.discover(source_root) if keep(a)),
                        key=lambda a: (a.kind.value, a.id))
        # reject duplicate ids early — they would collide in the pak TOC
        seen: set[str] = set()
        for a in assets:
            if a.id in seen:
                raise SystemExit(f"duplicate asset id from {self.name} adapter: {a.id!r}")
            seen.add(a.id)
        return Manifest(game=game, adapter=self.name, source_root=source_root, assets=assets)


def slug(text: str) -> str:
    """Normalize an arbitrary label into a safe asset-id segment."""
    keep = []
    for ch in text.strip().lower():
        if ch.isalnum():
            keep.append(ch)
        elif ch in "-_/":
            keep.append(ch)
        elif ch in " .":
            keep.append("_")
    s = "".join(keep).strip("_/")
    while "//" in s:
        s = s.replace("//", "/")
    return s or "unnamed"

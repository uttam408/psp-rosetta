"""Intermediate Representation — the contract between game adapters and PSP converters.

An Adapter's only job is to walk a game's source and emit a list of ``Asset`` records.
Nothing in this module knows anything about Flash, Java, or the PSP. Converters consume
``Asset`` records and never look at the original game.
"""

from __future__ import annotations

import dataclasses
import enum
import json
from pathlib import Path
from typing import Any


class Kind(str, enum.Enum):
    IMAGE = "image"      # raster sprite / texture
    AUDIO = "audio"      # sound effect or music clip
    MESH = "mesh"        # 3D model (NFM)
    FONT = "font"        # bitmap font
    DATA = "data"        # opaque or game-specific blob (level defs, car stats, ...)


@dataclasses.dataclass
class Asset:
    """One logical asset discovered in a game's source."""

    id: str                       # stable logical name, e.g. "sprite/player" or "sfx/shoot"
    kind: Kind
    source: Path                  # absolute path to the original file
    meta: dict[str, Any] = dataclasses.field(default_factory=dict)
    tags: list[str] = dataclasses.field(default_factory=list)
    # hint for the converter layer, e.g. {"role": "music"} vs {"role": "sfx"}.
    role: str | None = None

    def to_json(self) -> dict[str, Any]:
        d = dataclasses.asdict(self)
        d["kind"] = self.kind.value
        d["source"] = str(self.source)
        return d

    @classmethod
    def from_json(cls, d: dict[str, Any]) -> "Asset":
        return cls(
            id=d["id"],
            kind=Kind(d["kind"]),
            source=Path(d["source"]),
            meta=d.get("meta", {}),
            tags=d.get("tags", []),
            role=d.get("role"),
        )


@dataclasses.dataclass
class Manifest:
    """A crawl result: everything an adapter found, plus provenance."""

    game: str                     # "luftrauser" | "nfm" | ...
    adapter: str                  # "flash" | "java"
    source_root: Path
    assets: list[Asset] = dataclasses.field(default_factory=list)

    def counts(self) -> dict[str, int]:
        out: dict[str, int] = {}
        for a in self.assets:
            out[a.kind.value] = out.get(a.kind.value, 0) + 1
        return out

    def save(self, path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        payload = {
            "game": self.game,
            "adapter": self.adapter,
            "source_root": str(self.source_root),
            "assets": [a.to_json() for a in self.assets],
        }
        path.write_text(json.dumps(payload, indent=2))

    @classmethod
    def load(cls, path: Path) -> "Manifest":
        d = json.loads(Path(path).read_text())
        return cls(
            game=d["game"],
            adapter=d["adapter"],
            source_root=Path(d["source_root"]),
            assets=[Asset.from_json(x) for x in d["assets"]],
        )

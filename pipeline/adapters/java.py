"""Java adapter — walks a Need for Madness source tree (released / decompiled).

STATUS: partial. Loose media files (textures, sounds) are discovered reliably.
NFM's text model format ("cars/*", "stages/*") and stat tables are classified as
Kind.MESH / Kind.DATA but not yet parsed — see pipeline/convert/models.py.
"""

from __future__ import annotations

from pathlib import Path
from collections.abc import Iterator

from ..ir import Asset, Kind
from .base import Adapter, slug

IMAGE_EXT = {".png", ".gif", ".jpg", ".jpeg", ".bmp"}
AUDIO_EXT = {".wav", ".au", ".mp3", ".ogg", ".aif", ".aiff"}
# NFM keeps model/stage descriptors as extensionless or .txt files in these dirs
MESH_DIRS = {"cars", "models"}
STAGE_DIRS = {"stages", "levels"}
MUSIC_HINTS = ("music", "theme", "menu", "loop", "song")


class JavaAdapter(Adapter):
    name = "java"

    def discover(self, source_root: Path) -> Iterator[Asset]:
        for path in sorted(source_root.rglob("*")):
            if not path.is_file():
                continue
            ext = path.suffix.lower()
            rel = path.relative_to(source_root)
            parts = {p.lower() for p in rel.parts[:-1]}

            if ext in IMAGE_EXT:
                yield Asset(
                    id=f"image/{slug(rel.as_posix().rsplit('.', 1)[0])}",
                    kind=Kind.IMAGE, source=path.resolve(),
                    tags=["nfm"], meta={"orig_name": path.name},
                )
            elif ext in AUDIO_EXT:
                role = "music" if any(h in path.stem.lower() for h in MUSIC_HINTS) else "sfx"
                yield Asset(
                    id=f"audio/{slug(rel.as_posix().rsplit('.', 1)[0])}",
                    kind=Kind.AUDIO, source=path.resolve(), role=role,
                    tags=["nfm"], meta={"orig_name": path.name},
                )
            elif parts & MESH_DIRS and ext in ("", ".txt", ".obj"):
                yield Asset(
                    id=f"mesh/{slug(rel.as_posix())}",
                    kind=Kind.MESH, source=path.resolve(),
                    tags=["nfm", "unparsed"], meta={"orig_name": path.name},
                )
            elif parts & STAGE_DIRS and ext in ("", ".txt"):
                yield Asset(
                    id=f"data/stage/{slug(rel.as_posix())}",
                    kind=Kind.DATA, source=path.resolve(),
                    tags=["nfm", "stage", "unparsed"], meta={"orig_name": path.name},
                )

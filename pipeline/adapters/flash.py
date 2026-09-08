"""Flash adapter — consumes a JPEXS Free Flash Decompiler export directory.

Expected layout (JPEXS "Export all parts"):

    <export>/
      images/     *.png *.jpg *.gif      -> Kind.IMAGE
      sounds/     *.mp3 *.wav *.flac     -> Kind.AUDIO
      fonts/      *.ttf *.png            -> Kind.FONT
      binaryData/ *                      -> Kind.DATA
      scripts/    *.as                   -> ignored (code-port half, not assets)
      symbols.csv | symbols.txt          -> optional id;classname map for nicer ids

Loose image/sound files at the export root are also picked up.
"""

from __future__ import annotations

import csv
from pathlib import Path
from collections.abc import Iterator

from ..ir import Asset, Kind
from .base import Adapter, slug

IMAGE_EXT = {".png", ".jpg", ".jpeg", ".gif", ".bmp"}
AUDIO_EXT = {".mp3", ".wav", ".flac", ".ogg", ".aac"}
FONT_EXT = {".ttf", ".otf"}
MUSIC_HINTS = ("music", "theme", "bgm", "loop", "song", "track", "ambient")


class FlashAdapter(Adapter):
    name = "flash"

    def discover(self, source_root: Path) -> Iterator[Asset]:
        symbols = _load_symbols(source_root)
        seen_ids: dict[str, int] = {}

        def emit(kind: Kind, path: Path, subdir: str) -> Asset:
            base = _pretty_name(path, symbols)
            asset_id = f"{subdir}/{base}"
            # de-dupe by suffixing, since two SWF characters can share a class name
            n = seen_ids.get(asset_id, 0)
            seen_ids[asset_id] = n + 1
            if n:
                asset_id = f"{asset_id}_{n}"
            role = None
            if kind is Kind.AUDIO:
                role = "music" if any(h in path.stem.lower() for h in MUSIC_HINTS) else "sfx"
            return Asset(
                id=asset_id,
                kind=kind,
                source=path.resolve(),
                role=role,
                tags=["flash", subdir],
                meta={"orig_name": path.name},
            )

        buckets = [
            (Kind.IMAGE, IMAGE_EXT, ("images", "sprites", "shapes")),
            (Kind.AUDIO, AUDIO_EXT, ("sounds", "audio")),
            (Kind.FONT, FONT_EXT, ("fonts",)),
        ]
        for kind, exts, dirs in buckets:
            for d in dirs:
                for path in sorted((source_root / d).rglob("*")):
                    if path.is_file() and path.suffix.lower() in exts:
                        yield emit(kind, path, kind.value)
            # loose files at the root
            for path in sorted(source_root.glob("*")):
                if path.is_file() and path.suffix.lower() in exts:
                    yield emit(kind, path, kind.value)

        bin_dir = source_root / "binaryData"
        if bin_dir.is_dir():
            for path in sorted(bin_dir.rglob("*")):
                if path.is_file():
                    yield emit(Kind.DATA, path, "data")


def _load_symbols(root: Path) -> dict[str, str]:
    """Parse an optional JPEXS symbol map: lines of ``id;ClassName`` or ``id,ClassName``."""
    for fname in ("symbols.csv", "symbols.txt"):
        p = root / fname
        if not p.exists():
            continue
        out: dict[str, str] = {}
        text = p.read_text(errors="replace").splitlines()
        delim = ";" if any(";" in ln for ln in text) else ","
        for row in csv.reader(text, delimiter=delim):
            if len(row) >= 2 and row[0].strip().isdigit():
                out[row[0].strip()] = row[1].strip()
        return out
    return {}


def _pretty_name(path: Path, symbols: dict[str, str]) -> str:
    """Prefer a symbol class name; then ffdec's ``<id>_<ClassName>`` convention;
    then a slugged file stem."""
    stem = path.stem
    lead = stem.split("_", 1)[0]
    if lead.isdigit() and lead in symbols:
        return slug(symbols[lead])
    digits = "".join(c for c in stem if c.isdigit())
    if digits and digits in symbols:
        return slug(symbols[digits])

    # ffdec: "<charId>_<Dotted.Class.Name>" and for sounds "<id>_<Class>_<Class>"
    if lead.isdigit():
        rest = stem.split("_", 1)[1] if "_" in stem else stem
        parts = rest.split("_")
        if len(parts) % 2 == 0 and parts[: len(parts) // 2] == parts[len(parts) // 2:]:
            rest = "_".join(parts[: len(parts) // 2])   # collapse doubled sound name
        rest = rest.replace(".", "/").replace("_cl", "/")
        return slug(rest)
    return slug(stem)

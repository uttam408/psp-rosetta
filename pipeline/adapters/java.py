"""Java adapter — walks a Need for Madness source tree (released / decompiled).

Point ``--src`` at the tree root (the directory containing ``OBJ/``) or at ``OBJ/``
itself. NFM keeps its content in zips next to the code:

    OBJ/data/models.zip    84 ``.rad`` text models: 16 cars + 68 track pieces
    OBJ/data/images.zip    GIFs (HUD digits, menus, ...)
    OBJ/data/sounds.zip    WAV sfx
    OBJ/data/*.gif         loose UI images
    OBJ/music/*.zip        one ProTracker ``.mod`` per stage (+ interface, party)
    OBJ/stages/N.txt       the 32 stage descriptions

Zips are unpacked into ``extract_root`` (default ``build/_extract/nfm``) because
``Asset.source`` must be a real path. Loose files of the same kinds are also picked
up, so an already-unzipped tree works. ``OBJ/mycars`` / ``OBJ/mystages`` are the
game's user-content examples and are skipped.

Asset ids::

    mesh/car/<name>  mesh/piece/<name>   (meta: stage_id for pieces)
    data/stage/<n>
    data/music/<name>                    (.mod passthrough; ffmpeg can't decode them)
    audio/sfx/<name>   image/<name>
"""

from __future__ import annotations

import re
import zipfile
from collections.abc import Iterator
from pathlib import Path

from .. import config
from ..ir import Asset, Kind
from ..nfm.tables import CARS, PIECES, PIECE_ID_BASE
from .base import Adapter, slug

IMAGE_EXT = {".png", ".gif", ".jpg", ".jpeg", ".bmp"}
AUDIO_EXT = {".wav", ".au", ".mp3", ".ogg", ".aif", ".aiff"}
MODULE_EXT = {".mod", ".xm", ".s3m", ".it"}
SKIP_DIRS = {"mycars", "mystages", ".git"}
STAGE_RE = re.compile(r"^(\d+)\.txt$")


class JavaAdapter(Adapter):
    name = "java"

    def __init__(self, extract_root: Path | None = None):
        self.extract_root = extract_root or (config.REPO_ROOT / "build" / "_extract" / "nfm")

    # ------------------------------------------------------------------ helpers
    def _unpack(self, zpath: Path, sub: str) -> Iterator[tuple[str, Path]]:
        """Extract ``zpath`` and yield (member name, extracted path). Idempotent."""
        out = self.extract_root / sub
        out.mkdir(parents=True, exist_ok=True)
        root = out.resolve()
        with zipfile.ZipFile(zpath) as z:
            for info in sorted(z.infolist(), key=lambda i: i.filename):
                if info.is_dir():
                    continue
                dst = (out / info.filename).resolve()
                if root not in dst.parents:          # zip-slip guard
                    continue
                if not dst.exists() or dst.stat().st_size != info.file_size:
                    dst.parent.mkdir(parents=True, exist_ok=True)
                    dst.write_bytes(z.read(info))
                yield info.filename, dst

    def _classify(self, name: str, path: Path, origin: str) -> Asset | None:
        """One file -> one Asset, from its name/extension. ``origin`` is the zip
        stem ('models', 'images', 'sounds', 'music-stage3', or '' for loose)."""
        stem, ext = Path(name).stem, Path(name).suffix.lower()
        meta = {"orig_name": Path(name).name, "origin": origin or "loose"}
        if ext == ".rad":
            if stem in CARS:
                return Asset(f"mesh/car/{stem}", Kind.MESH, path, meta | {
                    "nfm_class": "car", "car_index": CARS.index(stem)}, ["nfm", "car"])
            if stem in PIECES:
                return Asset(f"mesh/piece/{stem}", Kind.MESH, path, meta | {
                    "nfm_class": "piece",
                    "stage_id": PIECES.index(stem) + PIECE_ID_BASE}, ["nfm", "piece"])
            return Asset(f"mesh/other/{slug(stem)}", Kind.MESH, path,
                         meta | {"nfm_class": "other"}, ["nfm", "other"])
        if ext in MODULE_EXT:
            return Asset(f"data/music/{slug(stem)}", Kind.DATA, path,
                         meta | {"format": ext[1:]}, ["nfm", "music", "tracker-module"],
                         role="music")
        if ext in IMAGE_EXT:
            return Asset(f"image/{slug(stem)}", Kind.IMAGE, path, meta, ["nfm"])
        if ext in AUDIO_EXT:
            return Asset(f"audio/sfx/{slug(stem)}", Kind.AUDIO, path, meta, ["nfm"],
                         role="sfx")
        return None

    # ---------------------------------------------------------------- discovery
    def discover(self, source_root: Path) -> Iterator[Asset]:
        obj = source_root / "OBJ"
        base = obj if obj.is_dir() else source_root

        def visible(p: Path) -> bool:
            return not (SKIP_DIRS & set(p.relative_to(source_root).parts))

        # 1. zips (models / images / sounds / per-stage music)
        for zpath in sorted(base.rglob("*.zip")):
            if not visible(zpath):
                continue
            origin = zpath.stem if zpath.parent.name == "data" else f"{zpath.parent.name}-{zpath.stem}"
            for member, path in self._unpack(zpath, origin):
                a = self._classify(member, path, origin)
                if a:
                    a.meta["zip"] = zpath.name
                    yield a

        # 2. loose files
        for path in sorted(base.rglob("*")):
            if not path.is_file() or not visible(path) or path.suffix.lower() == ".zip":
                continue
            if path.parent.name == "stages" and (m := STAGE_RE.match(path.name)):
                yield Asset(f"data/stage/{int(m.group(1))}", Kind.DATA, path.resolve(),
                            {"orig_name": path.name, "stage": int(m.group(1))},
                            ["nfm", "stage"])
                continue
            # rebuilds of the code tree carry classes and jars alongside; ignore those
            if path.suffix.lower() in (".java", ".class", ".jar"):
                continue
            a = self._classify(path.name, path.resolve(), "")
            if a:
                yield a

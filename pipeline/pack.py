"""Bundle a converted-assets directory into ``assets.pak`` + a runtime manifest.

assets.pak layout (little-endian)::

    0   char[4]  magic "PAK1"
    4   u32      entry count
    8   repeated entry:
          u16   name length
          u8[]  name (utf-8, '/'-separated logical path)
          u32   offset   (absolute, from start of file)
          u32   size
    ..  blob region (each entry's bytes, 4-byte aligned)
"""

from __future__ import annotations

import json
import struct
from pathlib import Path
from typing import Any


_KIND = {"image": 0, "font": 0, "audio": 1, "data": 2, "mesh": 2}
_PTXFMT = {"rgba8888": 0, "rgba5551": 1, "rgba4444": 2, "idx8": 3}


def _build_index(records: list[dict[str, Any]]) -> bytes:
    """Binary asset table so the runtime needs no JSON parser. See module docstring
    of runtime/src/pak.c for the layout."""
    have = [r for r in records if r.get("file")]
    out = bytearray(b"AIDX")
    out += struct.pack("<I", len(have))
    for r in have:
        idb = r["id"].encode("utf-8")
        kind = _KIND.get(r["kind"], 2)
        fmt = _PTXFMT.get(r.get("format", ""), 0)
        is_img = kind == 0
        sheet = bool(r.get("sheet"))
        w, h = (int(r.get("width", 0)), int(r.get("height", 0))) if is_img else (0, 0)
        if is_img and sheet:
            rects = [tuple(map(int, fr)) for fr in r["frames"]]
        elif is_img:
            rects = [(0, 0, int(r.get("content_w", w)), int(r.get("content_h", h)))]
        else:
            rects = []
        out += struct.pack("<H", len(idb)) + idb
        out += struct.pack("<BBBB", kind, fmt, 1 if sheet else 0, 0)
        out += struct.pack("<HHH", w, h, len(rects))
        for x, y, fw, fh in rects:
            out += struct.pack("<HHHH", x, y, fw, fh)
    return bytes(out)


def build_pak(assets_dir: Path, out_pak: Path, records: list[dict[str, Any]],
              meta: dict[str, Any]) -> dict[str, Any]:
    assets_dir = Path(assets_dir)
    files: list[tuple[str, bytes]] = [("@index", _build_index(records))]
    for rec in records:
        rel = rec.get("file")
        if not rel:
            continue
        files.append((rec["id"], (assets_dir / rel).read_bytes()))

    hdr_size = 8
    for name, _ in files:
        hdr_size += 2 + len(name.encode("utf-8")) + 8

    offsets: list[int] = []
    cursor = hdr_size
    for _, data in files:
        cursor += (-cursor) % 4          # 4-byte align
        offsets.append(cursor)
        cursor += len(data)

    out_pak.parent.mkdir(parents=True, exist_ok=True)
    with out_pak.open("wb") as f:
        f.write(b"PAK1")
        f.write(struct.pack("<I", len(files)))
        for (name, data), off in zip(files, offsets):
            nb = name.encode("utf-8")
            f.write(struct.pack("<H", len(nb)))
            f.write(nb)
            f.write(struct.pack("<II", off, len(data)))
        pos = f.tell()
        for (_, data), off in zip(files, offsets):
            f.write(b"\x00" * (off - pos))
            f.write(data)
            pos = off + len(data)

    manifest = {
        "pak": out_pak.name,
        "game": meta.get("game"),
        "screen": meta.get("screen"),
        "entry_count": len(files),
        "total_bytes": out_pak.stat().st_size,
        "assets": records,
    }
    man_path = out_pak.with_suffix(".manifest.json")
    man_path.write_text(json.dumps(manifest, indent=2))
    return {"pak": str(out_pak), "manifest": str(man_path),
            "entries": len(files), "bytes": out_pak.stat().st_size}

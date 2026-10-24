"""Mesh conversion (NFM ``.rad`` -> ``.pmesh``).

See ``pipeline/nfm/pmesh.py`` for the output layout and ``pipeline/nfm/rad.py`` for
the parser. Meshes that aren't ``.rad`` (none in stock NFM) are passed through and
flagged ``parsed: false`` so the rest of the pipeline keeps working.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any

from ..ir import Asset
from ..nfm.pmesh import pack_pmesh
from ..nfm.rad import parse_rad


def convert_mesh(asset: Asset, out_dir: Path, cfg: dict[str, Any]) -> dict[str, Any]:
    if asset.source.suffix.lower() != ".rad":
        dst = out_dir / (_safe(asset.id) + ".mesh.raw")
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(asset.source.read_bytes())
        return {"id": asset.id, "kind": asset.kind.value,
                "file": str(dst.relative_to(out_dir)), "bytes": dst.stat().st_size,
                "parsed": False, "note": "not a .rad model; passed through"}

    # .rad files are Latin-1-ish text; the original reads them via DataInputStream.readLine
    model = parse_rad(asset.source.read_bytes().decode("latin-1"), asset.source.stem)
    blob = pack_pmesh(model)
    dst = out_dir / (_safe(asset.id) + ".pmesh")
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(blob)
    rec = {
        "id": asset.id, "kind": asset.kind.value,
        "file": str(dst.relative_to(out_dir)), "bytes": len(blob), "parsed": True,
        "polys": len(model.polys), "verts": sum(len(p.verts) for p in model.polys),
        "wheels": len(model.wheels), "tracks": len(model.tracks),
        "max_r": model.max_r,
    }
    if model.unknown:
        # lines the original silently ignores; kept here so a format surprise is visible
        rec["ignored_lines"] = len(model.unknown)
    return rec


def _safe(asset_id: str) -> str:
    return asset_id.replace("..", "_")

"""Mesh conversion (NFM) — STUB.

Target output: a flat little-endian buffer of interleaved vertices
(pos xyz f32, normal xyz f32, uv f32x2, color rgba8) + u16 index list, plus a
small header the runtime hands straight to ``sceGuDrawArray`` / a display list.

Until NFM's text model format is parsed, meshes are passed through unmodified and
flagged ``parsed: false`` so the rest of the pipeline keeps working.
"""

from __future__ import annotations

from pathlib import Path
from typing import Any

from ..ir import Asset


def convert_mesh(asset: Asset, out_dir: Path, cfg: dict[str, Any]) -> dict[str, Any]:
    dst = out_dir / (_safe(asset.id) + ".mesh.raw")
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(asset.source.read_bytes())
    return {
        "id": asset.id, "kind": asset.kind.value,
        "file": str(dst.relative_to(out_dir)),
        "bytes": dst.stat().st_size,
        "parsed": False,
        "note": "NFM model format not yet parsed; see pipeline/convert/models.py",
    }


def _safe(asset_id: str) -> str:
    return asset_id.replace("..", "_")

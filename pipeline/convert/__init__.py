"""Game-agnostic encoders: IR asset -> PSP-native file."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from ..ir import Asset, Kind
from . import audio, models, textures


def convert_asset(asset: Asset, out_dir: Path, cfg: dict[str, Any]) -> dict[str, Any]:
    """Encode one asset. Returns a manifest record (dict) describing the output file."""
    out_dir.mkdir(parents=True, exist_ok=True)
    if asset.kind is Kind.IMAGE or asset.kind is Kind.FONT:
        return textures.convert_image(asset, out_dir, cfg.get("textures", {}))
    if asset.kind is Kind.AUDIO:
        return audio.convert_audio(asset, out_dir, cfg.get("audio", {}))
    if asset.kind is Kind.MESH:
        return models.convert_mesh(asset, out_dir, cfg.get("models", {}))
    # Kind.DATA — pass through untouched
    dst = out_dir / (_safe(asset.id) + ".bin")
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(asset.source.read_bytes())
    return {
        "id": asset.id, "kind": asset.kind.value, "file": str(dst.relative_to(out_dir)),
        "bytes": dst.stat().st_size, "passthrough": True,
    }


def _safe(asset_id: str) -> str:
    return asset_id.replace("..", "_")

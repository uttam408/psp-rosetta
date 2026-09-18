"""Kind.DATA conversion: NFM stages become binary .pstg; everything else is passed through."""

from __future__ import annotations

from pathlib import Path
from typing import Any

from ..ir import Asset
from ..nfm.pstg import pack_pstg
from ..nfm.stage import parse_stage


def convert_data(asset: Asset, out_dir: Path) -> dict[str, Any]:
    safe = asset.id.replace("..", "_")
    if "stage" in asset.tags:
        stage = parse_stage(asset.source.read_bytes().decode("latin-1"),
                            asset.meta.get("stage"))
        dst = out_dir / (safe + ".pstg")
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_bytes(pack_pstg(stage))
        return {"id": asset.id, "kind": asset.kind.value,
                "file": str(dst.relative_to(out_dir)), "bytes": dst.stat().st_size,
                "parsed": True, "objects": len(stage["objects"]),
                "name": stage.get("name")}

    dst = out_dir / (safe + ".bin")
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(asset.source.read_bytes())
    return {"id": asset.id, "kind": asset.kind.value, "file": str(dst.relative_to(out_dir)),
            "bytes": dst.stat().st_size, "passthrough": True}

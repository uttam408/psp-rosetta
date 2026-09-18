"""Kind.DATA conversion: NFM stages become JSON; everything else is passed through."""

from __future__ import annotations

import json
from pathlib import Path
from typing import Any

from ..ir import Asset
from ..nfm.stage import parse_stage


def convert_data(asset: Asset, out_dir: Path) -> dict[str, Any]:
    safe = asset.id.replace("..", "_")
    if "stage" in asset.tags:
        # JSON for now: the runtime format is decided when the runtime loader is
        # written (see docs/nfm-port-plan.md, "Open decisions").
        stage = parse_stage(asset.source.read_bytes().decode("latin-1"),
                            asset.meta.get("stage"))
        dst = out_dir / (safe + ".stage.json")
        dst.parent.mkdir(parents=True, exist_ok=True)
        dst.write_text(json.dumps(stage, separators=(",", ":")))
        return {"id": asset.id, "kind": asset.kind.value,
                "file": str(dst.relative_to(out_dir)), "bytes": dst.stat().st_size,
                "parsed": True, "objects": len(stage["objects"]),
                "name": stage.get("name")}

    dst = out_dir / (safe + ".bin")
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(asset.source.read_bytes())
    return {"id": asset.id, "kind": asset.kind.value, "file": str(dst.relative_to(out_dir)),
            "bytes": dst.stat().st_size, "passthrough": True}

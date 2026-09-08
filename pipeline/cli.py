"""psp-rosetta — asset pipeline CLI.

    psp-rosetta crawl   --game luftrauser --src <jpexs-export-dir>
    psp-rosetta convert --game luftrauser
    psp-rosetta pack    --game luftrauser
    psp-rosetta build   --game luftrauser --src <dir>   # crawl + convert + pack
    psp-rosetta info    --game luftrauser
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from . import config
from .adapters import get_adapter
from .convert import convert_asset
from .ir import Manifest
from .pack import build_pak


def _paths(game: str, args: argparse.Namespace):
    bd = config.build_dir(game)
    return (
        Path(getattr(args, "manifest", None) or bd / "manifest.json"),
        Path(getattr(args, "assets_out", None) or bd / "assets"),
        Path(getattr(args, "pak_out", None) or bd / "assets.pak"),
        bd / "convert.json",
    )


def cmd_crawl(args) -> int:
    gcfg = config.load_game(args.game)
    adapter_name = args.adapter or gcfg["game"].get("adapter")
    if not adapter_name:
        raise SystemExit("no adapter given and none in game.toml")
    adapter = get_adapter(adapter_name)
    man = adapter.crawl(args.game, Path(args.src),
                        exclude=gcfg.get("crawl", {}).get("exclude", []))
    man_path, *_ = _paths(args.game, args)
    man.save(man_path)
    print(f"crawled {len(man.assets)} assets  {dict(sorted(man.counts().items()))}")
    print(f"  -> {man_path}")
    return 0


def cmd_convert(args) -> int:
    gcfg = config.load_game(args.game)
    man_path, assets_out, _, conv_json = _paths(args.game, args)
    man = Manifest.load(man_path)
    conv_cfg = gcfg.get("convert", {})

    records, errors = [], []
    for a in man.assets:
        try:
            records.append(convert_asset(a, assets_out, conv_cfg))
        except SystemExit as e:
            errors.append(f"{a.id}: {e}")
        except Exception as e:  # noqa: BLE001 - keep going, report at end
            errors.append(f"{a.id}: {type(e).__name__}: {e}")

    conv_json.write_text(json.dumps(records, indent=2))
    total = sum(r.get("bytes", 0) for r in records)
    print(f"converted {len(records)}/{len(man.assets)} assets, {total/1024:.1f} KiB")
    print(f"  -> {assets_out}")
    for e in errors:
        print(f"  ! {e}", file=sys.stderr)
    return 1 if errors and not args.keep_going else 0


def cmd_pack(args) -> int:
    gcfg = config.load_game(args.game)
    man_path, assets_out, pak_out, conv_json = _paths(args.game, args)
    if not conv_json.exists():
        raise SystemExit("run `convert` first")
    records = json.loads(conv_json.read_text())
    meta = {"game": args.game, "screen": gcfg.get("screen", {})}
    res = build_pak(assets_out, pak_out, records, meta)
    print(f"packed {res['entries']} entries -> {res['pak']}  ({res['bytes']/1024:.1f} KiB)")
    print(f"  manifest -> {res['manifest']}")
    return 0


def cmd_build(args) -> int:
    return cmd_crawl(args) or cmd_convert(args) or cmd_pack(args)


def cmd_info(args) -> int:
    man_path, _, _, conv_json = _paths(args.game, args)
    man = Manifest.load(man_path)
    print(f"game={man.game} adapter={man.adapter} root={man.source_root}")
    print(f"assets: {dict(sorted(man.counts().items()))}  total={len(man.assets)}")
    by_kind: dict[str, list[str]] = {}
    for a in man.assets:
        by_kind.setdefault(a.kind.value, []).append(a.id)
    for kind, ids in sorted(by_kind.items()):
        print(f"\n[{kind}] {len(ids)}")
        for i in ids[:12]:
            print(f"  {i}")
        if len(ids) > 12:
            print(f"  ... +{len(ids) - 12} more")
    return 0


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(prog="psp-rosetta")
    sub = p.add_subparsers(dest="cmd", required=True)

    def add_common(sp):
        sp.add_argument("--game", required=True)
        sp.add_argument("--manifest")
        sp.add_argument("--assets-out")
        sp.add_argument("--pak-out")

    c = sub.add_parser("crawl"); add_common(c)
    c.add_argument("--src", required=True); c.add_argument("--adapter")
    c.set_defaults(fn=cmd_crawl)

    c = sub.add_parser("convert"); add_common(c)
    c.add_argument("--keep-going", action="store_true")
    c.set_defaults(fn=cmd_convert)

    c = sub.add_parser("pack"); add_common(c)
    c.set_defaults(fn=cmd_pack)

    c = sub.add_parser("build"); add_common(c)
    c.add_argument("--src", required=True); c.add_argument("--adapter")
    c.add_argument("--keep-going", action="store_true")
    c.set_defaults(fn=cmd_build)

    c = sub.add_parser("info"); add_common(c)
    c.set_defaults(fn=cmd_info)

    args = p.parse_args(argv)
    return args.fn(args)


if __name__ == "__main__":
    raise SystemExit(main())

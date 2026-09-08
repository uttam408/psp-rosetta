"""Render preview figures of the extracted Luftrauser sprites for docs/assets_readme.md.

    .venv/bin/python tools/render_asset_preview.py

Crawls the Luftrauser assets through the pipeline adapter (so ids + the
[convert.textures.sheets] frame table line up exactly), then writes
docs/assets/sprites-overview.png (single-frame sprites) and
docs/assets/anim-strips.png (animation strips with frame gridlines).
Sprites are near-monochrome on transparent -> composited on the game's navy,
scaled nearest-neighbour.
"""

from __future__ import annotations

import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO))
from pipeline import config                       # noqa: E402
from pipeline.adapters import get_adapter          # noqa: E402
from pipeline.ir import Kind                       # noqa: E402

SRC = Path("/Users/uttam/psp-rosetta/sources/luftrauser/assets")
OUT = REPO / "docs" / "assets"
BG = (28, 30, 46)
GRID = (70, 76, 110)
INK = (150, 156, 180)
HI = (232, 234, 245)
FONT = ImageFont.load_default()


def short(asset_id: str) -> str:
    return asset_id.removeprefix("image/").removeprefix("interaction/")


def composite(path: Path) -> Image.Image:
    im = Image.open(path).convert("RGBA")
    bg = Image.new("RGBA", im.size, (*BG, 255))
    return Image.alpha_composite(bg, im).convert("RGB")


def text_w(d, s):
    return d.textlength(s, font=FONT)


def overview(items, out: Path) -> None:
    # per-sprite adaptive zoom so a 16px body and the 468px logo end up similar
    # on-screen sizes, packed into a fixed grid.
    cell, cols, cap = 132, 6, 20
    sprites = [(short(i), composite(p)) for i, p in items]
    rows = (len(sprites) + cols - 1) // cols
    canvas = Image.new("RGB", (cell * cols, (cell + cap) * rows), BG)
    d = ImageDraw.Draw(canvas)
    for idx, (name, spr) in enumerate(sprites):
        cx, cy = (idx % cols) * cell, (idx // cols) * (cell + cap)
        avail = cell - 18
        ratio = avail / max(spr.width, spr.height)
        if ratio >= 1:
            z = min(8, int(ratio))
            big = spr.resize((spr.width * z, spr.height * z), Image.NEAREST)
            zlbl = f"{z}x"
        else:
            big = spr.resize((max(1, round(spr.width * ratio)),
                              max(1, round(spr.height * ratio))), Image.BOX)
            zlbl = f"{ratio:.2f}x"
        ox, oy = cx + (cell - big.width) // 2, cy + cap + (cell - cap - big.height) // 2
        canvas.paste(big, (ox, oy))
        d.rectangle([ox - 1, oy - 1, ox + big.width, oy + big.height], outline=GRID)
        d.text((cx + (cell - text_w(d, f"{spr.width}x{spr.height}  {zlbl}")) / 2, cy + 4),
               f"{spr.width}x{spr.height}  {zlbl}", font=FONT, fill=HI)
        d.text((cx + (cell - text_w(d, name)) / 2, cy + (cell + cap) - 13),
               name, font=FONT, fill=INK)
    canvas.save(out)
    print("wrote", out, canvas.size)


def strips(items, out: Path) -> None:
    labelw, target, rowpad = 210, 1180, 22
    rows = []
    for asset_id, path, (fw, fh) in items:
        spr = composite(path)
        n = max(1, spr.width // fw)
        z = max(1, min(6, round(target / spr.width)))
        rows.append((short(asset_id), spr, fw, fh, n, z))
    body_w = max(s.width * z for _, s, _, _, _, z in rows)
    heights = [s.height * z + rowpad for _, s, _, _, _, z in rows]
    canvas = Image.new("RGB", (labelw + body_w + 16, sum(heights) + 14), BG)
    d = ImageDraw.Draw(canvas)
    y = 8
    for (name, spr, fw, fh, n, z), h in zip(rows, heights):
        big = spr.resize((spr.width * z, spr.height * z), Image.NEAREST)
        canvas.paste(big, (labelw, y))
        for f in range(n + 1):
            gx = labelw + f * fw * z
            d.line([gx, y, gx, y + big.height], fill=GRID)
        d.text((10, y + 2), name, font=FONT, fill=HI)
        d.text((10, y + 14), f"{fw}x{fh} x{n}  ({z}x)", font=FONT, fill=INK)
        y += h
    canvas.save(out)
    print("wrote", out, canvas.size)


def main() -> int:
    gcfg = config.load_game("luftrauser")
    sheets = gcfg.get("convert", {}).get("textures", {}).get("sheets", {})
    bands = {"image/interaction/water/water", "image/interaction/space/space"}
    man = get_adapter("flash").crawl("luftrauser", SRC,
                                     exclude=gcfg.get("crawl", {}).get("exclude", []))
    images = [a for a in man.assets if a.kind is Kind.IMAGE]

    singles, strip_items = [], []
    for a in images:
        if a.id in sheets and a.id not in bands:
            fw, fh = sheets[a.id]
            strip_items.append((a.id, a.source, (fw, fh)))
        elif a.id not in bands:
            singles.append((a.id, a.source))

    OUT.mkdir(parents=True, exist_ok=True)
    overview(sorted(singles), OUT / "sprites-overview.png")
    strips(sorted(strip_items), OUT / "anim-strips.png")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

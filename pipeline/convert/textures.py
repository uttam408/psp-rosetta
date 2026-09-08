"""Image -> ``.ptx``: a minimal PSP-friendly texture container.

.ptx layout (little-endian)::

    0   char[4]  magic "PTX1"
    4   u16      width           (power of two)
    6   u16      height          (power of two)
    8   u8       format          0=RGBA8888 1=RGBA5551 2=RGBA4444 3=IDX8
    9   u8       flags           bit0 = swizzled (PSP 16x8-byte blocks)
    10  u16      pal_count       0 unless IDX8
    12  u8[pal_count*4]          palette, RGBA8888
    ..  pixel data

Sprites are top-left anchored and transparent-padded up to the next power of two;
the un-padded content size travels in the pack manifest as ``content_w/h`` so the
runtime can set correct UVs.
"""

from __future__ import annotations

import struct
from pathlib import Path
from typing import Any

from PIL import Image

from ..ir import Asset

_FMT = {"rgba8888": 0, "rgba5551": 1, "rgba4444": 2, "idx8": 3}


def convert_image(asset: Asset, out_dir: Path, cfg: dict[str, Any]) -> dict[str, Any]:
    max_size = int(cfg.get("max_size", 256))
    want_fmt = str(cfg.get("format", "rgba8888")).lower()
    do_swizzle = bool(cfg.get("swizzle", False))
    do_pot = bool(cfg.get("pot", True))

    img = Image.open(asset.source).convert("RGBA")
    src_w, src_h = img.size

    if max(img.size) > max_size:
        img.thumbnail((max_size, max_size), Image.LANCZOS)
    content_w, content_h = img.size

    if do_pot:
        pw, ph = _pot(content_w), _pot(content_h)
        if (pw, ph) != (content_w, content_h):
            canvas = Image.new("RGBA", (pw, ph), (0, 0, 0, 0))
            canvas.paste(img, (0, 0))
            img = canvas
    w, h = img.size

    if want_fmt == "auto":
        want_fmt = "rgba8888" if _has_soft_alpha(img) else "rgba5551"
    if want_fmt not in _FMT:
        raise SystemExit(f"{asset.id}: unknown texture format {want_fmt!r}")

    palette: list[int] = []
    if want_fmt == "idx8":
        pixels, palette = _encode_idx8(img)
        bpp = 1
    elif want_fmt == "rgba8888":
        pixels = img.tobytes()
        bpp = 4
    else:
        pixels = _encode_16(img, want_fmt)
        bpp = 2

    flags = 0
    bytewidth = w * bpp
    if do_swizzle and bytewidth % 16 == 0 and h % 8 == 0:
        pixels = _swizzle(pixels, bytewidth, h)
        flags |= 1
    elif do_swizzle:
        # too small to swizzle cleanly; leave linear
        pass

    dst = out_dir / (_safe(asset.id) + ".ptx")
    dst.parent.mkdir(parents=True, exist_ok=True)
    with dst.open("wb") as f:
        f.write(b"PTX1")
        f.write(struct.pack("<HHBBH", w, h, _FMT[want_fmt], flags, len(palette) // 4))
        if palette:
            f.write(bytes(palette))
        f.write(pixels)

    return {
        "id": asset.id, "kind": asset.kind.value, "file": str(dst.relative_to(out_dir)),
        "format": want_fmt, "width": w, "height": h,
        "content_w": content_w, "content_h": content_h,
        "src_w": src_w, "src_h": src_h,
        "swizzled": bool(flags & 1), "bytes": dst.stat().st_size,
    }


def _pot(n: int) -> int:
    p = 1
    while p < n:
        p <<= 1
    return max(p, 1)


def _has_soft_alpha(img: Image.Image) -> bool:
    a = img.getchannel("A")
    lo, hi = a.getextrema()
    return not (lo in (0, 255) and hi in (0, 255))


def _encode_16(img: Image.Image, fmt: str) -> bytes:
    raw = img.tobytes()  # RGBA, 4 bytes/pixel
    out = bytearray()
    for i in range(0, len(raw), 4):
        r, g, b, a = raw[i], raw[i + 1], raw[i + 2], raw[i + 3]
        if fmt == "rgba5551":
            v = ((a >= 128) << 15) | ((b >> 3) << 10) | ((g >> 3) << 5) | (r >> 3)
        else:  # rgba4444
            v = ((a >> 4) << 12) | ((b >> 4) << 8) | ((g >> 4) << 4) | (r >> 4)
        out += struct.pack("<H", v)
    return bytes(out)


def _encode_idx8(img: Image.Image) -> tuple[bytes, list[int]]:
    # Fast Octree is the only Pillow method that quantizes RGBA (keeps alpha).
    q = img.convert("RGBA").quantize(colors=256, method=Image.Quantize.FASTOCTREE)
    pal = q.getpalette(rawmode="RGBA") or []
    used = [idx for _, idx in (q.getcolors(maxcolors=256) or [])]
    n = (max(used) + 1) if used else 1
    rgba: list[int] = []
    for i in range(n):
        chunk = pal[i * 4: i * 4 + 4]
        rgba += chunk if len(chunk) == 4 else [0, 0, 0, 0]
    return q.tobytes(), rgba


def _swizzle(data: bytes, bytewidth: int, height: int) -> bytes:
    out = bytearray(len(data))
    rowblocks = bytewidth // 16
    for y in range(height):
        base_y = (y >> 3) * rowblocks
        row = y * bytewidth
        iny = (y & 7) << 4
        for x in range(bytewidth):
            block = ((x >> 4) + base_y) << 7        # * 128 bytes per 16x8 block
            out[block + (x & 15) + iny] = data[row + x]
    return bytes(out)


def _safe(asset_id: str) -> str:
    return asset_id.replace("..", "_")

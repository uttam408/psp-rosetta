#!/usr/bin/env python3
"""Pixel-diff two renders (Java oracle vs C runtime): nfm_diff.py java.png c.bmp [diff.png]

Prints the fraction of differing pixels, the mean channel error, and a per-band breakdown
(top/middle/bottom thirds) so it is obvious *where* the renderers disagree. Optionally
writes a diff image (black = identical, white = channel error >= 32)."""
import sys

from PIL import Image, ImageChops


def main() -> int:
    a = Image.open(sys.argv[1]).convert("RGB")
    b = Image.open(sys.argv[2]).convert("RGB")
    if a.size != b.size:
        print(f"size mismatch {a.size} vs {b.size}")
        return 2
    d = ImageChops.difference(a, b)
    w, h = a.size
    px = d.load()
    bad = 0
    tot = 0
    bands = [0, 0, 0]
    for y in range(h):
        for x in range(w):
            r, g, bl = px[x, y]
            e = max(r, g, bl)
            tot += r + g + bl
            if e:
                bad += 1
                bands[min(2, y * 3 // h)] += 1
    n = w * h
    print(f"{w}x{h}: {bad} px differ ({100.0 * bad / n:.2f}%), mean channel error {tot / (3 * n):.2f}")
    print("differing px by third (top/mid/bottom):", bands)
    if len(sys.argv) > 3:
        d.point(lambda v: min(255, v * 8)).save(sys.argv[3])
    return 0


if __name__ == "__main__":
    sys.exit(main())

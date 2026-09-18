"""Parser for NFM ``.rad`` model files (cars and track pieces).

Mirrors the ``ContO(byte[], Medium, Trackers)`` constructor in ContO.java, which is
a line-oriented reader: each line is trimmed and dispatched on ``startsWith``.

Coordinate math is reproduced exactly: every ``p(x,y,z)`` component goes through
``(int)(int(v) * div * iwid * scale)`` with **float32** intermediates, so vertex
positions match the Java integers bit for bit (``iwid`` applies to x only).

Not generated here (the runtime builds them from the parameters we record):
  * wheel geometry — ``w(...)`` makes 19 procedural planes in ``Wheels.make``
  * pile / mountain objects — stage-side, see stage.py
"""

from __future__ import annotations

import re
import struct
from dataclasses import dataclass, field
from typing import Any


def f32(x: float) -> float:
    """Round to Java float32."""
    return struct.unpack("<f", struct.pack("<f", x))[0]


def args(line: str) -> list[int]:
    """ContO.getvalue: comma-separated numbers, each truncated to int (Java
    ``(int) Float.valueOf``). Tolerates a missing close paren, like the original."""
    m = re.match(r"[^(]*\(([^)]*)", line)
    if not m:
        return []
    return [int(float(t)) if t.strip() else 0 for t in m.group(1).split(",")]


# poly material kinds (Plane ctor arg)
MAT_NORMAL, MAT_GLASS, MAT_GSHADOW = 0, 1, 2
LIGHT_NONE, LIGHT_FRONT, LIGHT_BACK = 0, 1, 2
# a normal-material poly whose colour equals 1stColor/2ndColor is repainted per car
PAINT_NONE, PAINT_FIRST, PAINT_SECOND = 0, 1, 2

_SIMPLE_FLAGS = ("shadow", "stonecold", "decorative")


@dataclass
class Poly:
    color: tuple[int, int, int]
    verts: list[tuple[int, int, int]]
    material: int = MAT_NORMAL
    light: int = LIGHT_NONE
    gr: int = 0              # Plane `gr(n)`
    fs: int = 0              # Plane `fs(n)`
    no_outline: bool = False
    road: bool = False       # `road` directive was active when the poly closed
    paint: int = PAINT_NONE


@dataclass
class Wheel:
    x: int
    y: int
    z: int
    steer: int               # w() arg 3, kept raw
    width: int
    height: int
    gwgr: int


@dataclass
class Track:
    """Collision box (`<track>` block). Sizes/positions are post-`div`."""
    color: tuple[int, int, int] = (0, 0, 0)
    xy: int = 0
    zy: int = 0
    radx: int = 0
    rady: int = 0
    radz: int = 0
    x: int = 0
    y: int = 0
    z: int = 0
    skid: int = 0
    dam: int = 1             # default 1; a bare `dam` line sets 3 (ContO.java:376)
    notwall: bool = False


@dataclass
class Model:
    name: str = ""
    polys: list[Poly] = field(default_factory=list)
    wheels: list[Wheel] = field(default_factory=list)
    rims: tuple[int, ...] | None = None
    tracks: list[Track] = field(default_factory=list)
    first_color: tuple[int, int, int] | None = None
    second_color: tuple[int, int, int] | None = None
    props: dict[str, Any] = field(default_factory=dict)   # numeric header directives
    flags: list[str] = field(default_factory=list)        # bare-word header flags
    unknown: list[str] = field(default_factory=list)      # lines we didn't recognise
    max_r: int = 0


def parse_rad(text: str, name: str = "") -> Model:
    mdl = Model(name=name)
    props = mdl.props
    div = iwid = f32(1.0)
    scale = [f32(1.0)] * 3
    road = False
    keep_outline = False     # `newstone` pins the no-outline flag across polys
    no_outline = False
    gwgr = 0
    seen_disp = False        # <track> blocks are only read after `disp(` (ContO.java:329)

    in_poly = in_track = False
    color = (0, 0, 0)
    material, light, gr, fs = MAT_NORMAL, LIGHT_NONE, 0, 0
    verts: list[tuple[int, int, int]] = []
    track = Track()

    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("//"):
            continue

        # ---- <p> polygon block
        if line.startswith("<p>"):
            in_poly, verts = True, []
            gr = fs = 0
            light = LIGHT_NONE
            if not keep_outline:
                no_outline = False
            continue
        if in_poly:
            if line.startswith("</p>"):
                paint = PAINT_NONE
                if material == MAT_NORMAL:
                    if color == mdl.first_color:
                        paint = PAINT_FIRST
                    if color == mdl.second_color:
                        paint = PAINT_SECOND
                mdl.polys.append(Poly(color, verts, material, light, gr, fs,
                                      no_outline, road, paint))
                in_poly = False
            elif line.startswith("gr("):
                gr = args(line)[0]
            elif line.startswith("fs("):
                fs = args(line)[0]
            elif line.startswith("c("):
                a = args(line)
                color, material = (a[0], a[1], a[2]), MAT_NORMAL
            elif line.startswith("glass"):
                material = MAT_GLASS
            elif line.startswith("gshadow"):
                material = MAT_GSHADOW
            elif line.startswith("lightB"):
                light = LIGHT_BACK
            elif line.startswith("light"):
                light = LIGHT_FRONT
            elif line.startswith("noOutline"):
                no_outline = True
            elif line.startswith("p("):
                a = args(line)
                x = int(f32(f32(f32(a[0] * div) * iwid) * scale[0]))
                y = int(f32(f32(a[1] * div) * scale[1]))
                z = int(f32(f32(a[2] * div) * scale[2]))
                verts.append((x, y, z))
                mdl.max_r = max(mdl.max_r, int((x * x + y * y + z * z) ** 0.5))
            continue

        # ---- <track> collision boxes
        if seen_disp:
            if line.startswith("<track>"):
                in_track, track = True, Track()
                continue
            if in_track:
                if line.startswith("</track>"):
                    mdl.tracks.append(track)
                    in_track = False
                    continue
                a = args(line)
                if line.startswith("c"):
                    track.color = (a[0], a[1], a[2])
                elif line.startswith("xy"):
                    track.xy = a[0]
                elif line.startswith("zy"):
                    track.zy = a[0]
                elif line.startswith("radx"):
                    track.radx = int(f32(a[0] * div))
                elif line.startswith("rady"):
                    track.rady = int(f32(a[0] * div))
                elif line.startswith("radz"):
                    track.radz = int(f32(a[0] * div))
                elif line.startswith("tx"):
                    track.x = int(f32(a[0] * div))
                elif line.startswith("ty"):
                    track.y = int(f32(a[0] * div))
                elif line.startswith("tz"):
                    track.z = int(f32(a[0] * div))
                elif line.startswith("skid"):
                    track.skid = a[0]
                elif line.startswith("dam"):
                    track.dam = 3
                elif line.startswith("notwall"):
                    track.notwall = True
                continue

        # ---- header directives
        if line.startswith("rims("):
            mdl.rims = tuple(args(line))
        elif line.startswith("w("):
            if len(mdl.wheels) >= 4:
                continue         # ContO ignores wheels past the fourth (n6 < 4)
            a = args(line)
            mdl.wheels.append(Wheel(
                x=int(f32(f32(f32(a[0] * div) * iwid) * scale[0])),
                y=int(f32(f32(a[1] * div) * scale[1])),
                z=int(f32(f32(a[2] * div) * scale[2])),
                steer=a[3],
                width=int(f32(f32(a[4] * div) * iwid)),
                height=int(f32(a[5] * div)),
                gwgr=gwgr))
        elif line.startswith("disp("):
            seen_disp = True
            props["disp"] = args(line)[0]
        elif line.startswith("disline("):
            props["disline"] = args(line)[0] * 2
        elif line.startswith("grounded("):
            props["grounded"] = args(line)[0] / 100.0
        elif line.startswith("div("):
            div = f32(args(line)[0] / f32(10.0))
        elif line.startswith("idiv("):
            div = f32(args(line)[0] / f32(100.0))
        elif line.startswith("iwid("):
            iwid = f32(args(line)[0] / f32(100.0))
        elif line.startswith("ScaleX("):
            scale[0] = f32(args(line)[0] / f32(100.0))
        elif line.startswith("ScaleY("):
            scale[1] = f32(args(line)[0] / f32(100.0))
        elif line.startswith("ScaleZ("):
            scale[2] = f32(args(line)[0] / f32(100.0))
        elif line.startswith("gwgr("):
            gwgr = args(line)[0]
        elif line.startswith("1stColor("):
            a = args(line)
            mdl.first_color = (a[0], a[1], a[2])
        elif line.startswith("2ndColor("):
            a = args(line)
            mdl.second_color = (a[0], a[1], a[2])
        elif line.startswith("newstone"):
            mdl.flags += ["stonecold", "newstone"]
            keep_outline = no_outline = True
        elif line.startswith("notroad"):
            road = False
        elif line.startswith("road"):
            road = True
            mdl.flags.append("road")
        elif any(line.startswith(f) for f in _SIMPLE_FLAGS):
            mdl.flags.append(next(f for f in _SIMPLE_FLAGS if line.startswith(f)))
        elif line.startswith("tracks"):
            pass                 # allocation count only; blocks are counted as parsed
        else:
            mdl.unknown.append(line)

    props.update(div=div, iwid=iwid, scale=list(scale))
    return mdl



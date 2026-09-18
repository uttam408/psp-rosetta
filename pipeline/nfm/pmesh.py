"""``.pmesh`` — the runtime model format for NFM cars and track pieces.

Little-endian.  Layout::

    HEADER (48 bytes, HDR_FMT)
      char[4] "PMSH"  u16 version(=1)  u16 flags
      u32 nverts  u32 npolys  u32 nindices  u32 max_r
      u8 nwheels  u8 ntracks  u16 pad
      u8[3] first_color  pad   u8[3] second_color  pad   i16[5] rims   pad[2]
    flags: bit0 decorative  bit1 shadow  bit2 road  bit3 has 1stColor
           bit4 has 2ndColor  bit5 has rims
    vertices  nverts  * { f32 x, y, z }      model units, Y-down as authored
    polys     npolys  * 18 bytes (POLY_FMT): u32 first_index, u16 nverts,
                        u8 material, u8 light, u8 r,g,b, u8 paint,
                        s16 gr, s16 fs, u8 no_outline, pad
    indices   nindices * u16                  one triangle fan per poly
    (pad to 4)
    wheels    nwheels * 28 bytes: i32 x,y,z,steer,width,height,gwgr
    tracks    ntracks * 44 bytes: u8 r,g,b,pad, i32 xy,zy,radx,rady,radz,x,y,z,skid,
                        u8 dam, u8 notwall, u16 pad

Polys stay n-gons with flat colour: NFM shades per polygon at draw time, so the
runtime wants poly granularity, not a merged vertex-colour mesh.  Vertices are
per-poly (not shared) because lighting and paint substitution are per poly.
Wheel geometry is *not* stored — the runtime builds it from the ``wheels`` table
(the original does the same in ``Wheels.make``).
"""

from __future__ import annotations

import struct
from typing import Any

from .rad import Model

HDR_FMT = "<4sHHIIIIBBH3sx3sx5hxx"      # 48 bytes
POLY_FMT = "<IHBBBBBBhhBx"              # 18 bytes
WHEEL_FMT = "<7i"                       # 28 bytes
TRACK_FMT = "<3BxiiiiiiiiiBBxx"         # 44 bytes


def pack_pmesh(m: Model) -> bytes:
    verts: list[tuple[int, int, int]] = []
    polys = bytearray()
    idx: list[int] = []
    for p in m.polys:
        base = len(verts)
        polys += struct.pack(POLY_FMT, len(idx), len(p.verts), p.material, p.light,
                             *p.color, p.paint, p.gr, p.fs, int(p.no_outline))
        verts.extend(p.verts)
        idx.extend(range(base, base + len(p.verts)))
    flags = (("decorative" in m.flags) | (("shadow" in m.flags) << 1)
             | (("road" in m.flags) << 2) | ((m.first_color is not None) << 3)
             | ((m.second_color is not None) << 4) | ((m.rims is not None) << 5))
    rims = (tuple(m.rims) + (0,) * 5)[:5] if m.rims else (0,) * 5
    out = bytearray(struct.pack(
        HDR_FMT, b"PMSH", 1, flags, len(verts), len(m.polys), len(idx), m.max_r,
        len(m.wheels), len(m.tracks), 0,
        bytes(m.first_color or (0, 0, 0)), bytes(m.second_color or (0, 0, 0)), *rims))
    for v in verts:
        out += struct.pack("<3f", *v)
    out += polys
    out += struct.pack(f"<{len(idx)}H", *idx)
    out += b"\0" * (-len(out) % 4)
    for w in m.wheels:
        out += struct.pack(WHEEL_FMT, w.x, w.y, w.z, w.steer, w.width, w.height, w.gwgr)
    for t in m.tracks:
        out += struct.pack(TRACK_FMT, *t.color, t.xy, t.zy, t.radx, t.rady, t.radz,
                           t.x, t.y, t.z, t.skid, t.dam, int(t.notwall))
    return bytes(out)


def unpack_pmesh(data: bytes) -> dict[str, Any]:
    """Inverse of pack_pmesh, for tests and tooling."""
    (magic, ver, flags, nv, npoly, ni, max_r, nw, nt, _pad,
     c1, c2, *rims) = struct.unpack_from(HDR_FMT, data, 0)
    assert magic == b"PMSH" and ver == 1, (magic, ver)
    off = struct.calcsize(HDR_FMT)
    verts = [struct.unpack_from("<3f", data, off + 12 * i) for i in range(nv)]
    off += 12 * nv
    polys = [struct.unpack_from(POLY_FMT, data, off + 18 * i) for i in range(npoly)]
    off += 18 * npoly
    indices = list(struct.unpack_from(f"<{ni}H", data, off))
    off += 2 * ni
    off += -off % 4
    wheels = [struct.unpack_from(WHEEL_FMT, data, off + 28 * i) for i in range(nw)]
    off += 28 * nw
    tracks = [struct.unpack_from(TRACK_FMT, data, off + 44 * i) for i in range(nt)]
    return {"flags": flags, "max_r": max_r, "first": tuple(c1), "second": tuple(c2),
            "rims": tuple(rims), "verts": verts, "polys": polys, "indices": indices,
            "wheels": wheels, "tracks": tracks}

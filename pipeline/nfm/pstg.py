"""``.pstg`` — binary stage format (parsed stage from stage.py -> runtime).

Little-endian::

    HEADER  16 bytes:  "PSTG"  u16 version(=1)  u16 flags(bit0 lightson)
                       u16 present   u16 nobjs   u16 pad
    present bits: 0 snap 1 sky 2 ground 3 polys 4 fog 5 texture 6 clouds
                  7 fadefrom 8 density 9 mountains 10 nlaps
    i16 snap[3] sky[3] ground[3] polys[3] fog[3] texture[4] clouds[5]   (absent = 0)
    i32 fadefrom, density, mountains, nlaps                             (absent = 0)
    OBJECTS nobjs * 24 bytes:  u8 op  u8 flags  i16 id  i32 a[5]
        op: 0 set 1 chk 2 fix 3 pile 4 maxr 5 maxl 6 maxt 7 maxb
        set : a = x, z, rot            flags = ord of first suffix char (0 if none)
        chk : a = x, z, rot, y         flags bit0 = has y
        fix : a = x, z, y, rot         flags bit0 = special
        pile: a = the five raw args    (the runtime expands it; see ContO pile ctor)
        max*: a = count, pos, offset
    STRINGS  u8 len + bytes: name, soundtrack track;  i16 volume, i32 size (packed)

The ints and directive order are exactly what GameSparker feeds Medium, so the
runtime replays them through the same setters.
"""

from __future__ import annotations

import struct
from typing import Any

OPS = ("set", "chk", "fix", "pile", "maxr", "maxl", "maxt", "maxb")
_ARR = (("snap", 3), ("sky", 3), ("ground", 3), ("polys", 3), ("fog", 3),
        ("texture", 4), ("clouds", 5))
_SCALARS = ("fadefrom", "density", "mountains", "nlaps")
_PRESENT = [k for k, _ in _ARR] + list(_SCALARS)
HDR = "<4sHHHHH"
OBJ = "<BBh5i"


def _s(text: str) -> bytes:
    b = text.encode("latin-1", "replace")[:255]
    return bytes([len(b)]) + b


def pack_pstg(st: dict[str, Any]) -> bytes:
    present = 0
    for i, key in enumerate(_PRESENT):
        if key in st and st[key]:
            present |= 1 << i
    objs = st.get("objects", [])
    out = bytearray(struct.pack(HDR, b"PSTG", 1, int(bool(st.get("lightson"))),
                                present, len(objs), 0))
    for key, n in _ARR:
        v = list(st.get(key, []))[:n]
        out += struct.pack(f"<{n}h", *(v + [0] * (n - len(v))))
    for key in _SCALARS:
        v = st.get(key, [0])
        out += struct.pack("<i", v[0] if isinstance(v, list) else v)
    for o in objs:
        op = OPS.index(o["op"])
        flags, ident = 0, o.get("id", 0)
        if o["op"] == "set":
            a = [o["x"], o["z"], o["rot"]]
            flags = ord(o["flags"][0]) if o.get("flags") else 0
        elif o["op"] == "chk":
            a = [o["x"], o["z"], o["rot"], o["y"] or 0]
            flags = int(o.get("y") is not None)
        elif o["op"] == "fix":
            a = [o["x"], o["z"], o["y"], o["rot"]]
            flags = int(bool(o.get("special")))
        elif o["op"] == "pile":
            a = list(o["args"])
        else:
            a = [o["count"], o["pos"], o["offset"]]
        out += struct.pack(OBJ, op, flags, ident, *(a + [0] * (5 - len(a)))[:5])
    sound = st.get("soundtrack", {})
    out += _s(st.get("name", "")) + _s(sound.get("track", ""))
    out += struct.pack("<hi", sound.get("volume", 100), sound.get("size", 0))
    return bytes(out)


def unpack_pstg(data: bytes) -> dict[str, Any]:
    """Inverse of pack_pstg (tests / tooling); objects come back as raw tuples."""
    magic, ver, flags, present, nobjs, _ = struct.unpack_from(HDR, data, 0)
    assert magic == b"PSTG" and ver == 1, (magic, ver)
    off = struct.calcsize(HDR)
    st: dict[str, Any] = {"lightson": bool(flags & 1), "present": present}
    for key, n in _ARR:
        st[key] = list(struct.unpack_from(f"<{n}h", data, off))
        off += 2 * n
    for key in _SCALARS:
        st[key] = struct.unpack_from("<i", data, off)[0]
        off += 4
    st["objects"] = []
    for _ in range(nobjs):
        op, fl, ident, *a = struct.unpack_from(OBJ, data, off)
        st["objects"].append({"op": OPS[op], "flags": fl, "id": ident, "a": a})
        off += struct.calcsize(OBJ)
    for key in ("name", "track"):
        n = data[off]
        st[key] = data[off + 1: off + 1 + n].decode("latin-1")
        off += 1 + n
    st["volume"], st["size"] = struct.unpack_from("<hi", data, off)
    return st

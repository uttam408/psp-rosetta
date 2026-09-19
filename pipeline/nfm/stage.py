"""Parser for NFM stage files (``OBJ/stages/N.txt``).

Mirrors the stage loader in GameSparker.java (the `loadstage`-style method around
lines 2400-2670, which dispatches on ``startsWith`` per line).  We only *describe*
the stage; expansion into world objects happens in the runtime, because several
directives (``pile``, ``maxr/l/t/b``, ``mountains``) are procedural and depend on
the ``ContO`` constructors.

Placement lines:

    set(id,x,z,rot)[flags]     road/scenery piece.  flags after ')': p = path point,
                               pt/pr/po/ph = path point of turn/ramp/... type
    chk(id,x,z,rot[,y])        checkpoint
    fix(id,x,z,y,rot)[s]       fix-point (car repair hoop); trailing `s` = special
    pile(a,b,c,d,e)            procedural pile of debris/blocks (ContO pile ctor)
    maxr/maxl/maxt/maxb(n,p,o) wall of n `thewall` pieces along one map edge

``id`` is a stage id (see tables.piece_name).
"""

from __future__ import annotations

import re
from typing import Any

from .tables import piece_name

_INT_DIRECTIVES = {
    # name: arity (values we keep); order = order in the file
    "snap": 3, "sky": 3, "ground": 3, "polys": 3, "fog": 3,
    "texture": 4, "clouds": 5, "density": 1, "fadefrom": 1, "mountains": 1,
    "nlaps": 1, "publish": 1,
}
_WALLS = ("maxr", "maxl", "maxt", "maxb")


def _ints(line: str) -> list[int]:
    m = re.match(r"[^(]*\(([^)]*)", line)
    if not m:
        return []
    return [int(t) for t in m.group(1).split(",") if t.strip()]


def _text(line: str) -> str:
    """GameSparker.getstring arg 0: everything up to the first ')' or ','."""
    m = re.match(r"[^(]*\(([^),]*)", line)
    return m.group(1) if m else ""


def parse_stage(text: str, stage_no: int | None = None) -> dict[str, Any]:
    st: dict[str, Any] = {"stage": stage_no, "lightson": False}
    objs: list[dict[str, Any]] = []
    unknown: list[str] = []

    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("//"):
            continue
        # order matters exactly as in the original: `startsWith` prefixes overlap
        # (`set` vs nothing else here, but `snap`/`sky`, `max*`, `soundtrack`).
        head = re.match(r"[A-Za-z]+", line)
        key = head.group(0) if head else ""
        if key == "name":
            st["name"] = _text(line).replace("|", ",")
        elif key == "stagemaker":
            st["maker"] = _text(line)
        elif key == "soundtrack":
            a = re.match(r"[^(]*\(([^)]*)\)", line)
            parts = a.group(1).split(",") if a else []
            st["soundtrack"] = {
                "track": parts[0].strip() if parts else "",
                "volume": max(50, min(300, int(parts[1]))) if len(parts) > 1 else 100,
                "size": int(parts[2]) if len(parts) > 2 else 0,
            }
        elif key == "lightson":
            st["lightson"] = True
        elif key in _INT_DIRECTIVES:
            st[key] = _ints(line)[: _INT_DIRECTIVES[key]]
        elif key in ("set", "chk", "fix", "pile"):
            a = _ints(line)
            suffix = line[line.index(")") + 1:].strip() if ")" in line else ""
            o: dict[str, Any] = {"op": key}
            if key == "set":
                o.update(id=a[0], x=a[1], z=a[2], rot=a[3], flags=suffix)
            elif key == "chk":
                o.update(id=a[0], x=a[1], z=a[2], rot=a[3], y=a[4] if len(a) > 4 else None)
            elif key == "fix":
                o.update(id=a[0], x=a[1], z=a[2], y=a[3], rot=a[4], special=suffix.startswith("s"))
            else:
                o.update(args=a)
            if "id" in o:
                o["model"] = piece_name(o["id"])
            objs.append(o)
        elif key in _WALLS:
            a = _ints(line)
            objs.append({"op": key, "count": a[0], "pos": a[1], "offset": a[2]})
        else:
            unknown.append(line)

    st["objects"] = objs
    if unknown:
        st["unknown"] = unknown
    return st

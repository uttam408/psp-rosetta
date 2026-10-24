#!/usr/bin/env python3
"""mklod.py — build a reduced-poly .pmesh variant from an NFM .pmesh.

Rule (v1, vegetation): drop degenerate (zero-area) polys outright, then keep the
largest polys by model-space area until `keep` percent of the mesh's total area
is covered.  Vertices are re-indexed down to the ones still referenced.

    mklod.py in.pmesh out.pmesh [keep_pct]
"""
import struct, sys, math

HDR = 56
POL = 18          # PmPoly: I H BBBBBB h h B B
PFMT = '<IHBBBBBBhhBB'


def load(path):
    d = bytearray(open(path, 'rb').read())
    assert d[:4] == b'PMSH', 'not a .pmesh'
    nv, npoly, nidx, maxr = struct.unpack_from('<IIII', d, 8)
    nwheels, ntracks = d[24], d[25]
    o = HDR
    verts = [struct.unpack_from('<fff', d, o + 12 * i) for i in range(nv)]
    po = o + 12 * nv
    polys = [list(struct.unpack_from(PFMT, d, po + POL * i)) for i in range(npoly)]
    io = po + POL * npoly
    idx = list(struct.unpack_from('<%dH' % nidx, d, io))
    tail_off = io + 2 * nidx
    tail_off += (4 - tail_off % 4) % 4
    tail = bytes(d[tail_off:])
    return d, verts, polys, idx, maxr, nwheels, ntracks, tail


def area(vs):
    a = 0.0
    v0 = vs[0]
    for k in range(1, len(vs) - 1):
        u = [vs[k][j] - v0[j] for j in range(3)]
        w = [vs[k + 1][j] - v0[j] for j in range(3)]
        c = (u[1] * w[2] - u[2] * w[1], u[2] * w[0] - u[0] * w[2], u[0] * w[1] - u[1] * w[0])
        a += math.sqrt(sum(x * x for x in c)) / 2
    return a


def build(path, out, keep_pct=97.0):
    d, verts, polys, idx, maxr, nwheels, ntracks, tail = load(path)
    info = []
    for i, p in enumerate(polys):
        vs = [verts[idx[p[0] + k]] for k in range(p[1])]
        info.append((area(vs), i))
    total = sum(a for a, _ in info)
    live = [(a, i) for a, i in info if a > 1e-6]
    dead = len(info) - len(live)
    live.sort(reverse=True)

    keep, acc = [], 0.0
    for a, i in live:
        keep.append(i)
        acc += a
        if acc >= total * keep_pct / 100.0:
            break
    keep.sort()

    # re-emit verts/polys/indices for the kept polys only
    vmap, nverts, nidx = {}, [], []
    npolys = []
    for i in keep:
        p = list(polys[i])
        first = len(nidx)
        for k in range(p[1]):
            src = idx[p[0] + k]
            if src not in vmap:
                vmap[src] = len(nverts)
                nverts.append(verts[src])
            nidx.append(vmap[src])
        p[0] = first
        npolys.append(p)

    mr = max((int(math.sqrt(v[0] ** 2 + v[1] ** 2 + v[2] ** 2)) for v in nverts), default=0)
    out_b = bytearray(d[:HDR])
    struct.pack_into('<IIII', out_b, 8, len(nverts), len(npolys), len(nidx), mr)
    for v in nverts:
        out_b += struct.pack('<fff', *v)
    for p in npolys:
        out_b += struct.pack(PFMT, *p)
    for i in nidx:
        out_b += struct.pack('<H', i)
    while len(out_b) % 4:
        out_b += b'\0'
    out_b += tail
    open(out, 'wb').write(bytes(out_b))
    print(f"{path} -> {out}: polys {len(polys)} -> {len(npolys)} "
          f"({dead} degenerate dropped), verts {len(verts)} -> {len(nverts)}, "
          f"area kept {100*acc/total:.1f}%, max_r {maxr} -> {mr}, "
          f"bytes {len(d)} -> {len(out_b)}")
    return len(npolys)


if __name__ == '__main__':
    build(sys.argv[1], sys.argv[2], float(sys.argv[3]) if len(sys.argv) > 3 else 97.0)

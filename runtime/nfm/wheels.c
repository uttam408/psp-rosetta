#include "wheels.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    PmVert *v; PmPoly *p; uint16_t *idx; PmPolyX *x;
    uint32_t nv, np, ni;
} Build;

static void add_plane(Build *b, int n, const int *X, const int *Y, const int *Z, const int rgb[3], int gr, int fs, bool solo,
                      int master, int wx, int wy, int wz)
{
    PmPoly *p = &b->p[b->np];
    memset(p, 0, sizeof *p);
    p->first_index = b->ni;
    p->nverts = (uint16_t)n;
    p->r = (uint8_t)rgb[0]; p->g = (uint8_t)rgb[1]; p->b = (uint8_t)rgb[2];
    p->gr = (int16_t)gr; p->fs = (int16_t)fs;
    p->no_outline = solo;
    for (int i = 0; i < n; i++) {
        b->v[b->nv] = (PmVert){ (float)X[i], (float)Y[i], (float)Z[i] };
        b->idx[b->ni++] = (uint16_t)b->nv++;
    }
    PmPolyX *x = &b->x[b->np];
    *x = (PmPolyX){ wx, wy, wz, 1, (uint8_t)master, 7, 0 };
    b->np++;
}

/* Wheels.make(n2=x, n3=y, n4=z, n5=steer, n6=width, n7=height, n8=gwgr) */
static void wheel_make(Build *b, const PMesh *src, const PmWheel *w, int *ground, int *sparkat)
{
    const int n2 = w->x, n3 = w->y, n4 = w->z, n5 = w->steer, n8 = w->gwgr;
    int rc[3] = { 120, 120, 120 };
    float size = 2.0f, depth = 3.0f;
    if (src->has_rims) {                                     /* Wheels.setrims */
        rc[0] = src->rims[0]; rc[1] = src->rims[1]; rc[2] = src->rims[2];
        size = src->rims[3] / 10.0f;
        if (size < 0.0f) size = 0.0f;
        depth = src->rims[4] / 10.0f;
        if (size > 0.0f) {
            if (depth / size > 41.0f) depth = size * 41.0f;
            if (depth / size < -25.0f) depth = -(size * 25.0f);
        }
    }
    const float n10 = w->width / 10.0f, n11 = w->height / 10.0f;
    const int n9 = n5 == 11 ? (int)(n2 + 4.0f * n10) : 0;
    *sparkat = (int)(n11 * 24.0f);
    *ground = (int)(n3 + 13.0f * n11);
    const int n12 = n2 < 0 ? 1 : -1;
    static const float ky[12] = { -9.1923f, -12.557f, -12.557f, -9.1923f, -3.3646f, 3.3646f, 9.1923f, 12.557f, 12.557f, 9.1923f, 3.3646f, -3.3646f };
    static const float kz[12] = { 9.1923f, 3.3646f, -3.3646f, -9.1923f, -12.557f, -12.557f, -9.1923f, -3.3646f, 3.3646f, 9.1923f, 12.557f, 12.557f };
    int py[20], pz[20], px[20];
    const int x0 = (int)(n2 - 4.0f * n10), x1 = (int)(n2 + 4.0f * n10);
    for (int i = 0; i < 20; i++) px[i] = x0;
    for (int i = 0; i < 12; i++) { py[i] = (int)((float)n3 + ky[i] * n11); pz[i] = (int)((float)n4 + kz[i] * n11); }
    py[12] = n3;                       pz[12] = (int)(n4 + 10.0f * size);
    py[13] = (int)(n3 + 8.66 * size);  pz[13] = (int)(n4 + 5.0f * size);
    py[14] = (int)(n3 + 8.66 * size);  pz[14] = (int)(n4 - 5.0f * size);
    py[15] = n3;                       pz[15] = (int)(n4 - 10.0f * size);
    py[16] = (int)(n3 - 8.66 * size);  pz[16] = (int)(n4 - 5.0f * size);
    py[17] = (int)(n3 - 8.66 * size);  pz[17] = (int)(n4 + 5.0f * size);
    py[18] = n3;                       pz[18] = (int)(n4 + 10.0f * size);
    py[19] = py[11];                   pz[19] = pz[11];
    const int k45[3] = { 45, 45, 45 };
    add_plane(b, 20, px, py, pz, k45, n8, 0, false, 1, n9, n3, n4);

    /* hub: six triangles (edge a,b of the hexagon + the centre) */
    int n13 = -16;
    if (size > 0.0f) { n13 = (int)(n8 - depth / size * 4.0f); if (n13 < -16) n13 = -16; }
    const int xd = (int)(n2 - depth * n10);
    const int master = (size > 0.0f && depth / size < 7.0f) ? 2 : 0;
    static const int ta[6] = { 12, 13, 14, 15, 16, 17 }, tb[6] = { 13, 14, 15, 16, 17, 12 };
    for (int t = 0; t < 6; t++) {
        int X[3] = { x0, x0, xd }, Y[3] = { py[ta[t]], py[tb[t]], n3 }, Z[3] = { pz[ta[t]], pz[tb[t]], n4 };
        add_plane(b, 3, X, Y, Z, rc, n13, 0, false, master, n9, n3, n4);
    }
    /* tyre wall: 12 quads between neighbouring points of the 12-gon, x -4..+4 */
    static const int qa[12] = { 1, 3, 3, 4, 6, 6, 7, 9, 9, 10, 0, 0 }, qb[12] = { 2, 2, 4, 5, 5, 7, 8, 8, 10, 11, 11, 1 };
    static const int fsm[12] = { -1, 1, 1, -1, 1, 1, -1, 1, 1, -1, 1, 1 };
    for (int q = 0; q < 12; q++) {
        int X[4] = { x0, x0, x1, x1 }, Y[4] = { py[qa[q]], py[qb[q]], py[qb[q]], py[qa[q]] }, Z[4] = { pz[qa[q]], pz[qb[q]], pz[qb[q]], pz[qa[q]] };
        add_plane(b, 4, X, Y, Z, k45, n8, fsm[q] * n12, true, 0, n9, n3, n4);
    }
}

#define ALIGN4(n) (((n) + 3u) & ~3u)

bool car_mesh_build(PMesh *dst, const PMesh *src)
{
    const int nw = src->nwheels > 4 ? 4 : src->nwheels;
    const uint32_t xv = (uint32_t)nw * 86, xp = (uint32_t)nw * 19;
    const size_t vb = (src->nverts + xv) * sizeof(PmVert), pb = (src->npolys + xp) * sizeof(PmPoly),
                 ib = (src->nindices + xv) * 2, wb = (size_t)src->nwheels * sizeof(PmWheel),
                 tb = (size_t)src->ntracks * sizeof(PmTrack), xb = (src->npolys + xp) * sizeof(PmPolyX);
    const size_t ov = 0, op = ALIGN4(ov + vb), oi = ALIGN4(op + pb), ow = ALIGN4(oi + ib), ot = ALIGN4(ow + wb),
                 ox = ALIGN4(ot + tb), total = ox + xb;
    uint8_t *buf = calloc(1, total ? total : 1);
    if (!buf) return false;
    *dst = *src;
    dst->owned = NULL;
    dst->xown = buf;
    Build b = { (PmVert *)(buf + ov), (PmPoly *)(buf + op), (uint16_t *)(buf + oi), (PmPolyX *)(buf + ox), 0, 0, 0 };
    memcpy(b.v, src->verts, src->nverts * sizeof(PmVert));
    memcpy(b.p, src->polys, src->npolys * sizeof(PmPoly));
    memcpy(b.idx, src->indices, src->nindices * 2);
    memcpy(buf + ow, src->wheels, wb);
    memcpy(buf + ot, src->tracks, tb);
    b.nv = src->nverts; b.np = src->npolys; b.ni = src->nindices;
    int ground = 0, sparkat = 0;
    for (int i = 0; i < nw; i++) wheel_make(&b, src, &src->wheels[i], &ground, &sparkat);
    dst->nverts = b.nv; dst->npolys = b.np; dst->nindices = b.ni;
    dst->verts = b.v; dst->polys = b.p; dst->indices = b.idx;
    dst->wheels = (const PmWheel *)(buf + ow); dst->tracks = (const PmTrack *)(buf + ot);
    dst->px = b.x;
    dst->grat = ground; dst->sparkat = sparkat;
    return true;
}

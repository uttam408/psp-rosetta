#include "pile.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MULT 0x5DEECE66DLL
#define MASK ((1LL << 48) - 1)

void jr_seed(JRandom *r, int64_t seed) { r->seed = (seed ^ MULT) & MASK; }

static int jr_next(JRandom *r, int bits)
{
    r->seed = (r->seed * MULT + 0xBLL) & MASK;
    return (int)(r->seed >> (48 - bits));   /* signed 32-bit truncation, as Java's (int) cast */
}

double jr_next_double(JRandom *r)
{
    int64_t hi = (int64_t)jr_next(r, 26), lo = (int64_t)jr_next(r, 27);
    return (double)((hi << 27) + lo) * (1.0 / (double)(1LL << 53));
}

static int jint(double d) { return d != d ? 0 : d >= 2147483647.0 ? 2147483647 : d <= -2147483648.0 ? (-2147483647 - 1) : (int)d; }

/* the A/B/A3/B4/A5 outline both pile_build (render mesh) and pile_add_trackers (collision) need; keeping one copy
 * keeps the two bit-identical, which matters since java.util.Random is consumed further by pile_build afterward. */
static void pile_outline(JRandom *rndp, int b, int c, int A[8], int B[8], int A3[8], int B4[8], int A5[8], int *maxR)
{
    JRandom rnd = *rndp;
    #define RD() jr_next_double(&rnd)
    float n4 = (float)b, n5 = (float)c;
    if (n5 < 2.0f) n5 = 2.0f;
    if (n5 > 6.0f) n5 = 6.0f;
    if (n4 < 2.0f) n4 = 2.0f;
    if (n4 > 6.0f) n4 = 6.0f;
    const float n6 = n4 / 1.5f;
    const float n7 = n5 / 1.5f * (1.0f + (n6 - 2.0f) * 0.1786f);

    float n8 = (float)(50.0 + 100.0 * RD());
    A[0] = -jint(n8 * n6 * 0.7071f);  B[0] = jint(n8 * n6 * 0.7071f);
    float n9 = (float)(50.0 + 100.0 * RD());
    A[1] = 0;                         B[1] = jint(n9 * n6);
    float n10 = (float)(50.0 + 100.0 * RD());
    A[2] = jint((double)(n10 * n6) * 0.7071); B[2] = A[2];
    A[3] = jint((float)(50.0 + 100.0 * RD()) * n6);  B[3] = 0;
    float n11 = (float)(50.0 + 100.0 * RD());
    A[4] = jint((double)(n11 * n6) * 0.7071); B[4] = -A[4];
    float n12 = (float)(50.0 + 100.0 * RD());
    A[5] = 0;                         B[5] = -jint(n12 * n6);
    float n13 = (float)(50.0 + 100.0 * RD());
    A[6] = -jint((double)(n13 * n6) * 0.7071); B[6] = A[6];
    A[7] = -jint((float)(50.0 + 100.0 * RD()) * n6); B[7] = 0;
    for (int i = 0; i < 8; i++) {
        A3[i]  = jint(A[i] * (0.2 + 0.4 * RD()));
        B4[i]  = jint(B[i] * (0.2 + 0.4 * RD()));
        A5[i]  = -jint((10.0 + 15.0 * RD()) * n7);
    }
    int rmax = 0;
    for (int j = 0; j < 8; j++) {
        int p = j - 1 == -1 ? 7 : j - 1, n = j + 1 == 8 ? 0 : j + 1;
        A[j]  = ((A[p]  + A[n])  / 2 + A[j])  / 2;
        B[j]  = ((B[p]  + B[n])  / 2 + B[j])  / 2;
        A3[j] = ((A3[p] + A3[n]) / 2 + A3[j]) / 2;
        B4[j] = ((B4[p] + B4[n]) / 2 + B4[j]) / 2;
        A5[j] = ((A5[p] + A5[n]) / 2 + A5[j]) / 2;
        int r1 = jint(sqrt((double)(A[j] * A[j] + B[j] * B[j])));
        if (r1 > rmax) rmax = r1;
        int r2 = jint(sqrt((double)(A3[j] * A3[j] + A5[j] * A5[j] + B4[j] * B4[j])));
        if (r2 > rmax) rmax = r2;
    }
    #undef RD
    *maxR = rmax;
    *rndp = rnd;
}

void pile_build(PMesh *out, const Medium *m, int seed, int b, int c)
{
    JRandom rnd; jr_seed(&rnd, seed);
    int A[8], B[8], A3[8], B4[8], A5[8], maxR;
    pile_outline(&rnd, b, c, A, B, A3, B4, A5, &maxR);
    float n4 = (float)b, n5 = (float)c;
    if (n5 < 2.0f) n5 = 2.0f;
    if (n5 > 6.0f) n5 = 6.0f;
    if (n4 < 2.0f) n4 = 2.0f;
    if (n4 > 6.0f) n4 = 6.0f;
    const float n6 = n4 / 1.5f;
    const float n7 = n5 / 1.5f * (1.0f + (n6 - 2.0f) * 0.1786f);
    #define RD() jr_next_double(&rnd)

    float n16 = -1.0f;
    float n17 = (n6 / n7 - 0.33f) / 33.4f;
    if (n17 < 0.005) n17 = 0.0f;
    if (n17 > 0.057) n17 = 0.057f;

    PmVert *verts = calloc(32, sizeof *verts);
    uint16_t *idx = calloc(32, sizeof *idx);
    PmPoly *polys = calloc(5, sizeof *polys);
    int nv = 0;
    for (int k = 0; k < 4; k++) {
        int a = k * 2, e = a + 2 == 8 ? 0 : a + 2;
        int x6[6] = { A[a], A[a + 1], A[e], A3[e], A3[a + 1], A3[a] };
        int z6[6] = { B[a], B[a + 1], B[e], B4[e], B4[a + 1], B4[a] };
        int y6[6] = { 0, 0, 0, A5[e], A5[a + 1], A5[a] };
        float n20 = (float)((0.17 - n17) * RD());
        while ((double)fabsf(n16 - n20) < 0.03 - (double)(n17 * 0.176f))
            n20 = (float)((0.17 - n17) * RD());
        n16 = n20;
        int col[3];
        for (int l = 0; l < 3; l++)
            col[l] = m->trk == 2 ? jint(390.0f / (2.2f + n20 - n17))
                                 : jint((float)(m->cpol[l] + m->cgrnd[l]) / (2.2f + n20 - n17));
        polys[k] = (PmPoly){ .first_index = (uint32_t)nv, .nverts = 6, .material = PM_MAT_RAW,
                             .r = (uint8_t)col[0], .g = (uint8_t)col[1], .b = (uint8_t)col[2], .gr = -8 };
        for (int i = 0; i < 6; i++, nv++) {
            verts[nv] = (PmVert){ (float)x6[i], (float)y6[i], (float)z6[i] };
            idx[nv] = (uint16_t)nv;
        }
    }
    float n21 = (float)(0.02 * RD());
    int col[3];
    for (int l = 0; l < 3; l++)
        col[l] = m->trk == 2 ? jint(390.0f / (2.15f + n21))
                             : jint((float)(m->cpol[l] + m->cgrnd[l]) / (2.15f + n21));
    polys[4] = (PmPoly){ .first_index = (uint32_t)nv, .nverts = 8, .material = PM_MAT_RAW,
                         .r = (uint8_t)col[0], .g = (uint8_t)col[1], .b = (uint8_t)col[2], .gr = -8 };
    for (int i = 0; i < 8; i++, nv++) {
        verts[nv] = (PmVert){ (float)A3[i], (float)A5[i], (float)B4[i] };
        idx[nv] = (uint16_t)nv;
    }
    #undef RD

    memset(out, 0, sizeof *out);
    out->flags = PMF_DECOR | PMF_ROAD;
    out->nverts = 32; out->npolys = 5; out->nindices = 32; out->max_r = (uint32_t)maxR;
    out->disline = 4; out->disp = (uint16_t)(maxR / 17); out->grounded_pct = 115;
    out->verts = verts; out->polys = polys; out->indices = idx;
}

void pile_free(PMesh *m)
{
    free((void *)m->verts); free((void *)m->polys); free((void *)m->indices); free(m->uown); free(m->psz); free(m->parea);
    memset(m, 0, sizeof *m);
}

/* ContO's pile constructor (port of the `this.t.*[this.t.nt]` block): 4 sloped sides, one per outline quadrant
 * (n23 0..3 = -z, +x, +z, -x face), each rising toward the tracker's own middle, plus one flat top box spanning
 * the inner rectangle.  Angles are clamped to +-40 as in the original. */
void pile_add_trackers(Trackers *t, const Medium *m, int seed, int b, int c, int x, int y, int z)
{
    (void)m;
    JRandom rnd; jr_seed(&rnd, seed);
    int A[8], B[8], A3[8], B4[8], A5[8], maxR;
    pile_outline(&rnd, b, c, A, B, A3, B4, A5, &maxR);
    int ext_z0 = 0, ext_z1 = 0, ext_x0 = 0, ext_x1 = 0;
    for (int n23 = 0; n23 < 4 && t->n < MAD_MAXTRK; ++n23) {
        int i = t->n++;
        int n24 = n23 * 2 + 1;
        t->y[i] = A5[n24] / 2;
        t->rady[i] = abs(A5[n24] / 2);
        if (n23 == 0 || n23 == 2) {
            t->z[i] = (B[n24] + B4[n24]) / 2;
            t->radz[i] = abs(t->z[i] - B[n24]);
            int n25 = n23 * 2 + 2; if (n25 == 8) n25 = 0;
            t->x[i] = (A[n23 * 2] + A[n25]) / 2;
            t->radx[i] = abs(t->x[i] - A[n23 * 2]);
        } else {
            t->x[i] = (A[n24] + A3[n24]) / 2;
            t->radx[i] = abs(t->x[i] - A[n24]);
            int n26 = n23 * 2 + 2; if (n26 == 8) n26 = 0;
            t->z[i] = (B[n23 * 2] + B[n26]) / 2;
            t->radz[i] = abs(t->z[i] - B[n23 * 2]);
        }
        if (n23 == 0) {
            ext_z0 = t->z[i] - t->radz[i];
            t->zy[i] = (int)(atan((double)t->rady[i] / t->radz[i]) / 0.017453292519943295);
            if (t->zy[i] > 40) t->zy[i] = 40;
            t->xy[i] = 0;
        } else if (n23 == 1) {
            ext_x0 = t->x[i] - t->radx[i];
            t->xy[i] = (int)(atan((double)t->rady[i] / t->radx[i]) / 0.017453292519943295);
            if (t->xy[i] > 40) t->xy[i] = 40;
            t->zy[i] = 0;
        } else if (n23 == 2) {
            ext_z1 = t->z[i] + t->radz[i];
            t->zy[i] = -(int)(atan((double)t->rady[i] / t->radz[i]) / 0.017453292519943295);
            if (t->zy[i] < -40) t->zy[i] = -40;
            t->xy[i] = 0;
        } else {
            ext_x1 = t->x[i] + t->radx[i];
            t->xy[i] = -(int)(atan((double)t->rady[i] / t->radx[i]) / 0.017453292519943295);
            if (t->xy[i] < -40) t->xy[i] = -40;
            t->zy[i] = 0;
        }
        t->x[i] += x; t->z[i] += z; t->y[i] += y;
        t->skd[i] = 2; t->dam[i] = 1; t->notwall[i] = false; t->decor[i] = true;
        t->rady[i] += 10;
    }
    if (t->n >= MAD_MAXTRK) return;
    int i = t->n++;
    int ysum = 0;
    for (int n28 = 0; n28 < 8; ++n28) ysum += A5[n28];
    t->y[i] = ysum / 8 + y;
    t->rady[i] = 200;
    t->radx[i] = ext_x0 - ext_x1;
    t->radz[i] = ext_z0 - ext_z1;
    t->x[i] = (ext_x0 + ext_x1) / 2 + x;
    t->z[i] = (ext_z0 + ext_z1) / 2 + z;
    t->zy[i] = 0; t->xy[i] = 0;
    t->skd[i] = 4; t->dam[i] = 1; t->notwall[i] = false; t->decor[i] = true;
}

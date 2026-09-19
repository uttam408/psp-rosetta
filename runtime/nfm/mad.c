/* mad.c — car physics.  Bodies come from gen/mad_gen.inc (spec/gen_nfm_mad.py). */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "mad.h"

#define JABS(x) _Generic((x), int: abs, float: fabsf, double: fabs)(x)

/* cosmetic hooks: sound, dust, sparks, crash meter, replay recording — not ported yet */
#define mad_fx(...)   ((void)0)
#define mad_sprk(...) ((void)0)
#define mad_dust(...) ((void)0)

static void rot(float *array, float *array2, int n, int n2, int n3, int n4);
static int rpy(float n, float n2, float n3, float n4, float n5, float n6);
static int py(int n, int n2, int n3, int n4);
static void mad_distruct(Mad *M, CarObj *o);

void mad_init(Mad *M, MadEnv *env, int im) {
    memset(M, 0, sizeof *M);
    M->env = env;
    M->cd = NFM_CARS;
    M->im = im;
    M->drag = 0.5f;
    M->pmlt = M->nmlt = 1;
    M->focus = -1;
    M->power = 75.0f;
    M->fixes = -1;
    M->rng = 0x9e3779b9u ^ (uint32_t)(im * 2654435761u);
}

float mad_rand(Mad *M) {
    M->rng = M->rng * 1664525u + 1013904223u;
    return (float)(M->rng >> 8) / 16777216.0f;
}

bool carobj_init(CarObj *o, Inst *in) {
    const PMesh *pm = in->mesh;
    memset(o, 0, sizeof *o);
    o->in = in;
    o->grat = pm->grat;
    o->maxR = (int)pm->max_r;
    for (int i = 0; i < 4 && i < pm->nwheels; ++i) { o->keyx[i] = pm->wheels[i].keyx; o->keyz[i] = pm->wheels[i].keyz; }
    o->npl = (int)pm->npolys;
    o->p = calloc(o->npl, sizeof(MadPoly));
    o->vbuf = malloc(sizeof(int) * 3 * pm->nindices);
    if (!o->p || !o->vbuf) { carobj_free(o); return false; }
    for (int k = 0; k < o->npl; ++k) {
        const PmPoly *pp = &pm->polys[k];
        MadPoly *d = &o->p[k];
        d->n = pp->nverts;
        d->gr = pp->gr;
        d->glass = pp->material == PM_MAT_GLASS;
        d->nocol = false;
        d->wz = pm->px ? pm->px[k].wz : 0;
        d->ox = o->vbuf + 3 * pp->first_index;
        d->oy = d->ox + pp->nverts;
        d->oz = d->oy + pp->nverts;
        for (int v = 0; v < pp->nverts; ++v) {
            const PmVert *pv = &pm->verts[pm->indices[pp->first_index + v]];
            d->ox[v] = (int)pv->x; d->oy[v] = (int)pv->y; d->oz[v] = (int)pv->z;
        }
    }
    return true;
}

void carobj_free(CarObj *o) {
    free(o->p); free(o->vbuf);
    o->p = NULL; o->vbuf = NULL;
}

bool trackers_init(Trackers *T) {
    memset(T, 0, sizeof *T);
    T->x = calloc(MAD_MAXTRK, sizeof(int)); T->y = calloc(MAD_MAXTRK, sizeof(int)); T->z = calloc(MAD_MAXTRK, sizeof(int));
    T->xy = calloc(MAD_MAXTRK, sizeof(int)); T->zy = calloc(MAD_MAXTRK, sizeof(int)); T->skd = calloc(MAD_MAXTRK, sizeof(int));
    T->dam = calloc(MAD_MAXTRK, sizeof(int)); T->radx = calloc(MAD_MAXTRK, sizeof(int));
    T->radz = calloc(MAD_MAXTRK, sizeof(int)); T->rady = calloc(MAD_MAXTRK, sizeof(int));
    T->notwall = calloc(MAD_MAXTRK, sizeof(bool)); T->decor = calloc(MAD_MAXTRK, sizeof(bool));
    return T->x && T->y && T->z && T->xy && T->zy && T->skd && T->dam && T->radx && T->radz && T->rady && T->notwall && T->decor;
}

void trackers_add_piece(Trackers *T, const PMesh *pm, int x, int y, int z, int xz, bool decor) {
    int n = xz;
    for (int k = 0; k < pm->ntracks && T->n < MAD_MAXTRK; ++k) {
        const PmTrack *t = &pm->tracks[k];
        int i = T->n++;
        T->xy[i] = (int)(t->xy * m_cos(n) - t->zy * m_sin(n));
        T->zy[i] = (int)(t->zy * m_cos(n) + t->xy * m_sin(n));
        T->x[i] = (int)(x + t->x * m_cos(n) - t->z * m_sin(n));
        T->z[i] = (int)(z + t->z * m_cos(n) + t->x * m_sin(n));
        T->y[i] = y + t->y;
        T->skd[i] = t->skid;
        T->dam[i] = t->dam;
        T->notwall[i] = t->notwall;
        T->decor[i] = decor;
        int a = abs(n);
        if (a == 180) a = 0;
        T->radx[i] = (int)fabsf(t->radx * m_cos(a) + t->radz * m_sin(a));
        T->radz[i] = (int)fabsf(t->radx * m_sin(a) + t->radz * m_cos(a));
        T->rady[i] = t->rady;
    }
}

void trackers_add_wall(Trackers *T, int x, int y, int z, int radx, int radz, int rady, int xy, int zy) {
    if (T->n >= MAD_MAXTRK) return;
    int i = T->n++;
    T->x[i] = x; T->y[i] = y; T->z[i] = z; T->radx[i] = radx; T->radz[i] = radz; T->rady[i] = rady;
    T->xy[i] = xy; T->zy[i] = zy; T->dam[i] = 167; T->decor[i] = false; T->skd[i] = 0; T->notwall[i] = false;
}

void trackers_divide(Trackers *T, int sx, int n, int sz, int n2) {
    T->sx = sx; T->sz = sz;
    T->ncx = n / 3000; if (T->ncx <= 0) T->ncx = 1;
    T->ncz = n2 / 3000; if (T->ncz <= 0) T->ncz = 1;
    T->sect = calloc(T->ncx, sizeof(Sect *));
    int *tmp = malloc(sizeof(int) * (2 * MAD_MAXTRK + 2));
    for (int i = 0; i < T->ncx; ++i) {
        T->sect[i] = calloc(T->ncz, sizeof(Sect));
        for (int j = 0; j < T->ncz; ++j) {
            int cx = T->sx + i * 3000 + 1500, cz = T->sz + j * 3000 + 1500, c = 0;
            for (int k = 0; k < T->n; ++k) {
                int d = py(cx, T->x[k], cz, T->z[k]);
                if (d < 20250000 && d > 0 && T->dam[k] != 167) tmp[c++] = k;
            }
            if (i == 0 || j == 0 || i == T->ncx - 1 || j == T->ncz - 1)
                for (int l = 0; l < T->n; ++l) if (T->dam[l] == 167) tmp[c++] = l;
            if (c == 0) tmp[c++] = 0;
            T->sect[i][j].v = malloc(sizeof(int) * c);
            memcpy(T->sect[i][j].v, tmp, sizeof(int) * c);
            T->sect[i][j].length = c;
        }
    }
    free(tmp);
    for (int k = 0; k < T->n; ++k) if (T->dam[k] == 167) T->dam[k] = 1;
    --T->ncx; --T->ncz;
}

void trackers_free(Trackers *T) {
    free(T->x); free(T->y); free(T->z); free(T->xy); free(T->zy); free(T->skd); free(T->dam);
    free(T->radx); free(T->radz); free(T->rady); free(T->notwall); free(T->decor);
    if (T->sect) {
        for (int i = 0; i <= T->ncx; ++i) {
            for (int j = 0; j <= T->ncz; ++j) free(T->sect[i][j].v);
            free(T->sect[i]);
        }
        free(T->sect);
    }
    memset(T, 0, sizeof *T);
}

#include "gen/mad_gen.inc"

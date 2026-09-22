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
    M->steer_cap = MAD_STEER_ORIG;
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

/* Trees and cacti carry four thin wall trackers (zy=+-90 slabs ~54x402, xy=+-90 slabs ~402x54, a "+").  Only the
 * car's corner points are tested, and a wall pushes a point along its face, so a corner caught in a thin arm slides
 * along it and the car scoots round the trunk.  Each slab of those pieces is widened to a TREE_HALF square so all
 * four walls close into a solid pillar; the +-90 wall orientation is kept, since the wall branches key off it. */
#define TREE_HALF 110
static bool is_plus_slabs(const PMesh *pm) {
    if (pm->ntracks != 4) return false;
    for (int k = 0; k < 4; ++k) {
        const PmTrack *t = &pm->tracks[k];
        int lo = t->radx < t->radz ? t->radx : t->radz, hi = t->radx < t->radz ? t->radz : t->radx;
        bool wall = (abs(t->xy) == 90) != (abs(t->zy) == 90);
        if (!wall || t->x || t->z || lo > 60 || hi < 150) return false;
    }
    return true;
}

void trackers_add_piece(Trackers *T, const PMesh *pm, int x, int y, int z, int xz, bool decor) {
    int n = xz;
    bool veg = decor && is_plus_slabs(pm);
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
        if (veg) T->radx[i] = T->radz[i] = TREE_HALF;
    }
}

/* bumproad's mesh undulates (70 high over 560 long, ~7 degrees, crests at z = 0 and +-1120..1190, troughs at +-560 and
 * +-1680) but the shipped trackers are flat, so the car drove straight through the bumps.  Rebuild the centre strip
 * (x +-840) as six sloped floors, one per half-wave, the same construction offbump uses (zy < 0 rises toward +z);
 * the flat end strips are trimmed to start where the bumps end. */
void trackers_add_bumproad(Trackers *T, const PMesh *pm, int x, int y, int z, int xz) {
    static const int cz[6] = { -1400, -840, -280, 280, 840, 1400 };
    static const int zy[6] = { -7, 7, -7, 7, -7, 7 };
    PmTrack t[16];
    int n = 0;
    if (pm->ntracks > 8) { trackers_add_piece(T, pm, x, y, z, xz, false); return; }
    for (int k = 0; k < pm->ntracks; ++k) {
        PmTrack c = pm->tracks[k];
        if (c.z == 0 && c.x == 0 && c.radz == 1960) continue;                    /* flat centre strip: replaced */
        if (c.x == 0 && (c.z == 2240 || c.z == -2240)) {                          /* flat ends: 1400..3080 -> 1680..3080 */
            c.z = c.z > 0 ? 2380 : -2380;
            c.radz = 700;
        }
        t[n++] = c;
    }
    for (int i = 0; i < 6; ++i) {
        PmTrack c = { 0 };
        c.zy = zy[i]; c.radx = 840; c.rady = 60; c.radz = 280; c.y = -35; c.z = cz[i]; c.skid = 3; c.dam = 1;
        t[n++] = c;
    }
    PMesh one = *pm;
    one.tracks = t;
    one.ntracks = n;
    trackers_add_piece(T, &one, x, y, z, xz, false);
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

#define mad_drive mad_drive_raw
#include "gen/mad_gen.inc"
#undef mad_drive

/* mad_drive: the generated physics plus one assist -- Mad.steer_cap limits the wheel steer angle (the original
 * allows +-36, which is very twitchy at speed on a d-pad/stick).  The physics ramps wxz by `turn` per tick and
 * uses the result immediately, so the ramp is pre-limited to land exactly on the cap. */
void mad_drive(Mad *M, Control *ctl, CarObj *o, Trackers *T, CheckPoints *cp) {
    int cap = M->steer_cap;
    if (cap < MAD_STEER_ORIG) {
        int lim = cap - M->cd[M->cn].turn;
        if (lim < 0) lim = 0;
        if (ctl->left && o->in->wxz > lim) o->in->wxz = lim;
        if (ctl->right && o->in->wxz < -lim) o->in->wxz = -lim;
    }
    mad_drive_raw(M, ctl, o, T, cp);
    if (cap < MAD_STEER_ORIG) {
        if (o->in->wxz > cap) o->in->wxz = cap;
        if (o->in->wxz < -cap) o->in->wxz = -cap;
    }
}

#include "stage.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/pak.h"
#include "gen/npieces.h"

static int16_t rd16(const uint8_t *p) { int16_t v; memcpy(&v, p, 2); return v; }
static int32_t rd32(const uint8_t *p) { int32_t v; memcpy(&v, p, 4); return v; }

bool stage_load(Stage *s, const uint8_t *d, size_t n)
{
    memset(s, 0, sizeof *s);
    if (n < 14 || memcmp(d, "PSTG", 4) != 0 || (uint16_t)rd16(d + 4) != 1) return false;
    s->lightson = rd16(d + 6) & 1;
    s->present  = (uint16_t)rd16(d + 8);
    s->nobjs    = (uint16_t)rd16(d + 10);
    size_t off = 14;
    int16_t *arrs[] = { s->snap, s->sky, s->ground, s->polys, s->fog, s->texture, s->clouds };
    const int lens[] = { 3, 3, 3, 3, 3, 4, 5 };
    for (int i = 0; i < 7; i++)
        for (int k = 0; k < lens[i]; k++, off += 2) arrs[i][k] = rd16(d + off);
    s->fadefrom = rd32(d + off); off += 4;
    s->density  = rd32(d + off); off += 4;
    s->mountains = rd32(d + off); off += 4;
    s->nlaps    = rd32(d + off); off += 4;
    if (off + (size_t)s->nobjs * 24 > n) return false;
    s->objs = calloc(s->nobjs ? s->nobjs : 1, sizeof(StObj));
    for (uint32_t i = 0; i < s->nobjs; i++, off += 24) {
        StObj *o = &s->objs[i];
        o->op = d[off]; o->flags = d[off + 1]; o->id = rd16(d + off + 2);
        for (int k = 0; k < 5; k++) o->a[k] = rd32(d + off + 4 + 4 * k);
    }
    char *strs[] = { s->name, s->track };
    for (int i = 0; i < 2; i++) {
        if (off >= n) break;
        size_t len = d[off++];
        if (off + len > n) return false;
        memcpy(strs[i], d + off, len); strs[i][len] = 0;
        off += len;
    }
    if (off + 6 <= n) { s->volume = rd16(d + off); s->size = rd32(d + off + 2); }
    return true;
}

void stage_free(Stage *s) { free(s->objs); memset(s, 0, sizeof *s); }

enum { P_SNAP = 1 << 0, P_SKY = 1 << 1, P_GROUND = 1 << 2, P_POLYS = 1 << 3, P_FOG = 1 << 4,
       P_TEXTURE = 1 << 5, P_CLOUDS = 1 << 6, P_FADEFROM = 1 << 7, P_DENSITY = 1 << 8 };

void stage_apply_env(const Stage *s, Medium *m)
{
    /* stage-file order matters: setgrnd reads texture, setpolys overrides cpol */
    if (s->present & P_SNAP)   medium_setsnap(m, s->snap[0], s->snap[1], s->snap[2]);
    if (s->present & P_SKY)    medium_setsky(m, s->sky[0], s->sky[1], s->sky[2]);
    if (s->present & P_FOG)    medium_setfade(m, s->fog[0], s->fog[1], s->fog[2]);
    if (s->present & P_GROUND) medium_setgrnd(m, s->ground[0], s->ground[1], s->ground[2]);
    if (s->present & P_TEXTURE) {
        int t3 = s->texture[3] < 20 ? 20 : s->texture[3] > 60 ? 60 : s->texture[3];
        for (int i = 0; i < 3; i++) m->texture[i] = s->texture[i];
        m->texture[3] = t3;
        medium_setgrnd(m, m->ogrnd[0], m->ogrnd[1], m->ogrnd[2]);   /* cpol from texture */
    }
    if (s->present & P_POLYS)  medium_setpolys(m, s->polys[0], s->polys[1], s->polys[2]);
    if (s->present & P_FADEFROM) medium_fadfrom(m, s->fadefrom);
    if (s->present & P_DENSITY) {
        int d = (s->density + 1) * 2 - 1;
        m->fogd = d < 1 ? 1 : d > 30 ? 30 : d;
    }
    m->lightson = s->lightson;
}

bool scene_build(Scene *sc, const Stage *s, Medium *m)
{
    memset(sc, 0, sizeof *sc);
    sc->meshes = calloc(NPIECE_COUNT, sizeof(PMesh));
    bool have[NPIECE_COUNT] = { false }, tried[NPIECE_COUNT] = { false };
    sc->inst = calloc(s->nobjs ? s->nobjs : 1, sizeof(Inst));
    for (uint32_t i = 0; i < s->nobjs; i++) {
        const StObj *o = &s->objs[i];
        if (o->op != ST_SET && o->op != ST_CHK && o->op != ST_FIX) { sc->skipped++; continue; }
        int pi = o->id - NPIECE_BASE;
        if (pi < 0 || pi >= NPIECE_COUNT) { sc->skipped++; continue; }
        if (!tried[pi]) {
            tried[pi] = true;
            char id[96];
            snprintf(id, sizeof id, "mesh/piece/%s", NPIECE_NAMES[pi]);
            const PakAsset *a = pak_find(id);
            have[pi] = a && pmesh_load(&sc->meshes[pi], a->data, a->size);
            if (!have[pi]) fprintf(stderr, "stage: missing piece %s\n", id);
        }
        if (!have[pi]) { sc->skipped++; continue; }
        int x = o->a[0], z = o->a[1], rot = o->a[2], y = 250;      /* ground - grat (grat=0 for pieces) */
        if (o->op == ST_CHK && (o->flags & 1)) y = o->a[3];
        if (o->op == ST_FIX) { y = o->a[2]; rot = o->a[3]; }
        inst_init(&sc->inst[sc->n++], m, &sc->meshes[pi], x, y, z, rot, -1, -1);
    }
    return true;
}

void scene_free(Scene *sc)
{
    for (uint32_t i = 0; i < sc->n; i++) inst_free(&sc->inst[i]);
    free(sc->inst); free(sc->meshes);
    memset(sc, 0, sizeof *sc);
}

static Inst **g_order; static uint32_t g_ocap;

static int by_dist_desc(const void *a, const void *b)
{
    int da = (*(Inst *const *)a)->dist, db = (*(Inst *const *)b)->dist;
    return (da < db) - (da > db);
}

void scene_draw(Medium *m, Scene *sc)
{
    medium_draw_backdrop(m);
    if (sc->n > g_ocap) { g_ocap = sc->n; g_order = realloc(g_order, g_ocap * sizeof *g_order); }
    for (uint32_t i = 0; i < sc->n; i++) g_order[i] = &sc->inst[i];
    /* far to near by last frame's dist; qsort isn't stable but the original's ties are cosmetic */
    qsort(g_order, sc->n, sizeof *g_order, by_dist_desc);
    for (uint32_t i = 0; i < sc->n; i++) inst_draw(m, g_order[i]);
}

#include "pmesh.h"
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#define HDR_SIZE 56

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }

bool pmesh_load(PMesh *m, const uint8_t *d, size_t size)
{
    if (size < HDR_SIZE || memcmp(d, "PMSH", 4) != 0 || rd16(d + 4) != 3) return false;
    memset(m, 0, sizeof *m);
    /* the arrays below are dereferenced in place: real MIPS (PSP) faults on misaligned float/int loads */
    if ((uintptr_t)d & 3) {
        uint8_t *c = malloc(size);
        if (!c) return false;
        memcpy(c, d, size);
        m->owned = c; d = c;
    }
    m->flags    = rd16(d + 6);
    m->nverts   = rd32(d + 8);
    m->npolys   = rd32(d + 12);
    m->nindices = rd32(d + 16);
    m->max_r    = rd32(d + 20);
    m->nwheels  = d[24];
    m->ntracks  = d[25];
    memcpy(m->first_color,  d + 28, 3);
    memcpy(m->second_color, d + 32, 3);
    memcpy(m->rims, d + 36, sizeof m->rims);
    m->has_rims = (m->flags & 0x20) != 0;
    m->disline = rd16(d + 48);
    m->disp    = rd16(d + 50);
    m->grounded_pct = rd16(d + 52);

    size_t off = HDR_SIZE;
    size_t vb = (size_t)m->nverts * sizeof(PmVert);
    size_t pb = (size_t)m->npolys * sizeof(PmPoly);
    size_t ib = (size_t)m->nindices * 2;
    size_t after = off + vb + pb + ib;
    after += (4 - after % 4) % 4;
    size_t end = after + (size_t)m->nwheels * sizeof(PmWheel) + (size_t)m->ntracks * sizeof(PmTrack);
    if (end > size) { pmesh_free(m); return false; }

    m->verts   = (const PmVert *)(d + off);
    m->polys   = (const PmPoly *)(d + off + vb);
    m->indices = (const uint16_t *)(d + off + vb + pb);
    m->wheels  = (const PmWheel *)(d + after);
    m->tracks  = (const PmTrack *)(d + after + (size_t)m->nwheels * sizeof(PmWheel));
    for (uint32_t i = 0; i < m->npolys; i++) {
        const PmPoly *p = &m->polys[i];
        if ((size_t)p->first_index + p->nverts > m->nindices) { pmesh_free(m); return false; }
    }
    return true;
}

void pmesh_free(PMesh *m) { free(m->owned); free(m->xown); free(m->uown); free(m->psz); m->owned = m->xown = m->uown = NULL; m->psz = NULL; m->maxpsz = 0; m->uidx = m->usrc = NULL; m->nuniq = 0; }

void pmesh_uniq(PMesh *m)
{
    if (m->uown || !m->nindices) return;
    uint32_t ni = m->nindices, cap = 1;
    while (cap < ni * 2) cap <<= 1;
    uint16_t *buf = malloc((size_t)ni * 2 * 2 + (size_t)cap * 2);   /* uidx[ni] + usrc[ni] + hash[cap] */
    if (!buf) return;
    uint16_t *uidx = buf, *usrc = buf + ni, *hash = buf + 2 * ni;
    memset(hash, 0xFF, (size_t)cap * 2);
    uint32_t nu = 0;
    for (uint32_t i = 0; i < ni; i++) uidx[i] = 0xFFFF;
    for (uint32_t pi = 0; pi < m->npolys; pi++) {
        if (m->px && m->px[pi].wheel) continue;
        const PmPoly *p = &m->polys[pi];
        for (uint32_t k = 0; k < p->nverts; k++) {
            uint32_t ix = p->first_index + k;
            const PmVert *v = &m->verts[m->indices[ix]];
            uint32_t bits[3]; memcpy(bits, v, 12);
            uint32_t h = (bits[0] * 73856093u ^ bits[1] * 19349663u ^ bits[2] * 83492791u) & (cap - 1);
            for (;;) {
                uint16_t u = hash[h];
                if (u == 0xFFFF) { hash[h] = (uint16_t)nu; usrc[nu] = m->indices[ix]; uidx[ix] = (uint16_t)nu; nu++; break; }
                if (memcmp(&m->verts[usrc[u]], v, 12) == 0) { uidx[ix] = u; break; }
                h = (h + 1) & (cap - 1);
            }
        }
    }
    m->uown = buf; m->uidx = uidx; m->usrc = usrc; m->nuniq = nu;
    float *psz = malloc((m->npolys ? m->npolys : 1) * sizeof(float));
    if (!psz) return;
    float mx = 0;
    for (uint32_t pi = 0; pi < m->npolys; pi++) {
        const PmPoly *p = &m->polys[pi];
        if (m->px && m->px[pi].wheel) { psz[pi] = 1e9f; continue; }
        float best = 0;
        for (uint32_t a = 0; a < p->nverts; a++)
            for (uint32_t b = a + 1; b < p->nverts; b++) {
                const PmVert *va = &m->verts[m->indices[p->first_index + a]], *vb = &m->verts[m->indices[p->first_index + b]];
                float dx = va->x - vb->x, dy = va->y - vb->y, dz = va->z - vb->z, d2 = dx * dx + dy * dy + dz * dz;
                if (d2 > best) best = d2;
            }
        psz[pi] = sqrtf(best);
        if (psz[pi] > mx) mx = psz[pi];
    }
    m->psz = psz; m->maxpsz = mx;
}

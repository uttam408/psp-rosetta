#include "pmesh.h"
#include <string.h>

#define HDR_SIZE 56

static uint32_t rd32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint16_t rd16(const uint8_t *p) { uint16_t v; memcpy(&v, p, 2); return v; }

bool pmesh_load(PMesh *m, const uint8_t *d, size_t size)
{
    if (size < HDR_SIZE || memcmp(d, "PMSH", 4) != 0 || rd16(d + 4) != 2) return false;
    memset(m, 0, sizeof *m);
    m->flags    = rd16(d + 6);
    m->nverts   = rd32(d + 8);
    m->npolys   = rd32(d + 12);
    m->nindices = rd32(d + 16);
    m->max_r    = rd32(d + 20);
    m->nwheels  = d[24];
    m->ntracks  = d[25];
    memcpy(m->first_color,  d + 28, 3);
    memcpy(m->second_color, d + 32, 3);
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
    if (end > size) return false;

    m->verts   = (const PmVert *)(d + off);
    m->polys   = (const PmPoly *)(d + off + vb);
    m->indices = (const uint16_t *)(d + off + vb + pb);
    m->wheels  = (const PmWheel *)(d + after);
    m->tracks  = (const PmTrack *)(d + after + (size_t)m->nwheels * sizeof(PmWheel));
    for (uint32_t i = 0; i < m->npolys; i++) {
        const PmPoly *p = &m->polys[i];
        if ((size_t)p->first_index + p->nverts > m->nindices) return false;
    }
    return true;
}

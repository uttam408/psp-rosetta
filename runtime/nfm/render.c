#include "render.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

int g_polys_in, g_polys_drawn;

#define MAXV   32        /* per-poly vertex cap (clipping can add one) */
#define NEAR_Z 10.0f

typedef struct { float x, y, z; } V3;
typedef struct { float x, y; } V2;

typedef struct {
    float    depth;
    uint32_t color;
    int      n;
    V2       v[MAXV];
} Drawn;

static Drawn  *g_list;
static size_t  g_cap, g_len;

static int cmp_depth(const void *a, const void *b)
{
    float da = ((const Drawn *)a)->depth, db = ((const Drawn *)b)->depth;
    return (da < db) - (da > db);        /* far first */
}

void frame_clear(Frame *f, uint32_t rgb)
{
    for (int i = 0; i < f->w * f->h; i++) f->px[i] = rgb;
    g_polys_in = g_polys_drawn = 0;
}

static uint32_t shade(int r, int g, int b, float k)
{
    int R = (int)(r * k), G = (int)(g * k), B = (int)(b * k);
    if (R > 255) R = 255; if (G > 255) G = 255; if (B > 255) B = 255;
    return ((uint32_t)R << 16) | ((uint32_t)G << 8) | (uint32_t)B;
}

/* scanline fill, even-odd, integer scanlines through pixel centres */
static void fill_poly(Frame *f, const V2 *v, int n, uint32_t color)
{
    float ymin = v[0].y, ymax = v[0].y;
    for (int i = 1; i < n; i++) {
        if (v[i].y < ymin) ymin = v[i].y;
        if (v[i].y > ymax) ymax = v[i].y;
    }
    int y0 = (int)ceilf(ymin - 0.5f), y1 = (int)floorf(ymax - 0.5f);
    if (y0 < 0) y0 = 0;
    if (y1 >= f->h) y1 = f->h - 1;
    for (int y = y0; y <= y1; y++) {
        float sy = y + 0.5f, xs[MAXV];
        int nx = 0;
        for (int i = 0, j = n - 1; i < n; j = i++) {
            const V2 *a = &v[j], *b = &v[i];
            if ((a->y <= sy) == (b->y <= sy)) continue;
            xs[nx++] = a->x + (sy - a->y) * (b->x - a->x) / (b->y - a->y);
        }
        for (int i = 1; i < nx; i++) {               /* tiny insertion sort */
            float t = xs[i]; int j = i - 1;
            while (j >= 0 && xs[j] > t) { xs[j + 1] = xs[j]; j--; }
            xs[j + 1] = t;
        }
        for (int i = 0; i + 1 < nx; i += 2) {
            int xa = (int)ceilf(xs[i] - 0.5f), xb = (int)floorf(xs[i + 1] - 0.5f);
            if (xa < 0) xa = 0;
            if (xb >= f->w) xb = f->w - 1;
            uint32_t *row = f->px + (size_t)y * f->w;
            for (int x = xa; x <= xb; x++) row[x] = color;
        }
    }
}

void draw_mesh(Frame *f, const Camera *c, const PMesh *m,
               float x, float y, float z, float yaw, int32_t paint1, int32_t paint2)
{
    float sy = sinf(yaw), cy = cosf(yaw);
    float syw = sinf(c->yaw), cyw = cosf(c->yaw);
    float sp = sinf(c->pitch), cp = cosf(c->pitch);
    float hx = f->w * 0.5f, hy = f->h * 0.5f;

    if (g_len + m->npolys > g_cap) {
        g_cap = (g_len + m->npolys) * 2;
        g_list = realloc(g_list, g_cap * sizeof *g_list);
    }
    /* g_len is the queue for this frame; caller flushes via draw_flush() — but for the
     * single-mesh path we sort and fill straight away below. */
    g_len = 0;

    for (uint32_t pi = 0; pi < m->npolys; pi++) {
        const PmPoly *p = &m->polys[pi];
        if (p->nverts < 3 || p->nverts > MAXV - 2) continue;
        g_polys_in++;

        V3 cv[MAXV], out[MAXV];
        float zsum = 0;
        for (int i = 0; i < p->nverts; i++) {
            const PmVert *s = &m->verts[m->indices[p->first_index + i]];
            /* model -> world (yaw about Y) */
            float wx = s->x * cy + s->z * sy + x - c->x;
            float wy = s->y + y - c->y;
            float wz = -s->x * sy + s->z * cy + z - c->z;
            /* world -> camera: yaw about Y, then pitch about X */
            float rx = wx * cyw - wz * syw;
            float rz = wx * syw + wz * cyw;
            cv[i].x = rx;
            cv[i].y = wy * cp - rz * sp;
            cv[i].z = wy * sp + rz * cp;
            zsum += cv[i].z;
        }

        /* flat shade from the camera-space normal (placeholder for Plane's lighting) */
        V3 e1 = { cv[1].x - cv[0].x, cv[1].y - cv[0].y, cv[1].z - cv[0].z };
        V3 e2 = { cv[2].x - cv[0].x, cv[2].y - cv[0].y, cv[2].z - cv[0].z };
        V3 nrm = { e1.y * e2.z - e1.z * e2.y, e1.z * e2.x - e1.x * e2.z, e1.x * e2.y - e1.y * e2.x };
        float nl = sqrtf(nrm.x * nrm.x + nrm.y * nrm.y + nrm.z * nrm.z);
        float k = 0.75f;
        if (nl > 0) k = 0.55f + 0.45f * fabsf(nrm.y * -0.6f + nrm.z * -0.5f + nrm.x * 0.3f) / nl;

        /* near-plane clip (Sutherland–Hodgman) */
        int n = 0;
        for (int i = 0, j = p->nverts - 1; i < p->nverts; j = i++) {
            const V3 *a = &cv[j], *b = &cv[i];
            bool ain = a->z >= NEAR_Z, bin = b->z >= NEAR_Z;
            if (ain != bin) {
                float t = (NEAR_Z - a->z) / (b->z - a->z);
                out[n++] = (V3){ a->x + t * (b->x - a->x), a->y + t * (b->y - a->y), NEAR_Z };
            }
            if (bin) out[n++] = *b;
        }
        if (n < 3) continue;

        Drawn *d = &g_list[g_len];
        for (int i = 0; i < n; i++) {
            d->v[i].x = hx + out[i].x * f->focal / out[i].z;
            d->v[i].y = hy + out[i].y * f->focal / out[i].z;
        }
        d->n = n;
        d->depth = zsum / p->nverts;

        int r = p->r, g = p->g, b = p->b;
        if (p->paint == PM_PAINT_FIRST && paint1 >= 0) { r = paint1 >> 16 & 255; g = paint1 >> 8 & 255; b = paint1 & 255; }
        if (p->paint == PM_PAINT_SECOND && paint2 >= 0) { r = paint2 >> 16 & 255; g = paint2 >> 8 & 255; b = paint2 & 255; }
        if (p->material == PM_MAT_GLASS) { r = (r + 140) / 2; g = (g + 170) / 2; b = (b + 210) / 2; }
        d->color = p->light == PM_LIGHT_NONE ? shade(r, g, b, 1.0f) : shade(r, g, b, k * 1.15f);
        if (p->light == PM_LIGHT_NONE && p->material == PM_MAT_NORMAL) d->color = shade(r, g, b, k);
        g_len++;
    }

    qsort(g_list, g_len, sizeof *g_list, cmp_depth);
    for (size_t i = 0; i < g_len; i++) {
        fill_poly(f, g_list[i].v, g_list[i].n, g_list[i].color);
        g_polys_drawn++;
    }
}

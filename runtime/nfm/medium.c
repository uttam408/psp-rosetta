#include "medium.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "gen/ntrig_table.h"

int g_polys_in, g_polys_drawn;

#define MAXN 24

/* ---- Java numeric helpers ------------------------------------------------ */

static int jint(double v)            /* (int)double: truncate, NaN->0, saturate */
{
    if (v != v) return 0;
    if (v >= 2147483647.0) return 2147483647;
    if (v <= -2147483648.0) return (int)-2147483648.0;
    return (int)v;
}
static int iabs(int v) { return v < 0 ? -v : v; }
static int clamp255(int v) { return v > 255 ? 255 : v < 0 ? 0 : v; }

float m_sin(int d) { while (d >= 360) d -= 360; while (d < 0) d += 360; return NTSIN[d]; }
float m_cos(int d) { while (d >= 360) d -= 360; while (d < 0) d += 360; return NTCOS[d]; }

/* java.awt.Color.RGBtoHSB / HSBtoRGB, float semantics */
static void rgb2hsb(int r, int g, int b, float *hsb)
{
    int cmax = r > g ? r : g; if (b > cmax) cmax = b;
    int cmin = r < g ? r : g; if (b < cmin) cmin = b;
    float bri = (float)cmax / 255.0f, sat = cmax != 0 ? (float)(cmax - cmin) / (float)cmax : 0.0f, hue;
    if (sat == 0.0f) hue = 0.0f;
    else {
        float rc = (float)(cmax - r) / (float)(cmax - cmin);
        float gc = (float)(cmax - g) / (float)(cmax - cmin);
        float bc = (float)(cmax - b) / (float)(cmax - cmin);
        if (r == cmax) hue = bc - gc;
        else if (g == cmax) hue = 2.0f + rc - bc;
        else hue = 4.0f + gc - rc;
        hue /= 6.0f;
        if (hue < 0.0f) hue += 1.0f;
    }
    hsb[0] = hue; hsb[1] = sat; hsb[2] = bri;
}

static void hsb2rgb(float hue, float sat, float bri, int *R, int *G, int *B)
{
    int r = 0, g = 0, b = 0;
    if (sat == 0.0f) r = g = b = (int)(bri * 255.0f + 0.5f);
    else {
        float h = (hue - floorf(hue)) * 6.0f, f = h - floorf(h);
        float p = bri * (1.0f - sat), q = bri * (1.0f - sat * f), t = bri * (1.0f - (sat * (1.0f - f)));
        float rr, gg, bb;
        switch ((int)h) {
        case 0: rr = bri; gg = t;   bb = p;   break;
        case 1: rr = q;   gg = bri; bb = p;   break;
        case 2: rr = p;   gg = bri; bb = t;   break;
        case 3: rr = p;   gg = q;   bb = bri; break;
        case 4: rr = t;   gg = p;   bb = bri; break;
        default:rr = bri; gg = p;   bb = q;   break;
        }
        r = (int)(rr * 255.0f + 0.5f); g = (int)(gg * 255.0f + 0.5f); b = (int)(bb * 255.0f + 0.5f);
    }
    *R = r; *G = g; *B = b;
}

/* ---- Medium ------------------------------------------------------------- */

void medium_init(Medium *m, Frame *f)
{
    memset(m, 0, sizeof *m);
    m->focus_point = 400; m->ground = 250; m->skyline = -300;
    for (int i = 0; i < 16; i++) m->fade[i] = 3000 + 1500 * i;
    int osky[3] = { 170, 220, 255 }, grnd[3] = { 205, 200, 200 }, cpol[3] = { 215, 210, 210 }, cf[3] = { 255, 220, 220 };
    for (int i = 0; i < 3; i++) {
        m->osky[i] = m->csky[i] = osky[i];
        m->ogrnd[i] = m->cgrnd[i] = m->crgrnd[i] = grnd[i];
        m->cpol[i] = cpol[i]; m->cfade[i] = cf[i];
    }
    m->texture[3] = 50;
    m->fogd = 7;
    m->cx = 400; m->cy = 225; m->cz = 50;
    m->w = 800; m->h = 450;
    m->adv = 500;
    m->frame = f;
    m->scale = f ? (float)f->w / 800.0f : 1.0f;
}

static int snapc(int v, int s) { return clamp255(jint(v + v * ((float)s / 100.0f))); }

void medium_setsnap(Medium *m, int r, int g, int b) { m->snap[0] = r; m->snap[1] = g; m->snap[2] = b; }

void medium_setsky(Medium *m, int r, int g, int b)
{
    m->osky[0] = r; m->osky[1] = g; m->osky[2] = b;
    for (int i = 0; i < 3; i++) m->csky[i] = snapc(m->osky[i], m->snap[i]);
}

static void recalc_rgrnd(Medium *m)
{
    for (int i = 0; i < 3; i++) m->crgrnd[i] = jint((m->cpol[i] * 0.99 + m->cgrnd[i]) / 2.0);
}

void medium_setgrnd(Medium *m, int r, int g, int b)
{
    m->ogrnd[0] = r; m->ogrnd[1] = g; m->ogrnd[2] = b;
    for (int i = 0; i < 3; i++) {
        m->cpol[i] = (m->ogrnd[i] * m->texture[3] + m->texture[i]) / (1 + m->texture[3]);
        m->cpol[i] = clamp255(m->cpol[i] + jint(m->cpol[i] * ((float)m->snap[i] / 100.0f)));
        m->cgrnd[i] = snapc(m->ogrnd[i], m->snap[i]);
    }
    recalc_rgrnd(m);
}

void medium_setpolys(Medium *m, int r, int g, int b)
{
    m->cpol[0] = snapc(r, m->snap[0]); m->cpol[1] = snapc(g, m->snap[1]); m->cpol[2] = snapc(b, m->snap[2]);
    recalc_rgrnd(m);
}

void medium_setfade(Medium *m, int r, int g, int b)
{
    m->cfade[0] = snapc(r, m->snap[0]); m->cfade[1] = snapc(g, m->snap[1]); m->cfade[2] = snapc(b, m->snap[2]);
}

void medium_fadfrom(Medium *m, int n)
{
    if (n > 8000) n = 8000;
    for (int i = 1; i < 17; i++) m->fade[i - 1] = n / 2 * (i + 1);
}

int m_xs(const Medium *m, int x, int cz)
{
    if (cz < m->cz) cz = m->cz;
    return (cz - m->focus_point) * (m->cx - x) / cz + x;
}
int m_ys(const Medium *m, int y, int cz)
{
    if (cz < m->cz) cz = m->cz;
    return (cz - m->focus_point) * (m->cy - y) / cz + y;
}
/* Medium.ys clamps at 10, not cz — used only by the backdrop */
static int mm_ys(const Medium *m, int y, int z)
{
    if (z < 10) z = 10;
    return (z - m->focus_point) * (m->cy - y) / z + y;
}

static void rot(const Medium *m, int *a, int *b, int n, int n2, int ang, int cnt)
{
    (void)m;
    if (!ang) return;
    float s = m_sin(ang), c = m_cos(ang);
    for (int i = 0; i < cnt; i++) {
        int x = a[i], y = b[i];
        a[i] = n  + jint((x - n) * c - (y - n2) * s);
        b[i] = n2 + jint((x - n) * s + (y - n2) * c);
    }
}

/* ---- rasterisation (scaled at fill time) -------------------------------- */

typedef struct { float x, y; } V2;

static void fill_poly(Frame *f, const V2 *v, int n, uint32_t color)
{
    float ymin = v[0].y, ymax = v[0].y;
    for (int i = 1; i < n; i++) { if (v[i].y < ymin) ymin = v[i].y; if (v[i].y > ymax) ymax = v[i].y; }
    int y0 = (int)ceilf(ymin - 0.5f), y1 = (int)floorf(ymax - 0.5f);
    if (y0 < 0) y0 = 0;
    if (y1 >= f->h) y1 = f->h - 1;
    for (int y = y0; y <= y1; y++) {
        float sy = y + 0.5f, xs[MAXN];
        int nx = 0;
        for (int i = 0, j = n - 1; i < n; j = i++) {
            const V2 *a = &v[j], *b = &v[i];
            if ((a->y <= sy) == (b->y <= sy)) continue;
            if (nx < MAXN) xs[nx++] = a->x + (sy - a->y) * (b->x - a->x) / (b->y - a->y);
        }
        for (int i = 1; i < nx; i++) {
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

static void line(Frame *f, int x0, int y0, int x1, int y1, uint32_t c)
{
    int dx = iabs(x1 - x0), dy = -iabs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < f->w && y0 >= 0 && y0 < f->h) f->px[(size_t)y0 * f->w + x0] = c;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}

static void fill_ipoly(const Medium *m, const int *xs, const int *ys, int n, int r, int g, int b)
{
    V2 v[MAXN];
    for (int i = 0; i < n; i++) { v[i].x = xs[i] * m->scale; v[i].y = ys[i] * m->scale; }
    fill_poly(m->frame, v, n, ((uint32_t)clamp255(r) << 16) | ((uint32_t)clamp255(g) << 8) | (uint32_t)clamp255(b));
}

static void outline_ipoly(const Medium *m, const int *xs, const int *ys, int n, uint32_t c)
{
    for (int i = 0, j = n - 1; i < n; j = i++)
        line(m->frame, (int)(xs[j] * m->scale), (int)(ys[j] * m->scale), (int)(xs[i] * m->scale), (int)(ys[i] * m->scale), c);
}

/* ---- backdrop: Medium.d sky + ground fog bands ---------------------------- */

void medium_draw_backdrop(Medium *m)
{
    if (m->zy > 90) m->zy = 90;
    if (m->zy < -90) m->zy = -90;
    if (m->xz > 360) m->xz -= 360;
    if (m->xz < 0) m->xz += 360;
    if (m->y > 0) m->y = 0;
    m->ground = 250 - m->y;
    float sz = m_sin(m->zy), cz = m_cos(m->zy);
    int ax[4], ay[4];

    /* ground bands, near to far */
    int n1 = m->cgrnd[0], n2 = m->cgrnd[1], n3 = m->cgrnd[2];
    int n4 = m->crgrnd[0], n5 = m->crgrnd[1], n6 = m->crgrnd[2];
    int h = m->h;
    for (int i = 0; i < 16; i++) {
        int n7 = m->fade[i], gr = m->ground;
        if (m->zy != 0) {
            gr = m->cy + jint((m->ground - m->cy) * cz - (m->fade[i] - m->cz) * sz);
            n7 = m->cz + jint((m->ground - m->cy) * sz + (m->fade[i] - m->cz) * cz);
        }
        ax[0] = m->iw; ay[0] = mm_ys(m, gr, n7);
        if (ay[0] < m->ih) ay[0] = m->ih;
        if (ay[0] > m->h) ay[0] = m->h;
        ax[1] = m->iw; ay[1] = h; ax[2] = m->w; ay[2] = h; ax[3] = m->w; ay[3] = ay[0];
        h = ay[0];
        if (i > 0) {
            n4 = (n4 * 7 + m->cfade[0]) / 8; n5 = (n5 * 7 + m->cfade[1]) / 8; n6 = (n6 * 7 + m->cfade[2]) / 8;
            if (i < 3) { n1 = (n1 * 7 + m->cfade[0]) / 8; n2 = (n2 * 7 + m->cfade[1]) / 8; n3 = (n3 * 7 + m->cfade[2]) / 8; }
            else { n1 = n4; n2 = n5; n3 = n6; }
        }
        if (ay[0] < m->h && ay[1] > m->ih) fill_ipoly(m, ax, ay, 4, n1, n2, n3);
    }

    /* sky bands, horizon up */
    int s0 = m->csky[0], s1 = m->csky[1], s2 = m->csky[2];
    int k0 = s0, k1 = s1, k2 = s2;
    int ys = mm_ys(m, m->cy + jint((m->skyline - 700 - m->cy) * cz - (7000 - m->cz) * sz),
                      m->cz + jint((m->skyline - 700 - m->cy) * sz + (7000 - m->cz) * cz));
    int ih = m->ih;
    for (int j = 0; j < 16; j++) {
        int n14 = m->fade[j], sl = m->skyline;
        if (m->zy != 0) {
            sl  = m->cy + jint((m->skyline - m->cy) * cz - (m->fade[j] - m->cz) * sz);
            n14 = m->cz + jint((m->skyline - m->cy) * sz + (m->fade[j] - m->cz) * cz);
        }
        ax[0] = m->iw; ay[0] = mm_ys(m, sl, n14);
        if (ay[0] > m->h) ay[0] = m->h;
        if (ay[0] < m->ih) ay[0] = m->ih;
        ax[1] = m->iw; ay[1] = ih; ax[2] = m->w; ay[2] = ih; ax[3] = m->w; ay[3] = ay[0];
        ih = ay[0];
        if (j > 0) { s0 = (s0 * 7 + m->cfade[0]) / 8; s1 = (s1 * 7 + m->cfade[1]) / 8; s2 = (s2 * 7 + m->cfade[2]) / 8; }
        if (ay[1] < ys) { k0 = s0; k1 = s1; k2 = s2; }
        if (ay[0] > m->ih && ay[1] < m->h) fill_ipoly(m, ax, ay, 4, s0, s1, s2);
    }

    /* gap between the sky bands and the ground bands */
    ax[0] = m->iw; ay[0] = ih; ax[1] = m->iw; ay[1] = h; ax[2] = m->w; ay[2] = h; ax[3] = m->w; ay[3] = ih;
    if (ay[0] < m->h && ay[1] > m->ih) {
        float t = (iabs(m->y) - 250.0f) / (float)(m->fade[0] * 2);
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        fill_ipoly(m, ax, ay, 4, jint((s0 * (1.0f - t) + n4 * (1.0f + t)) / 2.0f),
                                 jint((s1 * (1.0f - t) + n5 * (1.0f + t)) / 2.0f),
                                 jint((s2 * (1.0f - t) + n6 * (1.0f + t)) / 2.0f));
    }

    /* upper sky gradient. The decompiled source reads `n11 *= (int)0.991`, which would zero
     * the colour and paint the sky black; that is a Procyon artifact of the compound
     * assignment `int *= double` (really `(int)(n11 * 0.991)`), so it is undone here. */
    for (int k = 1; k < 20; k++) {
        int a = 7000, b = m->skyline - 700 - k * 70;
        if (m->zy != 0 && k != 19) {
            b = m->cy + jint((m->skyline - 700 - k * 70 - m->cy) * cz - (7000 - m->cz) * sz);
            a = m->cz + jint((m->skyline - 700 - k * 70 - m->cy) * sz + (7000 - m->cz) * cz);
        }
        ax[0] = m->iw;
        if (k != 19) {
            ay[0] = mm_ys(m, b, a);
            if (ay[0] > m->h) ay[0] = m->h;
            if (ay[0] < m->ih) ay[0] = m->ih;
        } else ay[0] = m->ih;
        ax[1] = m->iw; ay[1] = ys; ax[2] = m->w; ay[2] = ys; ax[3] = m->w; ay[3] = ay[0];
        ys = ay[0];
        k0 = jint(k0 * 0.991); k1 = jint(k1 * 0.991); k2 = jint(k2 * 0.998);
        if (ay[1] > m->ih && ay[0] < m->h) fill_ipoly(m, ax, ay, 4, k0, k1, k2);
    }
}

/* ---- Instances ------------------------------------------------------------ */

static void calc_deltaf_typ(const PMesh *mesh, const PmPoly *p, float *deltaf, uint8_t *typ, float *projf)
{
    const PmVert *v[3];
    for (int i = 0; i < 3; i++) v[i] = &mesh->verts[mesh->indices[p->first_index + i]];
    int ox[3], oy[3], oz[3];
    for (int i = 0; i < 3; i++) { ox[i] = (int)v[i]->x; oy[i] = (int)v[i]->y; oz[i] = (int)v[i]->z; }
    int ax = iabs(ox[2] - ox[1]), ay = iabs(oy[2] - oy[1]), az = iabs(oz[2] - oz[1]);
    *typ = 0;
    if (ay <= ax && ay <= az) *typ = 2;
    if (ax <= ay && ax <= az) *typ = 1;
    if (az <= ax && az <= ay) *typ = 3;
    float d = 1.0f, pj = 1.0f;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) if (j != i) {
            d  *= (float)(sqrt((double)((ox[j]-ox[i])*(ox[j]-ox[i]) + (oy[j]-oy[i])*(oy[j]-oy[i]) + (oz[j]-oz[i])*(oz[j]-oz[i]))) / 100.0);
            pj *= (float)(sqrt((double)((ox[i]-ox[j])*(ox[i]-ox[j]) + (oz[i]-oz[j])*(oz[i]-oz[j]))) / 100.0);
        }
    *deltaf = d / 3.0f;
    *projf = pj / 3.0f;
}

void inst_init(Inst *o, Medium *m, const PMesh *mesh, int x, int y, int z, int xz, int32_t p1, int32_t p2)
{
    memset(o, 0, sizeof *o);
    o->mesh = mesh; o->x = x; o->y = y; o->z = z; o->xz = xz;
    o->noline = false;
    size_t n = mesh->npolys;
    o->av = calloc(n, sizeof(int)); o->hsb = calloc(n * 3, sizeof(float)); o->col = calloc(n * 3, sizeof(int));
    o->deltaf = calloc(n, sizeof(float)); o->projf = calloc(n, sizeof(float)); o->typ = calloc(n, 1);
    for (size_t i = 0; i < n; i++) {
        const PmPoly *p = &mesh->polys[i];
        int oc[3] = { p->r, p->g, p->b }, *c = &o->col[i * 3];
        if (p->paint == PM_PAINT_FIRST && p1 >= 0) { oc[0] = p1 >> 16 & 255; oc[1] = p1 >> 8 & 255; oc[2] = p1 & 255; }
        if (p->paint == PM_PAINT_SECOND && p2 >= 0) { oc[0] = p2 >> 16 & 255; oc[1] = p2 >> 8 & 255; oc[2] = p2 & 255; }
        for (int k = 0; k < 3; k++) {
            if (p->material == PM_MAT_GLASS)
                c[k] = (m->csky[k] * m->fade[0] * 2 + m->cfade[k] * 3000) / (m->fade[0] * 2 + 3000);
            else if (p->material == PM_MAT_GSHADOW)
                c[k] = (int)(m->crgrnd[k] * 0.925f);
            else
                c[k] = snapc(oc[k], m->snap[k]);
        }
        rgb2hsb(c[0], c[1], c[2], &o->hsb[i * 3]);
        calc_deltaf_typ(mesh, p, &o->deltaf[i], &o->typ[i], &o->projf[i]);
    }
}

void inst_free(Inst *o)
{
    free(o->av); free(o->hsb); free(o->col); free(o->deltaf); free(o->projf); free(o->typ);
    memset(o, 0, sizeof *o);
}

/* Plane.d for the static case (no damage, wheels, chips or flicker) */
static void plane_draw(Medium *m, Inst *o, uint32_t pi, int n, int n2, int n3, int cxz, int cxy, int czy,
                       bool noline, int n6)
{
    const PMesh *mesh = o->mesh;
    const PmPoly *P = &mesh->polys[pi];
    const int N = P->nverts;
    if (N < 3 || N > MAXN) return;
    int ax[MAXN], az[MAXN], ay[MAXN];
    for (int i = 0; i < N; i++) {
        const PmVert *s = &mesh->verts[mesh->indices[P->first_index + i]];
        ax[i] = (int)s->x + n; ay[i] = (int)s->y + n2; az[i] = (int)s->z + n3;
    }
    bool b = noline;
    const int gr0 = P->gr, fs = P->fs, light = P->light;
    const bool solo = P->no_outline != 0;
    const bool road = (mesh->flags & PMF_ROAD) != 0;
    const int disline = mesh->disline ? mesh->disline : 14;
    const int glass = P->material == PM_MAT_GLASS ? 1 : P->material == PM_MAT_GSHADOW ? 2 : 0;
    const bool nocol = o->col[pi*3] == o->col[pi*3+1] && o->col[pi*3+1] == o->col[pi*3+2];
    (void)nocol; (void)light;

    rot(m, ax, ay, n, n2, cxy, N);
    rot(m, ay, az, n2, n3, czy, N);
    rot(m, ax, az, n, n3, cxz, N);
    if (cxy != 0 || czy != 0 || cxz != 0) {
        float pj = 1.0f;
        for (int i = 0; i < 3; i++)
            for (int j = 0; j < 3; j++) if (j != i)
                pj *= (float)(sqrt((double)((ax[i]-ax[j])*(ax[i]-ax[j]) + (az[i]-az[j])*(az[i]-az[j]))) / 100.0);
        o->projf[pi] = pj / 3.0f;
    }
    rot(m, ax, az, m->cx, m->cz, m->xz, N);

    /* b2: polygon faces away from the camera (spy comparison of its flattest edge) */
    bool b2 = false;
    int sx24[MAXN], sy25[MAXN];
    int n40 = 500;
    for (int i = 0; i < N; i++) { sx24[i] = m_xs(m, ax[i], az[i]); sy25[i] = m_ys(m, ay[i], az[i]); }
    int n42 = 0, n43 = 1;
    for (int i = 0; i < N; i++)
        for (int j = i; j < N; j++)
            if (i != j && iabs(sx24[i] - sx24[j]) - iabs(sy25[i] - sy25[j]) < n40) {
                n43 = i; n42 = j; n40 = iabs(sx24[i] - sx24[j]) - iabs(sy25[i] - sy25[j]);
            }
    if (sy25[n42] < sy25[n43]) { int t = n42; n42 = n43; n43 = t; }
#define SPY(i) jint(sqrt((double)((ax[i] - m->cx) * (ax[i] - m->cx) + az[i] * az[i])))
    if (SPY(n42) > SPY(n43)) {
        b2 = true;
        int same = 0;
        for (int i = 0; i < N; i++) {
            if (az[i] < 50 && ay[i] > m->cy) b2 = false;
            else if (ay[i] == ay[0]) same++;
        }
        if (same == N && ay[0] > m->cy) b2 = false;
    }
#undef SPY
    rot(m, ay, az, m->cy, m->cz, m->zy, N);

    int px[MAXN], py[MAXN];
    int c50 = 0, c51 = 0, c52 = 0, c53 = 0, c54 = 0;
    for (int i = 0; i < N; i++) {
        px[i] = m_xs(m, ax[i], az[i]); py[i] = m_ys(m, ay[i], az[i]);
        if (py[i] < m->ih || az[i] < 10) c50++;
        if (py[i] > m->h  || az[i] < 10) c51++;
        if (px[i] < m->iw || az[i] < 10) c52++;
        if (px[i] > m->w  || az[i] < 10) c53++;
        if (az[i] < 10) c54++;
    }
    bool vis = !(c52 == N || c50 == N || c51 == N || c53 == N);
    if (c54 != 0) b = true;
    if (vis && n6 != -1) {
        int d3 = 0, d4 = 0;
        for (int i = 0; i < N; i++)
            for (int j = i; j < N; j++) if (i != j) {
                if (iabs(px[i] - px[j]) > d3) d3 = iabs(px[i] - px[j]);
                if (iabs(py[i] - py[j]) > d4) d4 = iabs(py[i] - py[j]);
            }
        if (d3 == 0 || d4 == 0) vis = false;
        else if (d3 < 3 && d4 < 3 && ((n6 / d3 > 15 && n6 / d4 > 15) || b)) vis = false;
    }
    if (vis) {
        int lastmaf = 1, gr = gr0;
        if (gr < 0 && gr >= -15) gr = 0;
        if (gr0 == -11) gr = -90;
        if (gr0 == -12) gr = -75;
        if (gr0 == -14 || gr0 == -15) gr = -50;
        if (glass == 2) gr = 200;
        if (fs != 0) {
            int a, c;
            if (iabs(py[0] - py[1]) > iabs(py[2] - py[1])) { a = 0; c = 2; }
            else { a = 2; c = 0; lastmaf *= -1; }
            if (py[1] > py[a]) lastmaf *= -1;
            if (px[1] > px[c]) lastmaf *= -1;
            if (fs != 22) {
                lastmaf *= fs;
                if (lastmaf == -1) { gr += 40; lastmaf = -111; }
            }
        }
        int ymx = ay[0], ymn = ay[0], xmx = ax[0], xmn = ax[0], zmx = az[0], zmn = az[0];
        for (int i = 0; i < N; i++) {
            if (ay[i] > ymx) ymx = ay[i]; if (ay[i] < ymn) ymn = ay[i];
            if (ax[i] > xmx) xmx = ax[i]; if (ax[i] < xmn) xmn = ax[i];
            if (az[i] > zmx) zmx = az[i]; if (az[i] < zmn) zmn = az[i];
        }
        int cy = (ymx + ymn) / 2, cx = (xmx + xmn) / 2, czz = (zmx + zmn) / 2;
        o->av[pi] = jint(sqrt((double)((m->cy - cy) * (m->cy - cy) + (m->cx - cx) * (m->cx - cx) + czz * czz + gr * gr * gr)));
        int av = o->av[pi];
        if (m->trk == 0 && (av > m->fade[disline] || av == 0)) vis = false;
        if (lastmaf == -111 && av > 4500 && !road) vis = false;
        if (lastmaf == -111 && av > 1500) b = true;
        if (av > 3000 && m->adv <= 900) b = true;
        if (fs == 22 && av < 11200) m->lastmaf = lastmaf;
        if (gr0 == -13) vis = false;
        if ((gr0 == -14 || gr0 == -15 || gr0 == -12) && (av > 11000 || b2 || lastmaf == -111)) vis = false;
        if (gr0 == -11 && av > 11000) vis = false;
        if (glass == 2 && (m->trk != 0 || av > 6700)) vis = false;
    }
    if (!vis) return;

    int av = o->av[pi];
    float n70 = (float)(o->projf[pi] / o->deltaf[pi] + 0.3);
    if (b && !solo) {
        bool b3 = false;
        if (n70 > 1.0f) { if (n70 >= 1.27) b3 = true; n70 = 1.0f; }
        if (b3) n70 *= 0.89; else n70 *= 0.86;
        if (n70 < 0.37) n70 = 0.37f;
        if (gr0 == -9) n70 = 0.7f;
        if (gr0 == -4) n70 = 0.74f;
        if (gr0 != -7 && m->trk == 0 && b2) n70 = 0.32f;
        if (gr0 == -8 || gr0 == -14 || gr0 == -15) n70 = 1.0f;
        if (gr0 == -11 || gr0 == -12) n70 = n6 == -1 ? 0.76f : 0.6f;
        if (gr0 == -6) n70 = 0.62f;
        if (gr0 == -5) n70 = 0.55f;
    } else {
        if (n70 > 1.0f) n70 = 1.0f;
        if (n70 < 0.6 || b2) n70 = 0.6f;
    }
    int r, g, bl;
    hsb2rgb(o->hsb[pi*3], o->hsb[pi*3+1], o->hsb[pi*3+2] * n70, &r, &g, &bl);
    if (m->trk == 0)
        for (int i = 0; i < 16; i++)
            if (av > m->fade[i]) {
                r  = (r  * m->fogd + m->cfade[0]) / (m->fogd + 1);
                g  = (g  * m->fogd + m->cfade[1]) / (m->fogd + 1);
                bl = (bl * m->fogd + m->cfade[2]) / (m->fogd + 1);
            }
    fill_ipoly(m, px, py, N, r, g, bl);
    g_polys_drawn++;
    if (!b && !solo) outline_ipoly(m, px, py, N, 0x000000);
    (void)glass;
}

static int oxs(const Medium *m, int x, int cz) { return m_xs(m, x, cz); }

/* ContO.d */
void inst_draw(Medium *m, Inst *o)
{
    const PMesh *mesh = o->mesh;
    const int maxR = (int)mesh->max_r;
    const int disline = mesh->disline ? mesh->disline : 14;
    const bool decor = (mesh->flags & PMF_DECOR) != 0;
    const bool shadow = (mesh->flags & PMF_SHADOW) != 0;
    float sxz = m_sin(m->xz), cxz_ = m_cos(m->xz);
    float szy = m_sin(m->zy), czy_ = m_cos(m->zy);

    o->dist = 0;
    int n  = m->cx + jint((o->x - m->x - m->cx) * cxz_ - (o->z - m->z - m->cz) * sxz);
    int n2 = m->cz + jint((o->x - m->x - m->cx) * sxz + (o->z - m->z - m->cz) * cxz_);
    int n3 = m->cz + jint((o->y - m->y - m->cy) * szy + (n2 - m->cz) * czy_);
    int n4 = oxs(m, n + maxR, n3) - oxs(m, n - maxR, n3);
    if (oxs(m, n + maxR * 2, n3) > m->iw && oxs(m, n - maxR * 2, n3) < m->w && n3 > -maxR &&
        (n3 < m->fade[disline] + maxR || m->trk != 0) && (n4 > mesh->disp || m->trk != 0) && !(decor && false)) {
        int n8 = m->cy + jint((o->y - m->y - m->cy) * czy_ - (n2 - m->cz) * szy);
        if (m_ys(m, n8 + maxR, n3) > m->ih && m_ys(m, n8 - maxR, n3) < m->h) {
            const uint32_t np = mesh->npolys;
            int *rank = calloc(np, sizeof(int)), *order = calloc(np, sizeof(int));
            for (uint32_t i = 0; i < np; i++) {
                for (uint32_t j = i + 1; j < np; j++) {
                    if (o->av[i] != o->av[j]) { if (o->av[i] < o->av[j]) rank[i]++; else rank[j]++; }
                    else if (i > j) rank[i]++; else rank[j]++;
                }
                order[rank[i]] = (int)i;
            }
            for (uint32_t i = 0; i < np; i++) {
                g_polys_in++;
                plane_draw(m, o, (uint32_t)order[i], o->x - m->x, o->y - m->y, o->z - m->z, o->xz, o->xy, o->zy,
                           o->noline || (mesh->flags & (PMF_STONECOLD | PMF_NEWSTONE)) != 0, n4);
            }
            free(rank); free(order);
            (void)shadow;
            double d = sqrt((double)((m->x + m->cx - o->x) * (m->x + m->cx - o->x) + (m->z - o->z) * (m->z - o->z) +
                                     (m->y + m->cy - o->y) * (m->y + m->cy - o->y)));
            o->dist = jint(sqrt((double)jint(d)) * (mesh->grounded_pct / 100.0f));
        }
    }
}

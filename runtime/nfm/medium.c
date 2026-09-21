#include "medium.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "gen/ntrig_table.h"

void (*g_nfm_trace)(const char *, int, int);
int g_polys_in, g_polys_drawn;
#ifdef NFM_STATS
int g_st[8];
#endif
#ifdef NFM_PROF
unsigned long long (*g_prof_now)(void);
unsigned long long g_prof[PROF_N];
#endif

#define MAXN 64          /* largest polygon in the game is 28 verts (stage lettering); warn beyond this */

/* ---- Java numeric helpers ------------------------------------------------ */

static int jint_d(double v)          /* (int)double: truncate, NaN->0, saturate */
{
    if (v != v) return 0;
    if (v >= 2147483647.0) return 2147483647;
    if (v <= -2147483648.0) return (int)-2147483648.0;
    return (int)v;
}
/* same for float arguments: float->double is exact, so results are identical, but this
 * avoids soft-float double compares on the PSP (hot: per-vertex rotation) */
static int jint_f(float v)
{
    if (v != v) return 0;
    if (v >= 2147483648.0f) return 2147483647;
    if (v <= -2147483648.0f) return (int)-2147483648.0;
    return (int)v;
}
#define jint(x) _Generic((x), float: jint_f, default: jint_d)(x)
static int iabs(int v) { return v < 0 ? -v : v; }

/* jint(sqrt((double)n)) without soft-float doubles (the PSP FPU is single precision):
 * exact floor(sqrt(n)); negative n is NaN in Java, which jint maps to 0 */
static int isqrt_n(int n)
{
    if (n <= 0) return 0;
    int r = (int)sqrtf((float)n);
    while ((long long)r * r > n) r--;
    while ((long long)(r + 1) * (r + 1) <= n) r++;
    return r;
}
/* (float)(sqrt((double)n) / 100.0) in single precision (may differ from Java in the last ulp) */
static float sqrt100f(int n) { return sqrtf((float)n) / 100.0f; }
/* frame pixel packing: 0x00RRGGBB, or PSP-native ABGR8888 (NFM_ABGR) so the blit is a plain copy */
#ifdef NFM_ABGR
#define PACK(r, g, b) (((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))
#else
#define PACK(r, g, b) (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))
#endif
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

/* hsb2rgb with the bri-independent terms precomputed (bit-identical: same float ops, same order) */
static void hsb_prep(const float *hsb, float *pqt, uint8_t *sec)
{
    float sat = hsb[1];
    if (sat == 0.0f) { *sec = 255; return; }
    float h = (hsb[0] - floorf(hsb[0])) * 6.0f, f = h - floorf(h);
    pqt[0] = 1.0f - sat; pqt[1] = 1.0f - sat * f; pqt[2] = 1.0f - (sat * (1.0f - f));
    int sc = (int)h; if (sc < 0 || sc > 5) sc = 5;
    *sec = (uint8_t)sc;
}

static void hsb2rgb_pre(float bri, const float *pqt, int sec, int *R, int *G, int *B)
{
    if (sec == 255) { *R = *G = *B = (int)(bri * 255.0f + 0.5f); return; }
    float p = bri * pqt[0], q = bri * pqt[1], t = bri * pqt[2], rr, gg, bb;
    switch (sec) {
    case 0: rr = bri; gg = t;   bb = p;   break;
    case 1: rr = q;   gg = bri; bb = p;   break;
    case 2: rr = p;   gg = bri; bb = t;   break;
    case 3: rr = p;   gg = q;   bb = bri; break;
    case 4: rr = t;   gg = p;   bb = bri; break;
    default:rr = bri; gg = p;   bb = q;   break;
    }
    *R = (int)(rr * 255.0f + 0.5f); *G = (int)(gg * 255.0f + 0.5f); *B = (int)(bb * 255.0f + 0.5f);
}

/* ---- Medium ------------------------------------------------------------- */

void medium_init(Medium *m, Frame *f)
{
    memset(m, 0, sizeof *m);
    m->focus_point = 400; m->ground = 250; m->skyline = -300;
    m->far_pct = 100;
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
        /* inputs are bounded ints and c/s are finite table values, so the result is always finite and
         * inside int range: a plain truncating cast equals Java's (int)double and skips jint's guards */
        a[i] = n  + (int)((x - n) * c - (y - n2) * s);
        b[i] = n2 + (int)((x - n) * s + (y - n2) * c);
    }
}

/* ---- rasterisation (scaled at fill time) -------------------------------- */

typedef struct { float x, y; } V2;

static void fill_poly(Frame *f, const V2 *v, int n, uint32_t color)
{
#ifdef NFM_NOFILL
    return;
#endif
    float ymin = v[0].y, ymax = v[0].y;
    for (int i = 1; i < n; i++) { if (v[i].y < ymin) ymin = v[i].y; if (v[i].y > ymax) ymax = v[i].y; }
    int y0 = (int)ceilf(ymin - 0.25f), y1 = (int)floorf(ymax - 0.25f);
    if (y0 < 0) y0 = 0;
    if (y1 >= f->h) y1 = f->h - 1;
    /* edge table built once: a scanline sample sy crosses edge (a,b) iff min(ay,by) <= sy < max(ay,by)
     * (same as the (a.y <= sy) != (b.y <= sy) test); slope = dx/dy replaces a division per row */
    float ey0[MAXN], ey1[MAXN], ex[MAXN], eay[MAXN], esl[MAXN];
    int ne = 0;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const V2 *a = &v[j], *b = &v[i];
        if (a->y == b->y) continue;
        ey0[ne] = a->y < b->y ? a->y : b->y;
        ey1[ne] = a->y < b->y ? b->y : a->y;
        ex[ne] = a->x; eay[ne] = a->y; esl[ne] = (b->x - a->x) / (b->y - a->y);
        ne++;
    }
    for (int y = y0; y <= y1; y++) {
        float sy = y + 0.25f, xs[MAXN];
        int nx = 0;
        for (int k = 0; k < ne; k++)
            if (ey0[k] <= sy && sy < ey1[k] && nx < MAXN) xs[nx++] = ex[k] + (sy - eay[k]) * esl[k];
        for (int i = 1; i < nx; i++) {
            float t = xs[i]; int j = i - 1;
            while (j >= 0 && xs[j] > t) { xs[j + 1] = xs[j]; j--; }
            xs[j + 1] = t;
        }
        uint32_t *row = f->px + (size_t)y * f->w;
        for (int i = 0; i + 1 < nx; i += 2) {
            int xa = (int)ceilf(xs[i] - 0.25f), xb = (int)ceilf(xs[i + 1] - 0.25f) - 1;
            if (xa < 0) xa = 0;
            if (xb >= f->w) xb = f->w - 1;
            uint32_t *p = row + xa, *end = row + xb + 1;
            while (end - p >= 4) { p[0] = color; p[1] = color; p[2] = color; p[3] = color; p += 4; }
            while (p < end) *p++ = color;
        }
    }
}

static void line(Frame *f, int x0, int y0, int x1, int y1, uint32_t c)
{
#ifdef NFM_NOFILL
    return;
#endif
    int dx = iabs(x1 - x0), dy = -iabs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < f->w && y0 >= 0 && y0 < f->h) f->px[(size_t)y0 * f->w + x0] = c;
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * e;
        if (e2 >= dy) { e += dy; x0 += sx; }
        if (e2 <= dx) { e += dx; y0 += sy; }
    }
}

#ifdef NFM_GEFILL
int g_nfm_gefill = 1;   /* 0 = CPU rasteriser into Frame.px (launch-time fallback) */
#endif

static void fill_ipoly(const Medium *m, const int *xs, const int *ys, int n, int r, int g, int b)
{
#ifdef NFM_GEFILL
    if (g_nfm_gefill) {
        float xy[MAXN * 2];
        for (int i = 0; i < n; i++) { xy[2 * i] = xs[i] * m->scale; xy[2 * i + 1] = ys[i] * m->scale; }
        nfm_ge_poly(xy, n, PACK(clamp255(r), clamp255(g), clamp255(b)));
        return;
    }
#endif
    V2 v[MAXN];
    for (int i = 0; i < n; i++) { v[i].x = xs[i] * m->scale; v[i].y = ys[i] * m->scale; }
    fill_poly(m->frame, v, n, PACK(clamp255(r), clamp255(g), clamp255(b)));
}

static void outline_ipoly(const Medium *m, const int *xs, const int *ys, int n, uint32_t c)
{
#ifdef NFM_GEFILL
    if (g_nfm_gefill) {
        float xy[MAXN * 2];
        for (int i = 0; i < n; i++) { xy[2 * i] = xs[i] * m->scale; xy[2 * i + 1] = ys[i] * m->scale; }
        nfm_ge_outline(xy, n, c);
        return;
    }
#endif
    for (int i = 0, j = n - 1; i < n; j = i++)
        line(m->frame, (int)(xs[j] * m->scale), (int)(ys[j] * m->scale), (int)(xs[i] * m->scale), (int)(ys[i] * m->scale), c);
}

/* ---- backdrop: Medium.d sky + ground fog bands ---------------------------- */

void medium_draw_backdrop(Medium *m)
{
    NFM_TRACE("backdrop", 0, 0);
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
            d  *= sqrt100f((ox[j]-ox[i])*(ox[j]-ox[i]) + (oy[j]-oy[i])*(oy[j]-oy[i]) + (oz[j]-oz[i])*(oz[j]-oz[i]));
            pj *= sqrt100f((ox[i]-ox[j])*(ox[i]-ox[j]) + (oz[i]-oz[j])*(oz[i]-oz[j]));
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
    o->av = calloc(n, sizeof(int)); o->order = malloc((n ? n : 1) * sizeof(int));
    o->hpqt = calloc(n * 3 + 1, sizeof(float)); o->hsec = calloc(n + 1, 1); o->n70ok = calloc(n + 1, 1); o->n70c = calloc(n + 1, sizeof(float));
    for (size_t i = 0; i < n; i++) o->order[i] = (int)i;
    o->hsb = calloc(n * 3, sizeof(float)); o->col = calloc(n * 3, sizeof(int));
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
            else if (p->material == PM_MAT_RAW)
                c[k] = oc[k];
            else
                c[k] = snapc(oc[k], m->snap[k]);
        }
        rgb2hsb(c[0], c[1], c[2], &o->hsb[i * 3]);
        if (p->material == PM_MAT_RAW && m->trk != 2) {
            o->hsb[i * 3 + 1] += 0.05f;
            if (o->hsb[i * 3 + 1] > 1.0f) o->hsb[i * 3 + 1] = 1.0f;
        }
        hsb_prep(&o->hsb[i * 3], &o->hpqt[i * 3], &o->hsec[i]);
        calc_deltaf_typ(mesh, p, &o->deltaf[i], &o->typ[i], &o->projf[i]);
    }
}

void inst_free(Inst *o)
{
    free(o->av); free(o->order); free(o->hpqt); free(o->hsec); free(o->n70ok); free(o->n70c); free(o->hsb); free(o->col); free(o->deltaf); free(o->projf); free(o->typ);
    memset(o, 0, sizeof *o);
}

/* Plane.d for the static case (no damage, wheels, chips or flicker) */
/* b2: polygon faces away from the camera (spy comparison of its flattest edge); ay/az are pre-pitch */
static bool facing_away(const Medium *m, const int *ax, const int *ay, const int *az, int N)
{
    bool b2 = false;
    int sx24[MAXN], sy25[MAXN];
    int n40 = 500;
    NFM_TRACE("fa proj", N, 0);
    for (int i = 0; i < N; i++) { sx24[i] = m_xs(m, ax[i], az[i]); sy25[i] = m_ys(m, ay[i], az[i]); }
    int n42 = 0, n43 = 1;
    for (int i = 0; i < N; i++)
        for (int j = i; j < N; j++)
            if (i != j && iabs(sx24[i] - sx24[j]) - iabs(sy25[i] - sy25[j]) < n40) {
                n43 = i; n42 = j; n40 = iabs(sx24[i] - sx24[j]) - iabs(sy25[i] - sy25[j]);
            }
    NFM_TRACE("fa pick", n42, n43);
    if (sy25[n42] < sy25[n43]) { int t = n42; n42 = n43; n43 = t; }
#define SPY(i) isqrt_n((ax[i] - m->cx) * (ax[i] - m->cx) + az[i] * az[i])
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
    return b2;
}

/* (A,B) <- rot about (n,n2) by ang, applied to affine rows (3 coeffs + constant): exactly the rot() formula
 * a' = n + (a-n)c - (b-n2)s ; b' = n2 + (a-n)s + (b-n2)c, but on whole rows instead of one vertex */
static void rot_rows(float *A, float *B, float n, float n2, int ang)
{
    if (!ang) return;
    float s = m_sin(ang), c = m_cos(ang), a[4], b[4];
    for (int k = 0; k < 4; k++) { a[k] = A[k]; b[k] = B[k]; }
    for (int k = 0; k < 3; k++) { A[k] = a[k] * c - b[k] * s; B[k] = a[k] * s + b[k] * c; }
    A[3] = n  + (a[3] - n) * c - (b[3] - n2) * s;
    B[3] = n2 + (a[3] - n) * s + (b[3] - n2) * c;
}

/* build the per-object float transform rows used by plane_draw when m->fastxf is set */
static void xf_build(Medium *m, int n, int n2, int n3, int cxz, int cxy, int czy)
{
    float X[4] = { 1, 0, 0, (float)n }, Y[4] = { 0, 1, 0, (float)n2 }, Z[4] = { 0, 0, 1, (float)n3 };
    rot_rows(X, Y, (float)n, (float)n2, cxy);
    rot_rows(Y, Z, (float)n2, (float)n3, czy);
    rot_rows(X, Z, (float)n, (float)n3, cxz);
    memcpy(m->xf.Xo, X, sizeof X); memcpy(m->xf.Zo, Z, sizeof Z);
    rot_rows(X, Z, (float)m->cx, (float)m->cz, m->xz);
    memcpy(m->xf.X, X, sizeof X); memcpy(m->xf.Y1, Y, sizeof Y); memcpy(m->xf.Z1, Z, sizeof Z);
    rot_rows(Y, Z, (float)m->cy, (float)m->cz, m->zy);
    memcpy(m->xf.Y2, Y, sizeof Y); memcpy(m->xf.Z2, Z, sizeof Z);
}

static inline float dot4(const float *r, float x, float y, float z) { return r[0] * x + r[1] * y + r[2] * z + r[3]; }

/* rebuild the fog lookup when the fog colour/density changed */
static void fog_table_sync(Medium *m)
{
    if (m->fogtab_ok && m->fogtab_sig[0] == m->fogd && m->fogtab_sig[1] == m->cfade[0] &&
        m->fogtab_sig[2] == m->cfade[1] && m->fogtab_sig[3] == m->cfade[2]) return;
    for (int c = 0; c < 3; c++)
        for (int v = 0; v < 256; v++) {
            int x = v;
            m->fogtab[c][0][v] = (uint8_t)x;
            for (int k = 1; k < 17; k++) {
                x = (x * m->fogd + m->cfade[c]) / (m->fogd + 1);
                m->fogtab[c][k][v] = (uint8_t)x;
            }
        }
    m->fogtab_sig[0] = m->fogd; m->fogtab_sig[1] = m->cfade[0]; m->fogtab_sig[2] = m->cfade[1]; m->fogtab_sig[3] = m->cfade[2];
    m->fogtab_ok = true;
}

static void plane_draw(Medium *m, Inst *o, uint32_t pi, int n, int n2, int n3, int cxz, int cxy, int czy,
                       bool noline, int n6)
{
    const PMesh *mesh = o->mesh;
    const PmPoly *P = &mesh->polys[pi];
    const PmPolyX *X = mesh->px ? &mesh->px[pi] : NULL;
    const bool wheel = X && X->wheel;
    int N = P->nverts;
    if (wheel && X->master == 1 && !m->crs && o->av[pi] > 1500) N = 12;   /* distant wheel: 12-gon */
    if (N > MAXN) { fprintf(stderr, "plane: %d-vertex polygon exceeds MAXN\n", N); return; }
    if (N < 3) return;
    PROF_T(t_rot);
    int ax[MAXN], az[MAXN], ay[MAXN];
    int pay[MAXN], paz[MAXN];
    const bool rotated = cxy != 0 || czy != 0 || cxz != 0;
    const PmVert *pv[MAXN];
    for (int i = 0; i < N; i++) pv[i] = &mesh->verts[mesh->indices[P->first_index + i]];
    bool b = noline;
    const int gr0 = P->gr, fs = P->fs, light = P->light;
    const bool solo = P->no_outline != 0;
    const bool road = (mesh->flags & PMF_ROAD) != 0;
    const int disline = wheel ? X->disline : mesh->disline ? mesh->disline : 14;
    const int glass = P->material == PM_MAT_GLASS ? 1 : P->material == PM_MAT_GSHADOW ? 2 : 0;
    const bool nocol = o->col[pi*3] == o->col[pi*3+1] && o->col[pi*3+1] == o->col[pi*3+2];
    (void)nocol; (void)light;

    if (m->fastxf && !wheel) {
        for (int i = 0; i < N; i++) {
            float x = pv[i]->x, y = pv[i]->y, z = pv[i]->z;
            ax[i] = (int)dot4(m->xf.X, x, y, z);
            pay[i] = (int)dot4(m->xf.Y2, x, y, z);
            paz[i] = (int)dot4(m->xf.Z2, x, y, z);
        }
        if (rotated) {
            int ox[3], oz[3];
            for (int i = 0; i < 3; i++) {
                float x = pv[i]->x, y = pv[i]->y, z = pv[i]->z;
                ox[i] = (int)dot4(m->xf.Xo, x, y, z); oz[i] = (int)dot4(m->xf.Zo, x, y, z);
            }
            float pj = 1.0f;
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++) if (j != i)
                    pj *= sqrt100f((ox[i]-ox[j])*(ox[i]-ox[j]) + (oz[i]-oz[j])*(oz[i]-oz[j]));
            o->projf[pi] = pj / 3.0f;
            o->n70ok[pi] = 0;
        }
    } else {
        for (int i = 0; i < N; i++) { ax[i] = (int)pv[i]->x + n; ay[i] = (int)pv[i]->y + n2; az[i] = (int)pv[i]->z + n3; }
        if (wheel) {
            if (X->wz != 0) rot(m, ay, az, X->wy + n2, X->wz + n3, o->wzy, N);
            if (X->wx != 0) rot(m, ax, az, X->wx + n, X->wz + n3, o->wxz, N);
        }
        rot(m, ax, ay, n, n2, cxy, N);
        rot(m, ay, az, n2, n3, czy, N);
        rot(m, ax, az, n, n3, cxz, N);
        if (rotated) {
            float pj = 1.0f;
            for (int i = 0; i < 3; i++)
                for (int j = 0; j < 3; j++) if (j != i)
                    pj *= sqrt100f((ax[i]-ax[j])*(ax[i]-ax[j]) + (az[i]-az[j])*(az[i]-az[j]));
            o->projf[pi] = pj / 3.0f;
            o->n70ok[pi] = 0;
        }
        rot(m, ax, az, m->cx, m->cz, m->xz, N);
        /* screen-visibility first, on pitch-rotated copies: behind-camera / off-screen polygons leave
         * here before the (expensive) facing test.  Java updates Plane.av only after this point too. */
        memcpy(pay, ay, N * sizeof(int)); memcpy(paz, az, N * sizeof(int));
        rot(m, pay, paz, m->cy, m->cz, m->zy, N);
    }
    PROF_ADD(PROF_ROT, t_rot);
    PROF_T(t_prj);
    int px[MAXN], py[MAXN];
    int c50 = 0, c51 = 0, c52 = 0, c53 = 0, c54 = 0;
    for (int i = 0; i < N; i++) {
        px[i] = m_xs(m, ax[i], paz[i]); py[i] = m_ys(m, pay[i], paz[i]);
        if (py[i] < m->ih || paz[i] < 10) c50++;
        if (py[i] > m->h  || paz[i] < 10) c51++;
        if (px[i] < m->iw || paz[i] < 10) c52++;
        if (px[i] > m->w  || paz[i] < 10) c53++;
        if (paz[i] < 10) c54++;
    }
    PROF_ADD(PROF_PROJ, t_prj);
    if (c52 == N || c50 == N || c51 == N || c53 == N) {
#ifdef NFM_STATS
        g_st[c54 == N ? 0 : 1]++;
#endif
        return;
    }
    bool vis = true;

    bool b2 = false;
    int bay[MAXN], baz[MAXN];
    if (m->fastxf && !wheel) {
        for (int i = 0; i < N; i++) {
            float x = pv[i]->x, y = pv[i]->y, z = pv[i]->z;
            bay[i] = (int)dot4(m->xf.Y1, x, y, z); baz[i] = (int)dot4(m->xf.Z1, x, y, z);
        }
    } else { memcpy(bay, ay, N * sizeof(int)); memcpy(baz, az, N * sizeof(int)); }
    memcpy(ay, pay, N * sizeof(int)); memcpy(az, paz, N * sizeof(int));
    if (c54 != 0) b = true;
    if (vis && n6 != -1) {
        int d3 = 0, d4 = 0;
        for (int i = 0; i < N; i++)
            for (int j = i; j < N; j++) if (i != j) {
                if (iabs(px[i] - px[j]) > d3) d3 = iabs(px[i] - px[j]);
                if (iabs(py[i] - py[j]) > d4) d4 = iabs(py[i] - py[j]);
            }
#ifdef NFM_STATS
        g_st[7]++;
#endif
        if (d3 == 0 || d4 == 0) vis = false;
        else if (m->tinyfar > 0 && c54 == 0 && !o->always) {
            int zmn2 = paz[0];
            for (int i = 1; i < N; i++) if (paz[i] < zmn2) zmn2 = paz[i];
            const int D = (int)((int64_t)m->fade[disline] * m->far_pct / 100);
            if (zmn2 > D) zmn2 = D;
            const int T = D > 0 ? m->tiny + (int)((int64_t)(m->tinyfar - m->tiny) * zmn2 / D) : m->tiny;
            if (d3 <= T && d4 <= T) vis = false;
        }
        else if (d3 < 3 && d4 < 3 && ((n6 / d3 > 15 && n6 / d4 > 15) || b) && (!m->lightson || light == 0)) vis = false;
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
        o->av[pi] = isqrt_n((m->cy - cy) * (m->cy - cy) + (m->cx - cx) * (m->cx - cx) + czz * czz + gr * gr * gr);
        int av = o->av[pi];
        if (m->trk == 0 && ((av > (int)((int64_t)m->fade[disline] * m->far_pct / 100) && !o->always) || av == 0)) vis = false;
        if (wheel && X->master == 2 && av > 1500 && !m->crs) vis = false;
        if (lastmaf == -111 && av > 4500 && !road) vis = false;
        if (lastmaf == -111 && av > 1500) b = true;
        if (av > 3000 && m->adv <= 900) b = true;
        if (fs == 22 && av < 11200) m->lastmaf = lastmaf;
        if (gr0 == -13) vis = false;
        if (vis && (gr0 == -14 || gr0 == -15 || gr0 == -12)) b2 = facing_away(m, ax, bay, baz, N);
        if ((gr0 == -14 || gr0 == -15 || gr0 == -12) && (av > 11000 || b2 || lastmaf == -111)) vis = false;
        if (gr0 == -11 && av > 11000) vis = false;
        if (glass == 2 && (m->trk != 0 || av > 6700)) vis = false;
    }
#ifdef NFM_STATS
    g_st[vis ? 6 : 2]++;
#endif
    if (!vis) return;
    NFM_TRACE("shade", (int)pi, 0);
    PROF_T(t_sh);

    int av = o->av[pi];
    NFM_TRACE("b2 begin", (int)pi, N);
    if (!(gr0 == -14 || gr0 == -15 || gr0 == -12)) b2 = facing_away(m, ax, bay, baz, N);
    NFM_TRACE("b2 done", (int)pi, (int)b2);
    /* n70 = projf/deltaf + 0.3.  deltaf == 0 gives NaN (0/0) or +-inf; Java carries NaN through (all
     * comparisons false, HSBtoRGB(NaN) -> black) but the PSP FPU faults on NaN compares/conversions,
     * so NaN is tracked in `nan70` and never touches a float compare. */
    float n70;
    bool nan70 = false;
    if (!rotated && o->n70ok[pi]) { n70 = o->n70c[pi]; nan70 = o->n70ok[pi] == 2; }
    else {
        if (o->deltaf[pi] == 0.0f) {
            if (o->projf[pi] == 0.0f) { n70 = 0.0f; nan70 = true; }
            else n70 = o->projf[pi] > 0.0f ? 1e30f : -1e30f;
        } else n70 = o->projf[pi] / o->deltaf[pi] + 0.3f;
        if (!rotated) { o->n70c[pi] = n70; o->n70ok[pi] = nan70 ? 2 : 1; }
    }
#define SET70(v) do { n70 = (v); nan70 = false; } while (0)
    if (b && !solo) {
        if (!nan70) {
            bool b3 = false;
            if (n70 > 1.0f) { if (n70 >= 1.27f) b3 = true; n70 = 1.0f; }
            if (b3) n70 *= 0.89f; else n70 *= 0.86f;
            if (n70 < 0.37f) n70 = 0.37f;
        }
        if (gr0 == -9) SET70(0.7f);
        if (gr0 == -4) SET70(0.74f);
        if (gr0 != -7 && m->trk == 0 && b2) SET70(0.32f);
        if (gr0 == -8 || gr0 == -14 || gr0 == -15) SET70(1.0f);
        if (gr0 == -11 || gr0 == -12) SET70(n6 == -1 ? 0.76f : 0.6f);
        if (gr0 == -6) SET70(0.62f);
        if (gr0 == -5) SET70(0.55f);
    } else {
        if (!nan70 && n70 > 1.0f) n70 = 1.0f;
        if ((!nan70 && n70 < 0.6f) || b2) SET70(0.6f);
    }
#undef SET70
    int r, g, bl;
    if (nan70) r = g = bl = 0;
    else hsb2rgb_pre(o->hsb[pi*3+2] * n70, &o->hpqt[pi*3], o->hsec[pi], &r, &g, &bl);
    if (m->trk == 0) {
        int k = 0;
        for (int i = 0; i < 16; i++) if (av > m->fade[i]) k++;
        if (k) {
            if (r >= 0 && r < 256 && g >= 0 && g < 256 && bl >= 0 && bl < 256) {
                fog_table_sync(m);
                r = m->fogtab[0][k][r]; g = m->fogtab[1][k][g]; bl = m->fogtab[2][k][bl];
            } else {
                for (int i = 0; i < k; i++) {
                    r  = (r  * m->fogd + m->cfade[0]) / (m->fogd + 1);
                    g  = (g  * m->fogd + m->cfade[1]) / (m->fogd + 1);
                    bl = (bl * m->fogd + m->cfade[2]) / (m->fogd + 1);
                }
            }
        }
    }
    NFM_TRACE("fog done", av, m->fogd);
    NFM_TRACE("fill", (int)pi, N);
    PROF_ADD(PROF_SHADE, t_sh);
    PROF_T(t_fl);
    fill_ipoly(m, px, py, N, r, g, bl);
    g_polys_drawn++;
    if (!b) {
        if (!solo) {
            uint32_t oc = 0;                /* lit stages: outline is half the poly's own colour */
            if (m->lightson && light != 0)
                oc = PACK(clamp255(P->r / 2), clamp255(P->g / 2), clamp255(P->b / 2));
            outline_ipoly(m, px, py, N, oc);
        }
    } else if (road && av <= 3000 && m->trk == 0 && m->fade[0] > 4000) {
        /* near road polys are outlined a touch darker than their (fogged) fill */
        r -= 10; if (r < 0) r = 0;
        g -= 10; if (g < 0) g = 0;
        bl -= 10; if (bl < 0) bl = 0;
        outline_ipoly(m, px, py, N, PACK(r, g, bl));
    }
    PROF_ADD(PROF_FILL, t_fl);
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
        (n3 < (int)((int64_t)m->fade[disline] * m->far_pct / 100) + maxR || m->trk != 0 || o->always) && (n4 > mesh->disp || m->trk != 0 || o->always) && !(decor && false)) {
        int n8 = m->cy + jint((o->y - m->y - m->cy) * czy_ - (n2 - m->cz) * szy);
        if (m_ys(m, n8 + maxR, n3) > m->ih && m_ys(m, n8 - maxR, n3) < m->h) {
            const uint32_t np = mesh->npolys;
            PROF_T(t_so);
            /* painter order: larger av first, ties by lower index.  av barely changes between frames, so
             * insertion-sorting last frame's order is ~O(np) and gives exactly the O(np^2) rank result. */
            int *order = o->order;
            const int *av = o->av;
            for (uint32_t i = 1; i < np; i++) {
                const int key = order[i], kav = av[key];
                int j = (int)i - 1;
                while (j >= 0 && (av[order[j]] < kav || (av[order[j]] == kav && order[j] > key))) { order[j + 1] = order[j]; j--; }
                order[j + 1] = key;
            }
            PROF_ADD(PROF_SORT, t_so);
            if (m->fastxf) xf_build(m, o->x - m->x, o->y - m->y, o->z - m->z, o->xz, o->xy, o->zy);
            PROF_T(t_pl);
            for (uint32_t i = 0; i < np; i++) {
                g_polys_in++;
                NFM_TRACE("poly", (int)order[i], (int)np);
                plane_draw(m, o, (uint32_t)order[i], o->x - m->x, o->y - m->y, o->z - m->z, o->xz, o->xy, o->zy,
                           o->noline || (mesh->flags & (PMF_STONECOLD | PMF_NEWSTONE)) != 0, n4);
            }
            PROF_ADD(PROF_PLANE, t_pl);
            (void)shadow;
            int dsq = (m->x + m->cx - o->x) * (m->x + m->cx - o->x) + (m->z - o->z) * (m->z - o->z) +
                      (m->y + m->cy - o->y) * (m->y + m->cy - o->y);
            o->dist = jint(sqrtf((float)isqrt_n(dsq)) * (mesh->grounded_pct / 100.0f));
        }
    }
}

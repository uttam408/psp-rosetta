/* ge_fill.c — GE rasteriser backend for medium.c (-DNFM_GEFILL).
 * medium.c still does all transform / sort / shade / fog on the CPU; only the pixel fill moves here.  Every
 * polygon becomes one flat-coloured triangle fan (convex) or triangle list (ear-clipped concave), plus an
 * optional closed line strip for its outline, drawn in submission order so the painter's algorithm holds.
 * Coordinates are rounded to int16 pixels, so edges differ from the CPU rasteriser's Java fill rule by <=1 px. */
#include <pspkernel.h>
#include <pspgu.h>
#include <psputils.h>
#include <math.h>
#include <stdint.h>
#include "../../nfm/medium.h"

typedef struct { uint32_t c; int16_t x, y, z, pad; } GV;   /* GU_COLOR_8888 | GU_VERTEX_16BIT, 12 bytes */
#define VFMT (GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D)
#define NV 24000
#define MAXV 96
#define LIST_WORDS 32768   /* must match the display list array in main_nfm_psp.c */
#define CMD_WORDS 6         /* worst case per DrawArray: vtype + base + vaddr + prim (+ slack) */
#define LIST_CAP (LIST_WORDS - 256)
/* The GE's 2D path only has 12-bit screen coordinates after the offset: with sceGuOffset(2048-240, 2048-136) a
 * pixel x is valid in -1808..2287 and y in -1912..2183.  Anything beyond wraps and smears horizontal streaks
 * across the frame, so polygons reaching past +-GE_LIM are clipped to the frame first. */
#define GE_LIM 1500.f

#ifdef NFM_GESKIP2   /* profiling: do all the prep, skip the actual GE draw calls */
#define DRAW(prim, n, base) ((void)0)
#else
#define DRAW(prim, n, base) sceGuDrawArray(prim, VFMT, n, NULL, base)
#endif
static GV __attribute__((aligned(16))) g_v[NV];
static inline void put(int *k, uint32_t c, int x, int y);
static int g_nv, g_w = 480, g_h = 270;
static unsigned g_cmd;   /* estimated display-list words used this frame; the GE hangs if the list overruns */
unsigned g_ge_dropped, g_ge_nv, g_ge_cmd;   /* polys/outlines skipped because the vertex buffer was full */

void nfm_ge_begin(unsigned int *list, void *vram_off, int w, int h)
{
    g_w = w; g_h = h; g_nv = 0; g_cmd = 0; g_ge_dropped = 0;
    sceGuStart(GU_DIRECT, list);
    sceGuDrawBufferList(GU_PSM_8888, vram_off, 512);
    sceGuOffset(2048 - 240, 2048 - 136);
    sceGuViewport(2048, 2048, 480, 272);
    sceGuScissor(0, 0, w, h);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuDisable(GU_STENCIL_TEST);
    sceGuDisable(GU_LIGHTING);
}

void nfm_ge_finish(void)
{
    g_ge_nv = (unsigned)g_nv; g_ge_cmd = g_cmd;
#ifdef NFM_GETEST
    { GV *t = (GV *)sceGuGetMemory(2 * sizeof(GV));   /* -DNFM_GETEST: red square, proves the GE state is live */
      t[0] = (GV){ 0xFF0000FFu, 10, 10, 0, 0 }; t[1] = (GV){ 0xFF0000FFu, 110, 110, 0, 0 };
      sceGuDrawArray(GU_SPRITES, VFMT, 2, NULL, t); }
#endif
#ifndef NFM_NOWB   /* profiling only: skipping this is wrong on real hardware */
    sceKernelDcacheWritebackRange(g_v, (unsigned)g_nv * sizeof(GV));
#endif
    sceGuFinish();
}

void nfm_ge_sync(void) { sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE); }

/* Sutherland-Hodgman against the frame rectangle grown by 2 px; keeps coordinates well inside int16 */
static int clip_edge(const float *in, int n, float *out, int axis, float lim, int keep_greater)
{
    int m = 0;
    for (int i = 0, j = n - 1; i < n; j = i++) {
        const float *a = &in[2 * j], *b = &in[2 * i];
        float da = keep_greater ? a[axis] - lim : lim - a[axis], db = keep_greater ? b[axis] - lim : lim - b[axis];
        if ((da >= 0) != (db >= 0)) {
            float t = da / (da - db);
            out[2 * m] = a[0] + (b[0] - a[0]) * t; out[2 * m + 1] = a[1] + (b[1] - a[1]) * t; m++;
        }
        if (db >= 0 && m < MAXV - 1) { out[2 * m] = b[0]; out[2 * m + 1] = b[1]; m++; }
    }
    return m;
}

/* -> integer pixel polygon in (x,y), returns vertex count (0 = nothing to draw) */
static int prep(const float *xy, int n, int *x, int *y)
{
    float bufA[MAXV * 2], bufB[MAXV * 2];
    if (n < 3 || n > MAXV - 8) return 0;
    int need = 0;
    for (int i = 0; i < n; i++) {
        float px = xy[2 * i], py = xy[2 * i + 1];
        if (!(fabsf(px) < 1e7f) || !(fabsf(py) < 1e7f)) return 0;   /* NaN / inf */
        if (px < -GE_LIM || px > GE_LIM || py < -GE_LIM || py > GE_LIM) need = 1;
    }
    const float *p = xy;
    if (need) {
        float lx = -2.f, hx = g_w + 2.f, ly = -2.f, hy = g_h + 2.f;
        n = clip_edge(xy, n, bufA, 0, lx, 1);   if (n < 3) return 0;
        n = clip_edge(bufA, n, bufB, 0, hx, 0); if (n < 3) return 0;
        n = clip_edge(bufB, n, bufA, 1, ly, 1); if (n < 3) return 0;
        n = clip_edge(bufA, n, bufB, 1, hy, 0); if (n < 3) return 0;
        p = bufB;
    }
    for (int i = 0; i < n; i++) { x[i] = (int)floorf(p[2 * i] + 0.5f); y[i] = (int)floorf(p[2 * i + 1] + 0.5f); }
    return n;
}

static inline long cross(int ax, int ay, int bx, int by, int cx, int cy) { return (long)(bx - ax) * (cy - ay) - (long)(by - ay) * (cx - ax); }

static int is_convex(const int *x, const int *y, int n)
{
    int sign = 0;
    for (int i = 0, j = 1, k = 2; i < n; i++, j = j + 1 == n ? 0 : j + 1, k = k + 1 == n ? 0 : k + 1) {
        long c = cross(x[i], y[i], x[j], y[j], x[k], y[k]);
        if (c == 0) continue;
        int s = c > 0 ? 1 : -1;
        if (sign && s != sign) return 0;
        sign = s;
    }
    return 1;
}

static inline void put(int *k, uint32_t c, int x, int y) { g_v[*k].c = c; g_v[*k].x = (int16_t)x; g_v[*k].y = (int16_t)y; g_v[*k].z = 0; g_v[*k].pad = 0; (*k)++; }

static int in_tri(int px, int py, int ax, int ay, int bx, int by, int cx, int cy)
{
    long d1 = cross(ax, ay, bx, by, px, py), d2 = cross(bx, by, cx, cy, px, py), d3 = cross(cx, cy, ax, ay, px, py);
    int neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}

static void emit_poly(const int *x, const int *y, int n, uint32_t color)
{
    uint32_t c = color | 0xFF000000u;
    if (g_nv + 3 * n > NV || g_cmd + CMD_WORDS > LIST_CAP) { g_ge_dropped++; return; }
    g_cmd += CMD_WORDS;
    GV *base = &g_v[g_nv];
    if (is_convex(x, y, n)) {
        int k = g_nv;
        for (int i = 0; i < n; i++) put(&k, c, x[i], y[i]);
        DRAW(GU_TRIANGLE_FAN, n, base);
        g_nv = k;
        return;
    }
    /* concave: ear clipping (polygons here are tiny, stage lettering is the largest at 28 verts) */
    int idx[MAXV], m = n;
    long area = 0;
    for (int i = 0; i < n; i++) { idx[i] = i; int j = (i + 1) % n; area += (long)x[i] * y[j] - (long)x[j] * y[i]; }
    int sgn = area >= 0 ? 1 : -1, k = g_nv;
    int guard = 0;
    while (m > 3 && guard++ < 4 * MAXV) {
        int ear = -1;
        for (int i = 0; i < m && ear < 0; i++) {
            int a = idx[(i + m - 1) % m], b = idx[i], d = idx[(i + 1) % m];
            long cr = cross(x[a], y[a], x[b], y[b], x[d], y[d]);
            if (cr == 0 || (cr > 0 ? 1 : -1) != sgn) continue;
            int ok = 1;
            for (int j = 0; j < m && ok; j++) {
                int q = idx[j];
                if (q == a || q == b || q == d) continue;
                if (in_tri(x[q], y[q], x[a], y[a], x[b], y[b], x[d], y[d])) ok = 0;
            }
            if (ok) ear = i;
        }
        if (ear < 0) ear = 0;   /* degenerate / self-crossing: clip anyway */
        int a = idx[(ear + m - 1) % m], b = idx[ear], d = idx[(ear + 1) % m];
        put(&k, c, x[a], y[a]); put(&k, c, x[b], y[b]); put(&k, c, x[d], y[d]);
        for (int i = ear; i < m - 1; i++) idx[i] = idx[i + 1];
        m--;
    }
    if (m == 3) { put(&k, c, x[idx[0]], y[idx[0]]); put(&k, c, x[idx[1]], y[idx[1]]); put(&k, c, x[idx[2]], y[idx[2]]); }
    int cnt = k - g_nv;
    if (cnt >= 3) DRAW(GU_TRIANGLES, cnt, base);
    g_nv = k;
}

static void emit_outline(const int *x, const int *y, int n, uint32_t color)
{
    if (g_nv + n + 1 > NV || g_cmd + CMD_WORDS > LIST_CAP) { g_ge_dropped++; return; }
    g_cmd += CMD_WORDS;
    uint32_t c = color | 0xFF000000u;
    GV *base = &g_v[g_nv];
    int k = g_nv;
    for (int i = 0; i < n; i++) put(&k, c, x[i], y[i]);
    put(&k, c, x[0], y[0]);
    DRAW(GU_LINE_STRIP, n + 1, base);
    g_nv = k;
}

/* float entry points: clip/validate, then emit (used for the rare off-screen-heavy polygons) */
void nfm_ge_poly(const float *xy, int n, uint32_t color)
{
#ifdef NFM_GESKIP
    return;   /* profiling: measure everything except GE submission */
#endif
    int x[MAXV], y[MAXV];
    n = prep(xy, n, x, y);
    if (n) emit_poly(x, y, n, color);
}

void nfm_ge_outline(const float *xy, int n, uint32_t color)
{
#ifdef NFM_GESKIP
    return;
#endif
    int x[MAXV], y[MAXV];
    if (n < 2 || n > MAXV - 8) return;
    for (int i = 0; i < n; i++) {
        if (!(fabsf(xy[2 * i]) < GE_LIM) || !(fabsf(xy[2 * i + 1]) < GE_LIM)) return;   /* far off-screen outline: skip (int16 safety) */
        x[i] = (int)floorf(xy[2 * i] + 0.5f); y[i] = (int)floorf(xy[2 * i + 1] + 0.5f);
    }
    emit_outline(x, y, n, color);
}

/* integer entry points: medium.c already has integer native-space coordinates, so the common case (everything
 * within +-3000 px after scaling) is just a scale + round with no float validation or floorf calls */
#define RND(v) ((int)((v) + 4096.5f) - 4096)   /* floor(v + 0.5) for v > -4096 */
void nfm_ge_poly_i(const int *xs, const int *ys, int n, float scale, uint32_t color)
{
#ifdef NFM_GESKIP
    return;
#endif
    int x[MAXV], y[MAXV], far = 0;
    if (n < 3 || n > MAXV - 8) return;
    for (int i = 0; i < n; i++) {
        float fx = xs[i] * scale, fy = ys[i] * scale;
        far |= (fx > GE_LIM) | (fx < -GE_LIM) | (fy > GE_LIM) | (fy < -GE_LIM);
        x[i] = RND(fx); y[i] = RND(fy);
    }
    if (far) {
        float xy[MAXV * 2];
        for (int i = 0; i < n; i++) { xy[2 * i] = xs[i] * scale; xy[2 * i + 1] = ys[i] * scale; }
        nfm_ge_poly(xy, n, color);
        return;
    }
    emit_poly(x, y, n, color);
}

void nfm_ge_outline_i(const int *xs, const int *ys, int n, float scale, uint32_t color)
{
#ifdef NFM_GESKIP
    return;
#endif
    int x[MAXV], y[MAXV];
    if (n < 2 || n > MAXV - 8) return;
    for (int i = 0; i < n; i++) {
        float fx = xs[i] * scale, fy = ys[i] * scale;
        if (fx > GE_LIM || fx < -GE_LIM || fy > GE_LIM || fy < -GE_LIM) return;   /* far off-screen outline: skip (GE range) */
        x[i] = RND(fx); y[i] = RND(fy);
    }
    emit_outline(x, y, n, color);
}

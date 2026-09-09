/* gfx_psp.c — PSP GU backend for gfx.h.
 *
 * Perf pass: sprites are CPU-transformed to screen-space quads and accumulated
 * into per-texture batches; each batch is one sceGuTexImage + one sceGumDrawArray
 * at frame end. Textures are the pipeline's .ptx blobs used natively: RGBA5551,
 * power-of-two, PSP-swizzled -> GU_PSM_5551 with no conversion. */
#include "../../src/gfx.h"
#include "../../src/fp.h"
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <pspgum.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <math.h>

#define BUF_W 512
#define SCR_W 480
#define SCR_H 272

#define MAX_VERTS   12288          /* 2048 sprites * 6 verts */
#define MAX_BATCHES 64

static unsigned int __attribute__((aligned(16))) g_list[64 * 1024];

struct GfxTex {
    int  tw, th;
    int  swizzled;
    void *pixels;                  /* 16-byte aligned, RGBA5551 */
};

typedef struct {
    float u, v;
    unsigned int color;
    float x, y, z;
} Vtx;

static Vtx  __attribute__((aligned(16))) g_verts[MAX_VERTS];
static int  g_nverts;
static struct { GfxTex *tex; int first, count; } g_batch[MAX_BATCHES];
static int  g_nbatch;

static unsigned int rgb_to_abgr(unsigned int rgb)
{
    return 0xFF000000u | ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
}

bool gfx_init(const char *title, int lw, int lh, int scale)
{
    (void)title; (void)lw; (void)lh; (void)scale;

    void *fbp0 = 0;
    void *fbp1 = (void *)(BUF_W * SCR_H * 4);
    void *zbp  = (void *)(BUF_W * SCR_H * 4 * 2);

    sceGuInit();
    sceGuStart(GU_DIRECT, g_list);
    sceGuDrawBuffer(GU_PSM_8888, fbp0, BUF_W);
    sceGuDispBuffer(SCR_W, SCR_H, fbp1, BUF_W);
    sceGuDepthBuffer(zbp, BUF_W);
    sceGuOffset(2048 - (SCR_W / 2), 2048 - (SCR_H / 2));
    sceGuViewport(2048, 2048, SCR_W, SCR_H);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuScissor(0, 0, SCR_W, SCR_H);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuEnable(GU_BLEND);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexScale(1.0f, 1.0f);
    sceGuTexOffset(0.0f, 0.0f);
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);
    return true;
}

void gfx_shutdown(void) { sceGuTerm(); }

void gfx_frame_begin(uint32_t rgb)
{
    g_nverts = 0;
    g_nbatch = 0;

    sceGuStart(GU_DIRECT, g_list);
    sceGuClearColor(rgb_to_abgr(rgb));
    sceGuClear(GU_COLOR_BUFFER_BIT);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumOrtho(0, SCR_W, SCR_H, 0, -1, 1);
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
}

static void flush_batches(void)
{
    if (g_nverts) sceKernelDcacheWritebackRange(g_verts, g_nverts * sizeof(Vtx));
    for (int i = 0; i < g_nbatch; i++) {
        GfxTex *t = g_batch[i].tex;
        sceGuTexMode(GU_PSM_5551, 0, 0, t->swizzled ? GU_TRUE : GU_FALSE);
        sceGuTexImage(0, t->tw, t->th, t->tw, t->pixels);
        sceGumDrawArray(GU_TRIANGLES,
            GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
            g_batch[i].count, 0, &g_verts[g_batch[i].first]);
    }
}

void gfx_frame_end(void)
{
    flush_batches();
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

/* --- .ptx --------------------------------------------------------------- */
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static GfxTex *tex_alloc(const uint8_t *px, int w, int h, int swizzled)
{
    size_t bytes = (size_t)w * h * 2;
    void *buf = memalign(16, bytes);
    if (!buf) return NULL;
    memcpy(buf, px, bytes);
    sceKernelDcacheWritebackRange(buf, bytes);
    GfxTex *t = malloc(sizeof *t);
    t->tw = w; t->th = h; t->swizzled = swizzled; t->pixels = buf;
    return t;
}

GfxTex *gfx_tex_load(const PakAsset *a)
{
    if (!a || a->kind != 0 || !a->data || a->size < 12) return NULL;
    const uint8_t *d = a->data;
    if (memcmp(d, "PTX1", 4) != 0) return NULL;
    int w = rd16(d + 4), h = rd16(d + 6);
    uint8_t fmt = d[8], flags = d[9];
    uint16_t pal = rd16(d + 10);
    if (fmt != 1 || pal != 0) return NULL;              /* want 5551, no palette */
    return tex_alloc(d + 12, w, h, flags & 1);
}

GfxTex *gfx_tex_from_pixels(const uint16_t *px, int w, int h)
{
    return tex_alloc((const uint8_t *)px, w, h, 0);     /* font atlas: linear */
}

/* --- batched sprite ---------------------------------------------------- */
static Vtx *batch_reserve(GfxTex *t, int nverts)
{
    if (g_nverts + nverts > MAX_VERTS) return NULL;
    if (g_nbatch == 0 || g_batch[g_nbatch - 1].tex != t) {
        if (g_nbatch >= MAX_BATCHES) return NULL;
        g_batch[g_nbatch].tex = t;
        g_batch[g_nbatch].first = g_nverts;
        g_batch[g_nbatch].count = 0;
        g_nbatch++;
    }
    g_batch[g_nbatch - 1].count += nverts;
    Vtx *v = &g_verts[g_nverts];
    g_nverts += nverts;
    return v;
}

void gfx_draw(GfxTex *t, double x, double y, double angle,
              double ox, double oy, double sx, double sy, uint32_t tint,
              int fx, int fy, int fw, int fh)
{
    if (!t) return;
    Vtx *v = batch_reserve(t, 6);
    if (!v) return;

    float cx = (float)(x - fp_camera.x);
    float cy = (float)(y - fp_camera.y);
    double a = angle * FP_RAD;
    float ca = cosf((float)a), sa = sinf((float)a);
    float ax = (float)fabs(sx), ay = (float)fabs(sy);

    float lx0 = (float)(-ox) * ax, lx1 = (float)(fw - ox) * ax;
    float ly0 = (float)(-oy) * ay, ly1 = (float)(fh - oy) * ay;

    /* screen-space corners: TL, TR, BR, BL */
    float px[4], py[4];
    float lxs[4] = { lx0, lx1, lx1, lx0 };
    float lys[4] = { ly0, ly0, ly1, ly1 };
    for (int i = 0; i < 4; i++) {
        px[i] = cx + lxs[i] * ca - lys[i] * sa;
        py[i] = cy + lxs[i] * sa + lys[i] * ca;
    }

    float u0 = (float)fx / t->tw, v0 = (float)fy / t->th;
    float u1 = (float)(fx + fw) / t->tw, v1 = (float)(fy + fh) / t->th;
    if (sx < 0) { float s = u0; u0 = u1; u1 = s; }
    if (sy < 0) { float s = v0; v0 = v1; v1 = s; }
    unsigned int col = rgb_to_abgr(tint);

    float uu[4] = { u0, u1, u1, u0 };
    float vv[4] = { v0, v0, v1, v1 };
    static const int tri[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i = 0; i < 6; i++) {
        int c = tri[i];
        v[i].u = uu[c]; v[i].v = vv[c]; v[i].color = col;
        v[i].x = px[c]; v[i].y = py[c]; v[i].z = 0;
    }
}

/* one GU_REPEAT quad instead of a per-tile loop */
void gfx_draw_tiled(GfxTex *t, double x, double y, int span_w, int span_h)
{
    if (!t) return;
    flush_batches();
    g_nverts = 0; g_nbatch = 0;

    sceGuTexMode(GU_PSM_5551, 0, 0, t->swizzled ? GU_TRUE : GU_FALSE);
    sceGuTexImage(0, t->tw, t->th, t->tw, t->pixels);
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);

    float x0 = (float)(x - fp_camera.x), y0 = (float)(y - fp_camera.y);
    float x1 = x0 + span_w, y1 = y0 + span_h;
    float u1 = (float)span_w / t->tw, v1 = (float)span_h / t->th;
    unsigned int col = 0xFFFFFFFFu;

    Vtx *v = sceGuGetMemory(6 * sizeof(Vtx));
    float uu[4] = { 0, u1, u1, 0 }, vv[4] = { 0, 0, v1, v1 };
    float xx[4] = { x0, x1, x1, x0 }, yy[4] = { y0, y0, y1, y1 };
    static const int tri[6] = { 0, 1, 2, 0, 2, 3 };
    for (int i = 0; i < 6; i++) {
        int c = tri[i];
        v[i] = (Vtx){ uu[c], vv[c], col, xx[c], yy[c], 0 };
    }
    sceGumDrawArray(GU_TRIANGLES,
        GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D,
        6, 0, v);

    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
}

bool gfx_save_bmp(const char *path) { (void)path; return false; }

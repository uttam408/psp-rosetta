/* gfx_psp.c — PSP GU backend for gfx.h.
 * Textures are the pipeline's .ptx blobs used natively: RGBA5551, power-of-two,
 * unswizzled -> GU_PSM_5551 with no conversion. Sprites draw as rotated textured
 * quads through the GUM matrix stack under an ortho projection. */
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

static unsigned int __attribute__((aligned(16))) g_list[128 * 1024];

struct GfxTex {
    int tw, th;          /* power-of-two texture dims */
    void *pixels;        /* 16-byte aligned, RGBA5551 */
};

typedef struct {
    float u, v;
    unsigned int color;
    float x, y, z;
} Vtx;

static unsigned int rgb_to_abgr(unsigned int rgb)
{
    return 0xFF000000u | ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
}

bool gfx_init(const char *title, int lw, int lh, int scale)
{
    (void)title; (void)lw; (void)lh; (void)scale;

    void *fbp0 = 0;                                   /* VRAM offset 0 */
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
    sceGuStart(GU_DIRECT, g_list);
    sceGuClearColor(rgb_to_abgr(rgb));
    sceGuClear(GU_COLOR_BUFFER_BIT);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumOrtho(0, SCR_W, SCR_H, 0, -1, 1);
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();
}

void gfx_frame_end(void)
{
    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuSwapBuffers();
}

/* --- .ptx (see pipeline/convert/textures.py) -------------------------------- */
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

GfxTex *gfx_tex_load(const PakAsset *a)
{
    if (!a || a->kind != 0 || !a->data || a->size < 12) return NULL;
    const uint8_t *d = a->data;
    if (memcmp(d, "PTX1", 4) != 0) return NULL;
    int w = rd16(d + 4), h = rd16(d + 6);
    uint8_t fmt = d[8], flags = d[9];
    uint16_t pal = rd16(d + 10);
    if (fmt != 1 || (flags & 1) || pal != 0) return NULL;   /* want linear 5551 */

    const uint8_t *px = d + 12;
    size_t bytes = (size_t)w * h * 2;
    void *buf = memalign(16, bytes);
    if (!buf) return NULL;
    memcpy(buf, px, bytes);
    sceKernelDcacheWritebackRange(buf, bytes);

    GfxTex *t = malloc(sizeof *t);
    t->tw = w; t->th = h; t->pixels = buf;
    return t;
}

void gfx_draw(GfxTex *t, double x, double y, double angle,
              double ox, double oy, double sx, double sy, uint32_t tint,
              int fx, int fy, int fw, int fh)
{
    if (!t) return;
    float screenx = (float)(x - fp_camera.x);
    float screeny = (float)(y - fp_camera.y);

    sceGuTexMode(GU_PSM_5551, 0, 0, GU_FALSE);
    sceGuTexImage(0, t->tw, t->th, t->tw, t->pixels);

    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    ScePspFVector3 tr = { screenx, screeny, 0.0f };
    sceGumTranslate(&tr);
    sceGumRotateZ((float)(angle * FP_RAD));
    ScePspFVector3 sc = { (float)fabs(sx), (float)fabs(sy), 1.0f };
    sceGumScale(&sc);

    float l = -(float)ox, tp = -(float)oy, r = (float)(fw) - (float)ox, b = (float)(fh) - (float)oy;
    if (sx < 0) { float s = l; l = r; r = s; }
    if (sy < 0) { float s = tp; tp = b; b = s; }
    /* GU_TEXTURE_32BITF texcoords are normalised [0,1] */
    float u0 = (float)fx / t->tw, v0 = (float)fy / t->th;
    float u1 = (float)(fx + fw) / t->tw, v1 = (float)(fy + fh) / t->th;
    unsigned int col = rgb_to_abgr(tint);

    Vtx *v = sceGuGetMemory(4 * sizeof(Vtx));
    v[0] = (Vtx){ u0, v0, col, l,  tp, 0 };
    v[1] = (Vtx){ u0, v1, col, l,  b,  0 };
    v[2] = (Vtx){ u1, v0, col, r,  tp, 0 };
    v[3] = (Vtx){ u1, v1, col, r,  b,  0 };
    sceGumDrawArray(GU_TRIANGLE_STRIP,
        GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
        4, 0, v);
}

void gfx_draw_tiled(GfxTex *t, double x, double y, int span_w, int span_h)
{
    if (!t) return;
    for (double ty = y; ty < y + span_h; ty += t->th)
        for (double tx = x; tx < x + span_w; tx += t->tw)
            gfx_draw(t, tx, ty, 0, 0, 0, 1, 1, 0xFFFFFF, 0, 0, t->tw, t->th);
}

bool gfx_save_bmp(const char *path) { (void)path; return false; }

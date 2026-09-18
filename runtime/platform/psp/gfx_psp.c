/* gfx_psp.c — PSP GU backend for gfx.h.
 *
 * Sprites: per-sprite model matrix (translate/rotate/scale) + a 4-vert quad
 * through the GUM stack under an ortho projection — the known-good path.
 * Perf: the texture bind (sceGuTexImage) is skipped when the same texture is
 * used by consecutive draws, which the layer-ordered render makes common.
 * Textures are the pipeline's .ptx blobs used natively — RGBA5551, POT,
 * PSP-swizzled -> GU_PSM_5551, no conversion. */
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
    int  tw, th;
    int  swizzled;
    void *pixels;
};

typedef struct { float u, v; unsigned int color; float x, y, z; } Vtx;

static GfxTex *g_bound;     /* currently-bound texture (bind cache) */

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
    sceGuStart(GU_DIRECT, g_list);
    sceGuClearColor(rgb_to_abgr(rgb));
    sceGuClear(GU_COLOR_BUFFER_BIT);

    sceGumMatrixMode(GU_PROJECTION);
    sceGumLoadIdentity();
    sceGumOrtho(0, SCR_W, SCR_H, 0, -1, 1);
    sceGumMatrixMode(GU_VIEW);
    sceGumLoadIdentity();

    g_bound = NULL;
}

void gfx_frame_end(void)
{
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
    if (fmt != 1 || pal != 0) return NULL;
    return tex_alloc(d + 12, w, h, flags & 1);
}

GfxTex *gfx_tex_from_pixels(const uint16_t *px, int w, int h)
{
    return tex_alloc((const uint8_t *)px, w, h, 0);     /* font atlas: linear */
}

static void bind(GfxTex *t)
{
    if (t == g_bound) return;
    sceGuTexMode(GU_PSM_5551, 0, 0, t->swizzled ? GU_TRUE : GU_FALSE);
    sceGuTexImage(0, t->tw, t->th, t->tw, t->pixels);
    g_bound = t;
}

void gfx_draw(GfxTex *t, double x, double y, double angle,
              double ox, double oy, double sx, double sy, uint32_t tint,
              int fx, int fy, int fw, int fh)
{
    if (!t) return;

    float screenx = (float)(x - fp_camera.x);
    float screeny = (float)(y - fp_camera.y);

    /* frustum cull: skip fully off-screen sprites before touching the GU at
     * all (no bind, no matrix, no draw). Margin is deliberately generous
     * (covers any origin offset + rotation) — can only skip truly invisible
     * sprites, never a visible one. This is the fix for the 20+-enemy stutter:
     * enemies/bullets/FX spawn and travel well outside the 480x272 view. */
    {
        float dsx = (float)fabs(sx), dsy = (float)fabs(sy);
        float margin = (float)(fw + fh) * (dsx > dsy ? dsx : dsy) + 8.0f;
        if (screenx + margin < 0 || screenx - margin > SCR_W ||
            screeny + margin < 0 || screeny - margin > SCR_H)
            return;
    }

    bind(t);

    sceGumMatrixMode(GU_MODEL);
    sceGumLoadIdentity();
    ScePspFVector3 tr = { screenx, screeny, 0.0f };
    sceGumTranslate(&tr);
    if (angle != 0.0) sceGumRotateZ((float)(angle * FP_RAD));
    if (sx != 1.0 || sy != 1.0) {
        ScePspFVector3 sc = { (float)fabs(sx), (float)fabs(sy), 1.0f };
        sceGumScale(&sc);
    }

    float l = (float)-ox, tp = (float)-oy;
    float r = (float)(fw - ox), b = (float)(fh - oy);
    float u0 = (float)fx / t->tw, v0 = (float)fy / t->th;
    float u1 = (float)(fx + fw) / t->tw, v1 = (float)(fy + fh) / t->th;
    if (sx < 0) { float s = u0; u0 = u1; u1 = s; }
    if (sy < 0) { float s = v0; v0 = v1; v1 = s; }
    unsigned int col = rgb_to_abgr(tint);

    Vtx *v = sceGuGetMemory(4 * sizeof(Vtx));
    v[0] = (Vtx){ u0, v0, col, l, tp, 0 };
    v[1] = (Vtx){ u0, v1, col, l, b,  0 };
    v[2] = (Vtx){ u1, v0, col, r, tp, 0 };
    v[3] = (Vtx){ u1, v1, col, r, b,  0 };
    sceGumDrawArray(GU_TRIANGLE_STRIP,
        GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D,
        4, 0, v);
}

/* Per-tile draws through the normal (CLAMP-sampled) gfx_draw path — NOT a
 * single GU_REPEAT quad. A swizzled texture sampled with GU_REPEAT does not
 * wrap correctly on the PSP GU (the swizzle block layout breaks the simple
 * modulo addressing REPEAT relies on): the water/space bands, both swizzled,
 * rendered corrupted/incomplete this way, which is why sinking ships and the
 * surfacing submarine appeared to float in front of the water instead of
 * being masked by it. Water/space are only 2 entities, so the extra draw
 * calls here are not a real perf concern. */
void gfx_draw_tiled(GfxTex *t, double x, double y, int span_w, int span_h)
{
    if (!t) return;
    for (int ty = 0; ty < span_h; ty += t->th)
        for (int tx = 0; tx < span_w; tx += t->tw)
            gfx_draw(t, x + tx, y + ty, 0, 0, 0, 1, 1, 0xFFFFFF, 0, 0, t->tw, t->th);
}

bool gfx_save_bmp(const char *path) { (void)path; return false; }

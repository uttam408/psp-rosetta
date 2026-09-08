/* gfx_sdl.c — SDL2 backend for gfx.h (desktop fast-iteration target). */
#include "../../src/gfx.h"
#include "../../src/ptx.h"
#include "../../src/fp.h"
#include <SDL.h>
#include <stdlib.h>
#include <math.h>

struct GfxTex {
    SDL_Texture *tex;
    int w, h;
};

static SDL_Window   *g_win;
static SDL_Renderer *g_ren;

bool gfx_init(const char *title, int lw, int lh, int scale)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_Log("SDL_Init: %s", SDL_GetError());
        return false;
    }
    g_win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             lw * scale, lh * scale, SDL_WINDOW_ALLOW_HIGHDPI);
    if (!g_win) return false;
    g_ren = SDL_CreateRenderer(g_win, -1,
                               SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!g_ren) return false;
    SDL_RenderSetLogicalSize(g_ren, lw, lh);
    SDL_SetRenderDrawBlendMode(g_ren, SDL_BLENDMODE_BLEND);
    return true;
}

void gfx_shutdown(void)
{
    if (g_ren) SDL_DestroyRenderer(g_ren);
    if (g_win) SDL_DestroyWindow(g_win);
    SDL_Quit();
}

void gfx_frame_begin(uint32_t rgb)
{
    SDL_SetRenderDrawColor(g_ren, (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255);
    SDL_RenderClear(g_ren);
}

void gfx_frame_end(void) { SDL_RenderPresent(g_ren); }

GfxTex *gfx_tex_load(const PakAsset *a)
{
    if (!a || a->kind != 0 || !a->data) return NULL;
    PtxImage img;
    if (!ptx_decode(a->data, a->size, &img)) return NULL;

    SDL_Texture *t = SDL_CreateTexture(g_ren, SDL_PIXELFORMAT_ABGR8888,
                                       SDL_TEXTUREACCESS_STATIC, img.w, img.h);
    if (t) {
        SDL_UpdateTexture(t, NULL, img.rgba, img.w * 4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    }
    free(img.rgba);
    if (!t) return NULL;

    GfxTex *g = malloc(sizeof *g);
    g->tex = t; g->w = img.w; g->h = img.h;
    return g;
}

void gfx_draw(GfxTex *t, double x, double y, double angle,
              double ox, double oy, double sx, double sy, uint32_t tint,
              int fx, int fy, int fw, int fh)
{
    if (!t) return;
    double dsx = fabs(sx), dsy = fabs(sy);
    double screenx = x - fp_camera.x;
    double screeny = y - fp_camera.y;

    SDL_Rect src = { fx, fy, fw, fh };
    SDL_FRect dst = { (float)(screenx - ox * dsx), (float)(screeny - oy * dsy),
                      (float)(fw * dsx), (float)(fh * dsy) };
    SDL_FPoint center = { (float)(ox * dsx), (float)(oy * dsy) };
    int flip = SDL_FLIP_NONE;
    if (sx < 0) flip |= SDL_FLIP_HORIZONTAL;
    if (sy < 0) flip |= SDL_FLIP_VERTICAL;

    SDL_SetTextureColorMod(t->tex, (tint >> 16) & 0xFF, (tint >> 8) & 0xFF, tint & 0xFF);
    /* FP applies rotation as angle*RAD (RAD = -PI/180). SDL wants degrees CW. */
    SDL_RenderCopyExF(g_ren, t->tex, &src, &dst, -angle, &center,
                      (SDL_RendererFlip)flip);
}

void gfx_draw_tiled(GfxTex *t, double x, double y, int span_w, int span_h)
{
    if (!t) return;
    for (double ty = y; ty < y + span_h; ty += t->h)
        for (double tx = x; tx < x + span_w; tx += t->w)
            gfx_draw(t, tx, ty, 0, 0, 0, 1, 1, 0xFFFFFF, 0, 0, t->w, t->h);
}

bool gfx_save_bmp(const char *path)
{
    int w, h;
    SDL_GetRendererOutputSize(g_ren, &w, &h);
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ABGR8888);
    if (!s) return false;
    bool ok = SDL_RenderReadPixels(g_ren, NULL, SDL_PIXELFORMAT_ABGR8888,
                                   s->pixels, s->pitch) == 0
              && SDL_SaveBMP(s, path) == 0;
    SDL_FreeSurface(s);
    return ok;
}

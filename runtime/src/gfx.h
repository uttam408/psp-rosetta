/* gfx.h — backend-agnostic 2D drawing. Implemented per platform
 * (platform/sdl/gfx_sdl.c, later platform/psp/gfx_psp.c).
 * All draw calls take WORLD coordinates; the camera (fp_camera) is applied here. */
#ifndef RT_GFX_H
#define RT_GFX_H

#include <stdint.h>
#include <stdbool.h>
#include "pak.h"
#include "real.h"

typedef struct GfxTex GfxTex;

bool gfx_init(const char *title, int logical_w, int logical_h, int scale);
void gfx_shutdown(void);

void gfx_frame_begin(uint32_t clear_rgb);
void gfx_frame_end(void);

/* upload a pak image asset (decodes its .ptx); returns NULL on failure */
GfxTex *gfx_tex_load(const PakAsset *a);

/* upload raw RGBA5551 pixels (w,h power-of-two) — used for the built-in font */
GfxTex *gfx_tex_from_pixels(const uint16_t *px5551, int w, int h);

/* draw one sprite/frame. angle is degrees in FlashPunk convention (RAD = -PI/180).
 * origin is in frame pixels; scale <0 flips. tint_rgb 0xFFFFFF = untinted. */
void gfx_draw(GfxTex *t, real x, real y, real angle,
              real origin_x, real origin_y,
              real scale_x, real scale_y, uint32_t tint_rgb,
              int frame_x, int frame_y, int frame_w, int frame_h);

/* draw a texture tiled to cover span_w x span_h starting at world (x,y) */
void gfx_draw_tiled(GfxTex *t, real x, real y, int span_w, int span_h);

/* Clip subsequent draws to screen rows above world y (sprites below it are cut
 * off). gfx_clip_reset() lifts it. Used to put the water "in front" of sprites
 * regardless of GPU draw-order behaviour. */
void gfx_clip_below(real world_y);
void gfx_clip_reset(void);

/* headless: dump the current backbuffer to a BMP (for tests / CI) */
bool gfx_save_bmp(const char *path);

#endif

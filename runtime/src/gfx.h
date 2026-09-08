/* gfx.h — backend-agnostic 2D drawing. Implemented per platform
 * (platform/sdl/gfx_sdl.c, later platform/psp/gfx_psp.c).
 * All draw calls take WORLD coordinates; the camera (fp_camera) is applied here. */
#ifndef RT_GFX_H
#define RT_GFX_H

#include <stdint.h>
#include <stdbool.h>
#include "pak.h"

typedef struct GfxTex GfxTex;

bool gfx_init(const char *title, int logical_w, int logical_h, int scale);
void gfx_shutdown(void);

void gfx_frame_begin(uint32_t clear_rgb);
void gfx_frame_end(void);

/* upload a pak image asset (decodes its .ptx); returns NULL on failure */
GfxTex *gfx_tex_load(const PakAsset *a);

/* draw one sprite/frame. angle is degrees in FlashPunk convention (RAD = -PI/180).
 * origin is in frame pixels; scale <0 flips. tint_rgb 0xFFFFFF = untinted. */
void gfx_draw(GfxTex *t, double x, double y, double angle,
              double origin_x, double origin_y,
              double scale_x, double scale_y, uint32_t tint_rgb,
              int frame_x, int frame_y, int frame_w, int frame_h);

/* draw a texture tiled to cover span_w x span_h starting at world (x,y) */
void gfx_draw_tiled(GfxTex *t, double x, double y, int span_w, int span_h);

/* headless: dump the current backbuffer to a BMP (for tests / CI) */
bool gfx_save_bmp(const char *path);

#endif

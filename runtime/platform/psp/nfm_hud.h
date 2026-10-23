#ifndef NFM_HUD_H
#define NFM_HUD_H
#include <stdint.h>

/* HUD drawn straight into the CPU framebuffer (the default NFM render path) after scene_draw, before the blit.
 * Pixels are PSP-native ABGR8888, i.e. what medium.c packs under NFM_ABGR.  Sprites come from the pak's
 * image/<name> assets (swizzled 5551), decoded once to ABGR at their opaque extent. */
typedef struct { uint32_t *px; int w, h; } HudTex;

void hud_begin(uint32_t *fb, int w, int h);                   /* target for the calls below (stride = w) */
int  hud_load(HudTex *t, const char *name);                   /* 1 on success */
void hud_sprite(const HudTex *t, float x, float y, float s);  /* top-left at x,y; nearest-neighbour scale s */
void hud_rect(float x, float y, float w, float h, uint32_t rgb);   /* rgb = 0xRRGGBB */
void hud_arrow(float cx, float cy, float ang_deg, float size);     /* ang 0 = up, + = clockwise */
#endif

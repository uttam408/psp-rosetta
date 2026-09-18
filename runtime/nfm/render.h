/* render.h — CPU flat-polygon renderer in the style of NFM's Medium/Plane: transform,
 * near-clip, painter's-algorithm sort, flat shade, scanline fill. float only (PSP FPU).
 * This is the bit-faithful baseline; a GU backend replaces fill_poly, not the sort. */
#ifndef NFM_RENDER_H
#define NFM_RENDER_H

#include <stdint.h>
#include "pmesh.h"

typedef struct {
    int       w, h;
    uint32_t *px;      /* 0x00RRGGBB, w*h */
    float     focal;   /* pixels */
} Frame;

typedef struct {
    float x, y, z;     /* camera position, world (Y-down as authored) */
    float yaw, pitch;  /* radians */
} Camera;

void frame_clear(Frame *f, uint32_t rgb);

/* draw one mesh instance at (x,y,z) with yaw about the vertical axis (radians).
 * paint1/paint2 (0xRRGGBB, or -1 for the model's own) repaint 1stColor/2ndColor polys. */
void draw_mesh(Frame *f, const Camera *c, const PMesh *m,
               float x, float y, float z, float yaw, int32_t paint1, int32_t paint2);

/* per-frame stats, reset by frame_clear */
extern int g_polys_in, g_polys_drawn;

#endif

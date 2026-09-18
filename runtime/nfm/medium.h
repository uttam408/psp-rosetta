/* medium.h — port of NFM's Medium (camera, sky/ground/fog state) and Plane.d / ContO.d
 * (transform, cull, sort, shade, fill).  All geometry runs in the original 800x450
 * integer space with Java's truncation semantics; only the final fill scales to the
 * frame (0.6x for 480x270).  Build with -ffp-contract=off -fwrapv to stay bit-exact. */
#ifndef NFM_MEDIUM_H
#define NFM_MEDIUM_H

#include <stdint.h>
#include <stdbool.h>
#include "pmesh.h"

typedef struct {
    int w, h;
    uint32_t *px;          /* 0x00RRGGBB */
} Frame;

typedef struct {
    int cx, cy, cz;        /* fixed camera pivot: 400, 225, 50 */
    int xz, zy;            /* camera yaw / pitch, integer degrees */
    int x, y, z;           /* camera world position */
    int focus_point;       /* 400 */
    int iw, ih, w, h;      /* 0, 0, 800, 450 */
    int ground, skyline;
    int fade[16], fogd;
    int cfade[3], csky[3], cgrnd[3], crgrnd[3], cpol[3], osky[3], ogrnd[3], snap[3];
    int texture[4];
    int adv;               /* 500 */
    int lastmaf;
    float scale;           /* 800x450 -> frame, 0.6 for 480x270 */
    int trk;               /* 0 = normal render */
    bool lightson;
    Frame *frame;
} Medium;

void medium_init(Medium *m, Frame *f);
void medium_setsnap(Medium *m, int r, int g, int b);
void medium_setsky(Medium *m, int r, int g, int b);
void medium_setgrnd(Medium *m, int r, int g, int b);
void medium_setpolys(Medium *m, int r, int g, int b);
void medium_setfade(Medium *m, int r, int g, int b);
void medium_fadfrom(Medium *m, int n);

float m_sin(int deg);
float m_cos(int deg);
int  m_xs(const Medium *m, int x, int cz);             /* Plane.xs */
int  m_ys(const Medium *m, int y, int cz);             /* Plane.ys */

/* Medium.d: sky bands, ground bands. (mountains/clouds/stars deferred) */
void medium_draw_backdrop(Medium *m);

/* an instance of a mesh placed in the world; owns per-poly persistent state */
typedef struct {
    const PMesh *mesh;
    int x, y, z, xz, xy, zy;
    int dist;                    /* ContO.dist — painter key between objects */
    bool noline;
    int  *av;                    /* Plane.av persists between frames and feeds the sort */
    float *hsb;                  /* 3 per poly */
    int  *col;                   /* 3 per poly, post-snap */
    float *deltaf, *projf;
    uint8_t *typ;
} Inst;

/* paint1/paint2: 0xRRGGBB or -1 to keep the model's own */
void inst_init(Inst *o, Medium *m, const PMesh *mesh, int x, int y, int z, int xz,
               int32_t paint1, int32_t paint2);
void inst_free(Inst *o);
void inst_draw(Medium *m, Inst *o);          /* ContO.d */

extern int g_polys_in, g_polys_drawn;

#endif

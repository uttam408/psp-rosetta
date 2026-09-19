/* pmesh.h — loader for NFM .pmesh models (layout: pipeline/nfm/pmesh.py).
 * All pointers alias the input buffer, which must stay alive and 4-byte aligned. */
#ifndef NFM_PMESH_H
#define NFM_PMESH_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#pragma pack(push, 1)
typedef struct { float x, y, z; } PmVert;
typedef struct {
    uint32_t first_index;
    uint16_t nverts;
    uint8_t  material, light, r, g, b, paint;
    int16_t  gr, fs;
    uint8_t  no_outline, pad;
} PmPoly;
typedef struct { int32_t x, y, z, steer, width, height, gwgr, keyx, keyz; } PmWheel;
/* runtime-only per-poly extras, present (px != NULL) only on car meshes that carry procedural wheels */
typedef struct { int32_t wx, wy, wz; uint8_t wheel, master, disline, pad; } PmPolyX;
typedef struct {
    uint8_t  r, g, b, pad;
    int32_t  xy, zy, radx, rady, radz, x, y, z, skid;
    uint8_t  dam, notwall;
    uint16_t pad2;
} PmTrack;
#pragma pack(pop)

enum { PMF_DECOR = 1, PMF_SHADOW = 2, PMF_ROAD = 4, PMF_STONECOLD = 64, PMF_NEWSTONE = 128 };
/* PM_MAT_RAW = Plane glass==3 (procedural piles): colour used as-is, saturation +0.05 */
enum { PM_MAT_NORMAL = 0, PM_MAT_GLASS = 1, PM_MAT_GSHADOW = 2, PM_MAT_RAW = 3 };
enum { PM_LIGHT_NONE = 0, PM_LIGHT_FRONT = 1, PM_LIGHT_BACK = 2 };
enum { PM_PAINT_NONE = 0, PM_PAINT_FIRST = 1, PM_PAINT_SECOND = 2 };

typedef struct {
    uint16_t flags;
    uint32_t nverts, npolys, nindices, max_r;
    uint8_t  nwheels, ntracks;
    uint8_t  first_color[3], second_color[3];
    int16_t  rims[5];            /* r,g,b,size*10,depth*10; valid when has_rims */
    bool     has_rims;
    int      grat, sparkat;      /* car ground offset / spark height from the last wheel (Wheels.ground / .sparkat) */
    uint16_t disline, disp, grounded_pct;
    const PmVert  *verts;
    const PmPoly  *polys;
    const uint16_t *indices;
    const PmWheel *wheels;
    const PmTrack *tracks;
    const PmPolyX *px;           /* nverts-independent, one per poly, or NULL */
    void *xown;                  /* buffer built by car_mesh_build (free with pmesh_free) */
    void *owned;   /* aligned copy of the blob when the source was misaligned (free with pmesh_free) */
} PMesh;

/* returns false on bad magic/version/truncation */
bool pmesh_load(PMesh *m, const uint8_t *data, size_t size);
void pmesh_free(PMesh *m);

#endif

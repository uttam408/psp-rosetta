/* wheels.h — procedural wheel geometry (port of Wheels.make): 19 planes per wheel appended to a car mesh. */
#ifndef NFM_WHEELS_H
#define NFM_WHEELS_H
#include "pmesh.h"

/* Builds dst = src + wheel polygons (dst owns its buffers; free with pmesh_free) and fills dst->px,
 * dst->grat, dst->sparkat.  src may be freed afterwards.  Meshes without wheels are just copied. */
bool car_mesh_build(PMesh *dst, const PMesh *src);
#endif

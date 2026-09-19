/* pile.h — the procedural rock/debris pile (ContO's `pile` constructor).
 * Geometry is seeded by java.util.Random, so it must be regenerated identically. */
#ifndef NFM_PILE_H
#define NFM_PILE_H

#include <stdint.h>
#include "pmesh.h"
#include "medium.h"

/* java.util.Random, exactly (48-bit LCG) */
typedef struct { int64_t seed; } JRandom;
void   jr_seed(JRandom *r, int64_t seed);
double jr_next_double(JRandom *r);

/* Builds a heap-owned mesh: five polys (4 sides + top), 32 verts.  a=seed, b/c = the two
 * size args (2..6), env colours are read from m.  Free with pile_free. */
void pile_build(PMesh *out, const Medium *m, int seed, int b, int c);
void pile_free(PMesh *m);

#endif

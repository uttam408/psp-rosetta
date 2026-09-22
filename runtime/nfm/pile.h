/* pile.h — the procedural rock/debris pile (ContO's `pile` constructor).
 * Geometry is seeded by java.util.Random, so it must be regenerated identically. */
#ifndef NFM_PILE_H
#define NFM_PILE_H

#include <stdint.h>
#include "pmesh.h"
#include "medium.h"
#include "mad.h"

/* java.util.Random, exactly (48-bit LCG) */
typedef struct { int64_t seed; } JRandom;
void   jr_seed(JRandom *r, int64_t seed);
double jr_next_double(JRandom *r);

/* Builds a heap-owned mesh: five polys (4 sides + top), 32 verts.  a=seed, b/c = the two
 * size args (2..6), env colours are read from m.  Free with pile_free. */
void pile_build(PMesh *out, const Medium *m, int seed, int b, int c);
void pile_free(PMesh *m);

/* ContO's pile constructor also drops 4 sloped-side trackers + 1 flat-top tracker so the car climbs the pile
 * instead of driving through it; this rebuilds the same java.util.Random sequence to place them at (x, y, z). */
void pile_add_trackers(Trackers *t, const Medium *m, int seed, int b, int c, int x, int y, int z);

#endif

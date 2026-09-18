/* real.h — the engine's scalar type. PSP has a single-precision FPU only: double
 * arithmetic/libm is emulated in software (~1us per op, 25-40us per trig call),
 * which made 100 entities cost ~40ms/tick. So: float on PSP, double elsewhere. */
#ifndef REAL_H
#define REAL_H
#include <math.h>
#ifdef __PSP__
typedef float real;
#define rsin   sinf
#define rcos   cosf
#define ratan2 atan2f
#define rsqrt  sqrtf
#define rfabs  fabsf
#define rfloor floorf
#else
typedef double real;
#define rsin   sin
#define rcos   cos
#define ratan2 atan2
#define rsqrt  sqrt
#define rfabs  fabs
#define rfloor floor
#endif
#endif

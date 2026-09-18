/* fp.h — FlashPunk globals + math, reproduced faithfully (determinism-critical).
 * See docs/flashpunk-api-surface.md and net/flashpunk/FP.as. */
#ifndef RT_FP_H
#define RT_FP_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include "real.h"

#define FP_PI 3.14159265358979323846
extern const real FP_RAD; /* Math.PI / -180  */
extern const real FP_DEG; /* -180 / Math.PI  */

typedef struct { real x, y; } fp_vec;

extern int    fp_width, fp_height;
extern real fp_half_width, fp_half_height;
extern fp_vec fp_camera;
extern uint64_t fp_frame;   /* fixed-step sim frame counter */

void     fp_init(int w, int h);
void     fp_seed(uint32_t s);
uint32_t fp_random_seed(void);
real   fp_random(void);            /* [0,1)  — advances the LCG              */
uint32_t fp_rand(uint32_t amount);   /* rfloor(random()*amount)                */

real fp_choose2(real a, real b);   /* FP.choose(a,b) — rand(2) ? b : a */
real fp_approach(real v, real target, real amount);
real fp_lerp(real a, real b, real t);
real fp_distance(real x1, real y1, real x2, real y2);
real fp_angle(real x1, real y1, real x2, real y2);   /* degrees [0,360) */
void   fp_angle_xy(fp_vec *p, real angle_deg, real dist, real ox, real oy);
real fp_scale_clamp(real v, real lo, real hi, real a, real b);
real fp_clamp(real v, real lo, real hi);

/* AS3 Math.round: half rounds toward +Infinity (not away from zero like C round) */
static inline real fp_round(real v) { return rfloor(v + 0.5); }

#endif

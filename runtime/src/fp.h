/* fp.h — FlashPunk globals + math, reproduced faithfully (determinism-critical).
 * See docs/flashpunk-api-surface.md and net/flashpunk/FP.as. */
#ifndef RT_FP_H
#define RT_FP_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#define FP_PI 3.14159265358979323846
extern const double FP_RAD; /* Math.PI / -180  */
extern const double FP_DEG; /* -180 / Math.PI  */

typedef struct { double x, y; } fp_vec;

extern int    fp_width, fp_height;
extern double fp_half_width, fp_half_height;
extern fp_vec fp_camera;
extern uint64_t fp_frame;   /* fixed-step sim frame counter */

void     fp_init(int w, int h);
void     fp_seed(uint32_t s);
uint32_t fp_random_seed(void);
double   fp_random(void);            /* [0,1)  — advances the LCG              */
uint32_t fp_rand(uint32_t amount);   /* floor(random()*amount)                */

double fp_choose2(double a, double b);   /* FP.choose(a,b) — rand(2) ? b : a */
double fp_approach(double v, double target, double amount);
double fp_lerp(double a, double b, double t);
double fp_distance(double x1, double y1, double x2, double y2);
double fp_angle(double x1, double y1, double x2, double y2);   /* degrees [0,360) */
void   fp_angle_xy(fp_vec *p, double angle_deg, double dist, double ox, double oy);
double fp_scale_clamp(double v, double lo, double hi, double a, double b);
double fp_clamp(double v, double lo, double hi);

/* AS3 Math.round: half rounds toward +Infinity (not away from zero like C round) */
static inline double fp_round(double v) { return floor(v + 0.5); }

#endif

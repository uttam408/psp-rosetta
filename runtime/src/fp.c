#include "fp.h"

const real FP_RAD = FP_PI / -180.0;
const real FP_DEG = -180.0 / FP_PI;

int    fp_width = 0, fp_height = 0;
real fp_half_width = 0, fp_half_height = 0;
fp_vec fp_camera = {0, 0};
uint64_t fp_frame = 0;

static uint32_t g_seed = 1;

void fp_init(int w, int h)
{
    fp_width = w;
    fp_height = h;
    fp_half_width = w / 2.0;
    fp_half_height = h / 2.0;
    fp_camera.x = fp_camera.y = 0;
    fp_frame = 0;
}

void fp_seed(uint32_t s)
{
    if (s < 1) s = 1;
    if (s > 2147483646u) s = 2147483646u;
    g_seed = s;
}

uint32_t fp_random_seed(void) { return g_seed; }

/* FP.as: _seed = _seed * 16807 % 2147483647; return _seed / 2147483647; */
static void lcg_step(void) { g_seed = (uint32_t)((uint64_t)g_seed * 16807ULL % 2147483647ULL); }

real fp_random(void)
{
    lcg_step();
    real r = (real)g_seed / (real)2147483647.0;
    return r < 1 ? r : (real)0.9999999;   /* float rounding must not reach 1.0 */
}

/* exact integer form of floor(random()*amount): no float rounding, no soft-double */
uint32_t fp_rand(uint32_t amount)
{
    lcg_step();
    return (uint32_t)((uint64_t)g_seed * amount / 2147483647ULL);
}

real fp_choose2(real a, real b) { return fp_rand(2) ? b : a; }

real fp_approach(real v, real target, real amount)
{
    if (v < target) return (target < v + amount) ? target : v + amount;
    return (target > v - amount) ? target : v - amount;
}

real fp_lerp(real a, real b, real t) { return a + (b - a) * t; }

real fp_distance(real x1, real y1, real x2, real y2)
{
    real dx = x2 - x1, dy = y2 - y1;
    return rsqrt(dx * dx + dy * dy);
}

real fp_angle(real x1, real y1, real x2, real y2)
{
    real a = ratan2(y2 - y1, x2 - x1) * FP_DEG;
    return a < 0 ? a + 360 : a;
}

void fp_angle_xy(fp_vec *p, real angle_deg, real dist, real ox, real oy)
{
    real a = angle_deg * FP_RAD;
    p->x = rcos(a) * dist + ox;
    p->y = rsin(a) * dist + oy;
}

real fp_scale_clamp(real v, real lo, real hi, real a, real b)
{
    v = a + (v - lo) / (hi - lo) * (b - a);
    if (b > a) { v = v < b ? v : b; return v > a ? v : a; }
    v = v < a ? v : a;
    return v > b ? v : b;
}

real fp_clamp(real v, real lo, real hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

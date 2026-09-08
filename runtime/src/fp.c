#include "fp.h"

const double FP_RAD = FP_PI / -180.0;
const double FP_DEG = -180.0 / FP_PI;

int    fp_width = 0, fp_height = 0;
double fp_half_width = 0, fp_half_height = 0;
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
double fp_random(void)
{
    g_seed = (uint32_t)((uint64_t)g_seed * 16807ULL % 2147483647ULL);
    return (double)g_seed / 2147483647.0;
}

uint32_t fp_rand(uint32_t amount)
{
    return (uint32_t)(fp_random() * (double)amount);
}

double fp_approach(double v, double target, double amount)
{
    if (v < target) return (target < v + amount) ? target : v + amount;
    return (target > v - amount) ? target : v - amount;
}

double fp_lerp(double a, double b, double t) { return a + (b - a) * t; }

double fp_distance(double x1, double y1, double x2, double y2)
{
    return sqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1));
}

double fp_angle(double x1, double y1, double x2, double y2)
{
    double a = atan2(y2 - y1, x2 - x1) * FP_DEG;
    return a < 0 ? a + 360.0 : a;
}

void fp_angle_xy(fp_vec *p, double angle_deg, double dist, double ox, double oy)
{
    double a = angle_deg * FP_RAD;
    p->x = cos(a) * dist + ox;
    p->y = sin(a) * dist + oy;
}

double fp_scale_clamp(double v, double lo, double hi, double a, double b)
{
    v = a + (v - lo) / (hi - lo) * (b - a);
    if (b > a) { v = v < b ? v : b; return v > a ? v : a; }
    v = v < a ? v : a;
    return v > b ? v : b;
}

double fp_clamp(double v, double lo, double hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

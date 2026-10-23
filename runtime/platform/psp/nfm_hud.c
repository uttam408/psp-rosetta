/* nfm_hud.c — CPU-framebuffer HUD: sprites, bars and the checkpoint arrow. */
#include "nfm_hud.h"
#include "pak.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t *g_fb;
static int g_w, g_h;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t abgr(int r, int g, int b) { return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r; }

void hud_begin(uint32_t *fb, int w, int h) { g_fb = fb; g_w = w; g_h = h; }

int hud_load(HudTex *t, const char *name)
{
    char id[64]; snprintf(id, sizeof id, "image/%s", name);
    const PakAsset *a = pak_find(id);
    if (!a || a->size < 12 || memcmp(a->data, "PTX1", 4) != 0) return 0;
    const uint8_t *d = a->data;
    int w = rd16(d + 4), h = rd16(d + 6);
    if (d[8] != 1 || !(d[9] & 1) || rd16(d + 10) != 0 || (w & 7) || (h & 7)) return 0;   /* swizzled 5551 only */
    if (a->size < 12 + (size_t)w * h * 2) return 0;
    const uint8_t *s = d + 12;
    int rb = w * 2 / 16, cw = 0, ch = 0;
    uint32_t *full = malloc((size_t)w * h * 4);
    if (!full) return 0;
    for (int y = 0; y < h; y++)                     /* undo the 16-byte x 8-row swizzle while expanding to ABGR */
        for (int x = 0; x < w; x++) {
            int bx = x * 2, off = (((bx >> 4) + (y >> 3) * rb) << 7) + (bx & 15) + ((y & 7) << 4);
            unsigned v = s[off] | s[off + 1] << 8;
            if (v & 0x8000) {
                full[y * w + x] = abgr((v & 31) * 255 / 31, ((v >> 5) & 31) * 255 / 31, ((v >> 10) & 31) * 255 / 31);
                if (x + 1 > cw) cw = x + 1;
                if (y + 1 > ch) ch = y + 1;
            } else full[y * w + x] = 0;
        }
    if (cw == 0 || ch == 0) { free(full); return 0; }
    t->px = malloc((size_t)cw * ch * 4);
    if (!t->px) { free(full); return 0; }
    for (int y = 0; y < ch; y++) memcpy(t->px + y * cw, full + y * w, (size_t)cw * 4);
    free(full);
    t->w = cw; t->h = ch;
    return 1;
}

void hud_sprite(const HudTex *t, float x, float y, float s)
{
    if (!t || !t->px || !g_fb || s <= 0) return;
    int dw = (int)(t->w * s), dh = (int)(t->h * s), x0 = (int)x, y0 = (int)y;
    for (int dy = 0; dy < dh; dy++) {
        int py = y0 + dy;
        if (py < 0 || py >= g_h) continue;
        const uint32_t *row = t->px + (int)(dy / s) * t->w;
        uint32_t *out = g_fb + py * g_w;
        for (int dx = 0; dx < dw; dx++) {
            int pxx = x0 + dx;
            if (pxx < 0 || pxx >= g_w) continue;
            uint32_t c = row[(int)(dx / s)];
            if (c) out[pxx] = c;
        }
    }
}

void hud_rect(float x, float y, float w, float h, uint32_t rgb)
{
    if (!g_fb || w <= 0 || h <= 0) return;
    uint32_t c = abgr((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
    int x0 = (int)x, y0 = (int)y, x1 = (int)(x + w), y1 = (int)(y + h);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > g_w) x1 = g_w;
    if (y1 > g_h) y1 = g_h;
    for (int py = y0; py < y1; py++)
        for (int px = x0; px < x1; px++) g_fb[py * g_w + px] = c;
}

/* even-odd scanline fill of a small polygon */
static void fill_poly(const float *xy, int n, uint32_t c)
{
    float ymin = xy[1], ymax = xy[1];
    for (int i = 1; i < n; i++) { if (xy[2 * i + 1] < ymin) ymin = xy[2 * i + 1]; if (xy[2 * i + 1] > ymax) ymax = xy[2 * i + 1]; }
    int ya = (int)ymin, yb = (int)ymax + 1;
    if (ya < 0) ya = 0;
    if (yb > g_h) yb = g_h;
    for (int py = ya; py < yb; py++) {
        float sy = py + 0.5f, xs[16]; int k = 0;
        for (int i = 0; i < n && k < 15; i++) {
            const float *p = xy + 2 * i, *q = xy + 2 * ((i + 1) % n);
            if ((p[1] <= sy) == (q[1] <= sy)) continue;
            xs[k++] = p[0] + (sy - p[1]) * (q[0] - p[0]) / (q[1] - p[1]);
        }
        for (int i = 1; i < k; i++) { float v = xs[i]; int j = i - 1; while (j >= 0 && xs[j] > v) { xs[j + 1] = xs[j]; j--; } xs[j + 1] = v; }
        for (int i = 0; i + 1 < k; i += 2) {
            int xa = (int)(xs[i] + 0.5f), xb = (int)(xs[i + 1] + 0.5f);
            if (xa < 0) xa = 0;
            if (xb > g_w) xb = g_w;
            for (int px = xa; px < xb; px++) g_fb[py * g_w + px] = c;
        }
    }
}

void hud_arrow(float cx, float cy, float ang_deg, float size)
{
    static const float P[7][2] = { {0, -1}, {0.8f, 0.05f}, {0.32f, 0.05f}, {0.32f, 0.9f}, {-0.32f, 0.9f}, {-0.32f, 0.05f}, {-0.8f, 0.05f} };
    if (!g_fb) return;
    float a = ang_deg * 0.01745329252f, sn = sinf(a), cs = cosf(a);
    for (int pass = 0; pass < 2; pass++) {
        float k = pass == 0 ? 1.0f : 0.76f, xy[14];
        for (int i = 0; i < 7; i++) {
            float lx = P[i][0] * size * k, ly = (P[i][1] - (pass == 0 ? 0.0f : 0.06f)) * size * k;
            xy[2 * i] = cx + lx * cs - ly * sn;
            xy[2 * i + 1] = cy + lx * sn + ly * cs;
        }
        fill_poly(xy, 7, pass == 0 ? abgr(16, 16, 16) : abgr(255, 216, 32));
    }
}

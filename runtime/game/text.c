#include "text.h"
#include "game.h"
#include "../vendor/font8x8_basic.h"
#include <string.h>

#define GLYPH 8
#define ATLAS_W 128     /* 16 glyphs across */
#define ATLAS_H 64      /* 8 rows           */

static GfxTex *g_font;

void text_init(void)
{
    static uint16_t px[ATLAS_W * ATLAS_H];
    for (int c = 0; c < 128; c++) {
        int gx = (c % 16) * GLYPH, gy = (c / 16) * GLYPH;
        for (int row = 0; row < 8; row++) {
            unsigned bits = font8x8_basic[c][row];
            for (int col = 0; col < 8; col++)
                px[(gy + row) * ATLAS_W + (gx + col)] =
                    (bits & (1u << col)) ? 0xFFFF : 0x0000;   /* white / clear */
        }
    }
    g_font = gfx_tex_from_pixels(px, ATLAS_W, ATLAS_H);
}

static int line_len(const char *s)
{
    int n = 0;
    while (*s && *s != '\n') { n++; s++; }
    return n;
}

int text_width(const char *s)
{
    int best = 0;
    while (*s) {
        int n = line_len(s);
        if (n > best) best = n;
        s += n;
        if (*s == '\n') s++;
    }
    return best * GLYPH;
}

void text_draw(const char *s, double sx, double sy, uint32_t rgb, int align)
{
    if (!g_font) return;
    double lx = sx, ly = sy;
    const char *p = s;
    while (*p) {
        int n = line_len(p);
        double x = lx;
        if (align == TEXT_CENTER) x = lx - n * GLYPH / 2.0;
        else if (align == TEXT_RIGHT) x = lx - n * GLYPH;
        for (int i = 0; i < n; i++) {
            unsigned char c = (unsigned char)p[i];
            if (c > 127) c = '?';
            if (c != ' ') {
                int gx = (c % 16) * GLYPH, gy = (c / 16) * GLYPH;
                gfx_draw(g_font, fp_camera.x + x + i * GLYPH, fp_camera.y + ly, 0,
                         0, 0, 1, 1, rgb, gx, gy, GLYPH, GLYPH);
            }
        }
        p += n;
        if (*p == '\n') { p++; ly += GLYPH + 2; }
    }
}

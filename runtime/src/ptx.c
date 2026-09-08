#include "ptx.h"
#include <stdlib.h>
#include <string.h>

enum { FMT_8888 = 0, FMT_5551 = 1, FMT_4444 = 2, FMT_IDX8 = 3 };

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

bool ptx_decode(const uint8_t *data, size_t len, PtxImage *out)
{
    if (!data || len < 12 || memcmp(data, "PTX1", 4) != 0) return false;
    int w = rd16(data + 4);
    int h = rd16(data + 6);
    uint8_t fmt = data[8];
    uint8_t flags = data[9];
    uint16_t pal_count = rd16(data + 10);
    if (flags & 1) return false;              /* swizzled — not handled here */

    const uint8_t *pal = data + 12;
    const uint8_t *px = pal + pal_count * 4;
    size_t need_px;
    switch (fmt) {
        case FMT_8888: need_px = (size_t)w * h * 4; break;
        case FMT_5551:
        case FMT_4444: need_px = (size_t)w * h * 2; break;
        case FMT_IDX8: need_px = (size_t)w * h; break;
        default: return false;
    }
    if ((size_t)(px - data) + need_px > len) return false;

    uint8_t *rgba = malloc((size_t)w * h * 4);
    if (!rgba) return false;

    for (int i = 0; i < w * h; i++) {
        uint8_t r, g, b, a;
        if (fmt == FMT_8888) {
            r = px[i * 4]; g = px[i * 4 + 1]; b = px[i * 4 + 2]; a = px[i * 4 + 3];
        } else if (fmt == FMT_5551) {
            uint16_t v = rd16(px + i * 2);
            r = (uint8_t)(((v)       & 0x1F) << 3);
            g = (uint8_t)(((v >> 5)  & 0x1F) << 3);
            b = (uint8_t)(((v >> 10) & 0x1F) << 3);
            a = (v & 0x8000) ? 255 : 0;
        } else if (fmt == FMT_4444) {
            uint16_t v = rd16(px + i * 2);
            r = (uint8_t)(((v)       & 0xF) * 17);
            g = (uint8_t)(((v >> 4)  & 0xF) * 17);
            b = (uint8_t)(((v >> 8)  & 0xF) * 17);
            a = (uint8_t)(((v >> 12) & 0xF) * 17);
        } else { /* IDX8 */
            const uint8_t *e = pal + px[i] * 4;
            r = e[0]; g = e[1]; b = e[2]; a = e[3];
        }
        rgba[i * 4] = r; rgba[i * 4 + 1] = g; rgba[i * 4 + 2] = b; rgba[i * 4 + 3] = a;
    }
    out->w = w; out->h = h; out->rgba = rgba;
    return true;
}

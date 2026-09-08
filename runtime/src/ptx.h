/* ptx.h — decode a .ptx blob (see pipeline/convert/textures.py) to RGBA8888. */
#ifndef RT_PTX_H
#define RT_PTX_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
    int      w, h;
    uint8_t *rgba;   /* malloc'd, w*h*4, caller frees */
} PtxImage;

bool ptx_decode(const uint8_t *data, size_t len, PtxImage *out);

#endif

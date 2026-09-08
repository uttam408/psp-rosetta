/* pak.h — reads assets.pak (PAK1 TOC) + its "@index" entry (AIDX asset table).
 *
 * PAK1:  "PAK1" u32 count, then count * { u16 nlen, name, u32 off, u32 size },
 *        then the blob region (entries 4-byte aligned).
 * AIDX:  "AIDX" u32 count, then count * {
 *            u16 idlen, id,
 *            u8 kind (0=image 1=audio 2=data), u8 ptxfmt, u8 flags (bit0=sheet), u8 pad,
 *            u16 w, u16 h, u16 nframes,
 *            nframes * { u16 x, u16 y, u16 w, u16 h }      // atlas rects
 *        }
 */
#ifndef RT_PAK_H
#define RT_PAK_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define PAK_MAX_FRAMES 32

typedef struct {
    uint16_t x, y, w, h;
} PakRect;

typedef struct {
    char     id[96];
    uint8_t  kind;       /* 0 image, 1 audio, 2 data */
    uint8_t  ptxfmt;
    bool     sheet;
    uint16_t w, h;
    uint16_t nframes;
    PakRect  frames[PAK_MAX_FRAMES];
    const uint8_t *data; /* points into the mapped pak buffer */
    uint32_t size;
} PakAsset;

bool             pak_open(const char *path);
void             pak_close(void);
const PakAsset  *pak_find(const char *id);
int              pak_count(void);
const PakAsset  *pak_at(int i);

#endif

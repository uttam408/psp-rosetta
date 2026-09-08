#include "pak.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PAK_MAX_ASSETS 256

static uint8_t   *g_buf;
static size_t     g_len;
static PakAsset   g_assets[PAK_MAX_ASSETS];
static int        g_nassets;

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

/* locate a raw TOC entry's bytes by name */
static const uint8_t *toc_find(const char *name, uint32_t *out_size)
{
    if (!g_buf || memcmp(g_buf, "PAK1", 4) != 0) return NULL;
    uint32_t count = rd32(g_buf + 4);
    const uint8_t *p = g_buf + 8;
    for (uint32_t i = 0; i < count; i++) {
        uint16_t nlen = rd16(p); p += 2;
        const char *nm = (const char *)p; p += nlen;
        uint32_t off = rd32(p); p += 4;
        uint32_t size = rd32(p); p += 4;
        if ((size_t)nlen == strlen(name) && memcmp(nm, name, nlen) == 0) {
            if (out_size) *out_size = size;
            return g_buf + off;
        }
    }
    return NULL;
}

static void parse_index(void)
{
    uint32_t sz = 0;
    const uint8_t *p = toc_find("@index", &sz);
    if (!p || sz < 8 || memcmp(p, "AIDX", 4) != 0) return;
    uint32_t count = rd32(p + 4);
    p += 8;
    for (uint32_t i = 0; i < count && g_nassets < PAK_MAX_ASSETS; i++) {
        PakAsset *a = &g_assets[g_nassets];
        memset(a, 0, sizeof *a);
        uint16_t idlen = rd16(p); p += 2;
        size_t n = idlen < sizeof a->id - 1 ? idlen : sizeof a->id - 1;
        memcpy(a->id, p, n); a->id[n] = 0; p += idlen;
        a->kind   = p[0];
        a->ptxfmt = p[1];
        a->sheet  = (p[2] & 1) != 0;
        p += 4;
        a->w = rd16(p); p += 2;
        a->h = rd16(p); p += 2;
        a->nframes = rd16(p); p += 2;
        uint16_t nf = a->nframes < PAK_MAX_FRAMES ? a->nframes : PAK_MAX_FRAMES;
        for (uint16_t f = 0; f < a->nframes; f++) {
            if (f < nf) {
                a->frames[f].x = rd16(p);
                a->frames[f].y = rd16(p + 2);
                a->frames[f].w = rd16(p + 4);
                a->frames[f].h = rd16(p + 6);
            }
            p += 8;
        }
        a->data = toc_find(a->id, &a->size);
        g_nassets++;
    }
}

bool pak_open(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "pak: cannot open %s\n", path); return false; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 8) { fclose(f); return false; }
    g_buf = malloc((size_t)len);
    g_len = fread(g_buf, 1, (size_t)len, f);
    fclose(f);
    if (g_len != (size_t)len || memcmp(g_buf, "PAK1", 4) != 0) {
        fprintf(stderr, "pak: bad file %s\n", path);
        pak_close();
        return false;
    }
    g_nassets = 0;
    parse_index();
    return true;
}

void pak_close(void)
{
    free(g_buf);
    g_buf = NULL;
    g_len = 0;
    g_nassets = 0;
}

const PakAsset *pak_find(const char *id)
{
    for (int i = 0; i < g_nassets; i++)
        if (strcmp(g_assets[i].id, id) == 0) return &g_assets[i];
    return NULL;
}

int pak_count(void) { return g_nassets; }
const PakAsset *pak_at(int i) { return (i >= 0 && i < g_nassets) ? &g_assets[i] : NULL; }

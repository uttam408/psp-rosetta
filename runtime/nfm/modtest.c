/* modtest.c — desktop check for modplay: modtest <file.mod> [out.wav] [seconds]
 * Prints load info, song length until the order list wraps, peak/RMS and clip count; optionally writes a WAV. */
#include "modplay.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: modtest file.mod [out.wav] [seconds]\n"); return 2; }
    FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(n); if (fread(buf, 1, n, f) != (size_t)n) return 1; fclose(f);
    static ModPlayer m;
    if (!modplay_load(&m, buf, (uint32_t)n)) { printf("%s: LOAD FAILED\n", argv[1]); return 1; }
    int secs = argc > 3 ? atoi(argv[3]) : 0;
    const int chunk = 1024;
    int16_t *pcm = NULL; long total = 0, cap = 0, clip = 0, wrapped_at = -1; double sq = 0; int peak = 0, maxord = 0;
    for (long fr = 0; ; fr += chunk) {
        int16_t tmp[1024 * 2];
        modplay_render(&m, tmp, chunk);
        for (int i = 0; i < chunk * 2; i++) { int v = tmp[i]; int a = v < 0 ? -v : v; if (a > peak) peak = a; if (a >= 32767) clip++; sq += (double)v * v; }
        if (argc > 2) {
            if (total + chunk * 2 > cap) { cap = (cap + chunk * 2) * 2; pcm = realloc(pcm, cap * 2); }
            memcpy(pcm + total, tmp, chunk * 4);
        }
        total += chunk * 2;
        if (m.ord > maxord) maxord = m.ord;
        if (wrapped_at < 0 && m.ord < maxord) wrapped_at = fr + chunk;
        long limit = secs ? (long)secs * MODPLAY_RATE : 20L * 60 * MODPLAY_RATE;
        if ((secs && fr >= limit) || (!secs && (wrapped_at >= 0 || fr >= limit))) break;
    }
    printf("%s: orders %d, patterns %d, length %.1fs, peak %d, rms %.0f, clipped %ld\n", argv[1], m.song_len, m.npat,
           wrapped_at >= 0 ? wrapped_at / (double)MODPLAY_RATE : -1.0, peak, sqrt(sq / total), clip);
    if (argc > 2 && pcm) {
        FILE *w = fopen(argv[2], "wb"); uint32_t db = (uint32_t)total * 2, v32;
        fwrite("RIFF", 1, 4, w); v32 = 36 + db; fwrite(&v32, 4, 1, w); fwrite("WAVEfmt ", 1, 8, w);
        v32 = 16; fwrite(&v32, 4, 1, w); uint16_t v16 = 1; fwrite(&v16, 2, 1, w); v16 = 2; fwrite(&v16, 2, 1, w);
        v32 = MODPLAY_RATE; fwrite(&v32, 4, 1, w); v32 = MODPLAY_RATE * 4; fwrite(&v32, 4, 1, w);
        v16 = 4; fwrite(&v16, 2, 1, w); v16 = 16; fwrite(&v16, 2, 1, w); fwrite("data", 1, 4, w); fwrite(&db, 4, 1, w);
        fwrite(pcm, 2, total, w); fclose(w);
    }
    return 0;
}

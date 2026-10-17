/* modplay.h — small ProTracker (.mod, 4-channel "M.K.") replayer: renders interleaved stereo s16 at MODPLAY_RATE.
 * The module bytes are read in place (no copy), so they must outlive the player. Loops forever. */
#ifndef NFM_MODPLAY_H
#define NFM_MODPLAY_H

#include <stdint.h>

#define MODPLAY_RATE 44100

typedef struct {
    const uint8_t *data; uint32_t size;
    const uint8_t *pat;                         /* pattern data (64 rows x 4 ch x 4 bytes per pattern) */
    struct { const int8_t *pcm; int len, loop_start, loop_len, vol, fine; } smp[31];   /* lengths/loops in bytes */
    uint8_t order[128]; int song_len, npat;
    int speed, tempo, tick, row, ord, brk_ord, brk_row, delay_rows, samples_left;
    int loop_row[4], loop_cnt[4];
    struct {
        int smp, note, period, target, vol, vib_pos, vib_cmd, port_spd, off, cmd, par, delay_note, delay_smp;
        uint32_t pos, step; int active;
    } ch[4];
    float tick_frac;
    int gain;                                   /* 0..256 */
} ModPlayer;

int  modplay_load(ModPlayer *m, const uint8_t *data, uint32_t size);   /* 1 on success */
void modplay_render(ModPlayer *m, int16_t *out, int frames);            /* overwrites out */

#endif

/* nfm_audio_psp.c — pspaudiolib channel 0 pulls stereo s16: the MOD player plus a few one-shot PCM voices. */
#include "nfm_audio_psp.h"
#include "../../nfm/modplay.h"
#include <pspaudiolib.h>
#include <pspkernel.h>
#include <string.h>

#define NVOICE 6
#define SFX_RATE 22050      /* shipped clips are 22.05 kHz mono s16le; the output is 44.1 kHz */

typedef struct { const int16_t *pcm; uint32_t n, pos2; int vol; } Voice;   /* pos2 counts output frames: sample = pos2 / 2 */

static ModPlayer g_mod;
static volatile int g_on;
static Voice g_v[NVOICE];

static void cb(void *buf, unsigned int frames, void *userdata)
{
    (void)userdata;
    int16_t *o = (int16_t *)buf;
    if (g_on) modplay_render(&g_mod, o, (int)frames);
    else memset(buf, 0, (size_t)frames * 4);
    for (int v = 0; v < NVOICE; v++) {
        Voice *s = &g_v[v];
        if (!s->pcm) continue;
        for (unsigned i = 0; i < frames; i++) {
            uint32_t k = s->pos2 >> 1;
            if (k >= s->n) { s->pcm = 0; break; }
            int a = s->pcm[k], b = k + 1 < s->n ? s->pcm[k + 1] : a;
            int x = ((s->pos2 & 1) ? (a + b) / 2 : a) * s->vol / 256;
            int l = o[2 * i] + x, r = o[2 * i + 1] + x;
            o[2 * i] = (int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l);
            o[2 * i + 1] = (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r);
            s->pos2++;
        }
    }
}

int nfm_audio_init(void)
{
    if (pspAudioInit() != 0) return 0;          /* fixed 44100 Hz stereo, matches MODPLAY_RATE */
    pspAudioSetChannelCallback(0, cb, 0);
    return 1;
}

int nfm_music_play(const uint8_t *mod, uint32_t size)
{
    g_on = 0;
    sceKernelDelayThread(60000);                /* let an in-flight callback finish before the player is rewritten */
    if (!mod || !modplay_load(&g_mod, mod, size)) return 0;
    g_on = 1;
    return 1;
}

void nfm_sfx_play(const uint8_t *pcm, uint32_t bytes, int vol)
{
    if (!pcm || bytes < 2) return;
    int slot = 0;
    for (int v = 0; v < NVOICE; v++) if (!g_v[v].pcm) { slot = v; break; }   /* all busy: steal voice 0 */
    Voice nv = { (const int16_t *)pcm, bytes / 2, 0, vol };
    g_v[slot].pcm = 0;                          /* silence the slot first so the callback never sees a half-written voice */
    g_v[slot].n = nv.n; g_v[slot].pos2 = 0; g_v[slot].vol = nv.vol;
    g_v[slot].pcm = nv.pcm;
}

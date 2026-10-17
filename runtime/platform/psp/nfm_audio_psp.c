/* nfm_audio_psp.c — pspaudiolib channel 0 pulls stereo s16 from the MOD player. */
#include "nfm_audio_psp.h"
#include "../../nfm/modplay.h"
#include <pspaudiolib.h>
#include <pspkernel.h>
#include <string.h>

static ModPlayer g_mod;
static volatile int g_on;

static void cb(void *buf, unsigned int frames, void *userdata)
{
    (void)userdata;
    if (g_on) modplay_render(&g_mod, (int16_t *)buf, (int)frames);
    else memset(buf, 0, (size_t)frames * 4);
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

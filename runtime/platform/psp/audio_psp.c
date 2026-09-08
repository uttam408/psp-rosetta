/* audio_psp.c — pspaudiolib (44100/stereo) feeding the shared mixer. */
#include "../../src/audio_mix.h"
#include <pspaudiolib.h>

static void cb(void *buf, unsigned int frames, void *userdata)
{
    (void)userdata;
    audio_mix_pull((int16_t *)buf, (int)frames);
}

int snd_dev_open(void)
{
    if (pspAudioInit() != 0) return 0;          /* fixed 44100 Hz stereo */
    pspAudioSetChannelCallback(0, cb, 0);
    return 1;
}

void snd_dev_close(void)
{
    pspAudioSetChannelCallback(0, 0, 0);
    pspAudioEnd();
}

/* audio_sdl.c — SDL2 audio device feeding the shared mixer. */
#include "../../src/audio_mix.h"
#include <SDL.h>

static SDL_AudioDeviceID g_dev;

static void cb(void *u, Uint8 *stream, int len)
{
    (void)u;
    audio_mix_pull((int16_t *)stream, len / 4);   /* 4 bytes / stereo frame */
}

int snd_dev_open(void)
{
    if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) return 0;
    SDL_AudioSpec want = {0}, have;
    want.freq = MIX_RATE;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = cb;
    g_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (g_dev) SDL_PauseAudioDevice(g_dev, 0);
    return g_dev != 0;
}

void snd_dev_close(void)
{
    if (g_dev) { SDL_CloseAudioDevice(g_dev); g_dev = 0; }
}

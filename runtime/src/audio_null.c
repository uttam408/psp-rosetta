#include "audio.h"

/* No-op audio backend. Replace with sceAudio / SDL_mixer later. */
void snd_init(void) {}
void snd_shutdown(void) {}
void snd_play(const char *id, float vol, float pan) { (void)id; (void)vol; (void)pan; }
void snd_music(const char *id, float vol) { (void)id; (void)vol; }
void snd_music_volume(float vol) { (void)vol; }
void snd_music_stop(void) {}
void snd_master_mute(int muted) { (void)muted; }

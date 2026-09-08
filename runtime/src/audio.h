/* audio.h — SFX + music. Backend-provided; audio_null.c is a working no-op so
 * the game runs before sceAudio / SDL_mixer are wired. */
#ifndef RT_AUDIO_H
#define RT_AUDIO_H

void snd_init(void);
void snd_shutdown(void);
void snd_play(const char *id, float vol, float pan);  /* one-shot SFX by pak id */
void snd_music(const char *id, float vol);            /* start looping music    */
void snd_music_volume(float vol);
void snd_music_stop(void);
void snd_master_mute(int muted);

#endif

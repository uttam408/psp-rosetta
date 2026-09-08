/* audio_mix.h — software mixer shared by both platforms.
 * SFX are the pipeline's raw s16 mono PCM (44.1 kHz); music is its MP3 blob,
 * streamed with dr_mp3. The platform opens a 44100/stereo device and pumps
 * audio_mix_pull() from its callback. */
#ifndef RT_AUDIO_MIX_H
#define RT_AUDIO_MIX_H

#include <stdint.h>

#define MIX_RATE 44100

/* fill `frames` interleaved stereo s16 samples */
void audio_mix_pull(int16_t *out, int frames);

/* provided by platform/<x>/audio_<x>.c */
int  snd_dev_open(void);
void snd_dev_close(void);

#endif

#ifndef NFM_AUDIO_PSP_H
#define NFM_AUDIO_PSP_H
#include <stdint.h>
int nfm_audio_init(void);
int nfm_music_play(const uint8_t *mod, uint32_t size);   /* mod = NULL stops; the bytes must stay mapped (pak) */
void nfm_sfx_play(const uint8_t *pcm, uint32_t bytes, int vol);   /* one-shot s16le mono 22.05 kHz, vol 0..256; bytes must stay mapped */
#endif

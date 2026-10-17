#ifndef NFM_AUDIO_PSP_H
#define NFM_AUDIO_PSP_H
#include <stdint.h>
int nfm_audio_init(void);
int nfm_music_play(const uint8_t *mod, uint32_t size);   /* mod = NULL stops; the bytes must stay mapped (pak) */
#endif

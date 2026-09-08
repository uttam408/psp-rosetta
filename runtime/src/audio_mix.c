#include "audio.h"
#include "audio_mix.h"
#include "pak.h"
#include "../vendor/dr_mp3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define VOICES     8
#define SFX_CACHE  24

/* --- sfx clips (raw s16 mono @ MIX_RATE, straight from the pak) ------------ */
typedef struct { char id[96]; const int16_t *pcm; int frames; } Clip;
static Clip  g_clips[SFX_CACHE];
static int   g_nclips;

static const Clip *clip_get(const char *id)
{
    for (int i = 0; i < g_nclips; i++)
        if (strcmp(g_clips[i].id, id) == 0) return &g_clips[i];
    const PakAsset *a = pak_find(id);
    if (!a || a->kind != 1 || g_nclips >= SFX_CACHE) return NULL;
    Clip *c = &g_clips[g_nclips++];
    snprintf(c->id, sizeof c->id, "%s", id);
    c->pcm = (const int16_t *)a->data;      /* .pcm is headerless s16le */
    c->frames = (int)(a->size / 2);
    return c;
}

/* --- voices -------------------------------------------------------------- */
typedef struct {
    const int16_t *pcm;
    int frames, pos;
    float lvol, rvol;
    volatile int active;
} Voice;
static Voice g_voice[VOICES];

/* --- music ------------------------------------------------------------- */
static drmp3           g_mp3;
static volatile int    g_music_on;
static volatile float  g_music_gain = 0.5f;
static float           g_master = 1.0f;

/* --- public API (audio.h) -------------------------------------------------- */
void snd_init(void)   { memset(g_voice, 0, sizeof g_voice); snd_dev_open(); }
void snd_shutdown(void)
{
    snd_dev_close();
    if (g_music_on) { drmp3_uninit(&g_mp3); g_music_on = 0; }
}

void snd_play(const char *id, float vol, float pan)
{
    const Clip *c = clip_get(id);
    if (!c || !c->pcm) return;
    int slot = -1, oldest = 0, oldpos = -1;
    for (int i = 0; i < VOICES; i++) {
        if (!g_voice[i].active) { slot = i; break; }
        if (g_voice[i].pos > oldpos) { oldpos = g_voice[i].pos; oldest = i; }
    }
    if (slot < 0) slot = oldest;
    Voice *v = &g_voice[slot];
    v->active = 0;                                  /* park before rewrite */
    v->pcm = c->pcm; v->frames = c->frames; v->pos = 0;
    v->lvol = vol * (pan > 0 ? 1.0f - pan : 1.0f);
    v->rvol = vol * (pan < 0 ? 1.0f + pan : 1.0f);
    v->active = 1;
}

void snd_music(const char *id, float vol)
{
    const PakAsset *a = pak_find(id);
    if (!a || a->kind != 1) return;
    if (g_music_on) { g_music_on = 0; drmp3_uninit(&g_mp3); }
    if (drmp3_init_memory(&g_mp3, a->data, a->size, NULL)) {
        g_music_gain = vol;
        g_music_on = 1;
    }
}
void snd_music_volume(float vol) { g_music_gain = vol; }
void snd_music_stop(void)        { g_music_on = 0; }
void snd_master_mute(int muted)  { g_master = muted ? 0.0f : 1.0f; }

/* --- the mix ----------------------------------------------------------- */
static inline int16_t clip16(int v)
{
    return v < -32768 ? -32768 : (v > 32767 ? 32767 : (int16_t)v);
}

void audio_mix_pull(int16_t *out, int frames)
{
    memset(out, 0, (size_t)frames * 4);

    if (g_music_on && g_master > 0.0f) {
        static int16_t tmp[2048 * 2];
        int done = 0;
        while (done < frames) {
            int want = frames - done;
            if (want > 2048) want = 2048;
            drmp3_uint64 got = drmp3_read_pcm_frames_s16(&g_mp3, want, tmp);
            if (got == 0) { drmp3_seek_to_pcm_frame(&g_mp3, 0); continue; }
            float g = g_music_gain * g_master;
            for (drmp3_uint64 i = 0; i < got * 2; i++)
                out[done * 2 + i] = clip16((int)(tmp[i] * g));
            done += (int)got;
        }
    }

    for (int i = 0; i < VOICES; i++) {
        Voice *v = &g_voice[i];
        if (!v->active) continue;
        float lg = v->lvol * g_master, rg = v->rvol * g_master;
        int n = frames;
        if (v->pos + n > v->frames) n = v->frames - v->pos;
        for (int f = 0; f < n; f++) {
            int s = v->pcm[v->pos + f];
            out[(f) * 2]     = clip16(out[f * 2]     + (int)(s * lg));
            out[(f) * 2 + 1] = clip16(out[f * 2 + 1] + (int)(s * rg));
        }
        v->pos += n;
        if (v->pos >= v->frames) v->active = 0;
    }
}

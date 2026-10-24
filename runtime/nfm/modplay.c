/* modplay.c — ProTracker 4-channel MOD replayer (see modplay.h). Effects: arpeggio, slides, tone portamento,
 * vibrato (+vol-slide combos), sample offset, vol slide, position jump/pattern break/loop/delay, set volume,
 * speed/tempo, fine slides/volume, retrigger, note cut/delay.  Tremolo and the waveform-select effects are ignored. */
#include "modplay.h"
#include <math.h>
#include <string.h>

static const int period0[36] = {
    856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
    428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
    214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113,
};
static int sine_tab[32];
static int tab_ready;

static int be16(const uint8_t *p) { return (p[0] << 8) | p[1]; }

static int note_of(int period)
{
    int best = 0, bd = 1 << 30;
    for (int i = 0; i < 36; i++) { int d = period - period0[i]; if (d < 0) d = -d; if (d < bd) { bd = d; best = i; } }
    return best;
}
static int period_ft(int note, int fine)
{
    if (note > 35) note = 35;
    if (note < 0) note = 0;
    int ft = fine > 7 ? fine - 16 : fine;
    return ft ? (int)(period0[note] * exp2f(-ft / 96.0f) + 0.5f) : period0[note];
}

int modplay_load(ModPlayer *m, const uint8_t *d, uint32_t size)
{
    if (!tab_ready) { for (int i = 0; i < 32; i++) sine_tab[i] = (int)(255.0f * sinf(3.14159265f * i / 32.0f) + 0.5f); tab_ready = 1; }
    memset(m, 0, sizeof *m);
    if (size < 1084) return 0;
    if (memcmp(d + 1080, "M.K.", 4) && memcmp(d + 1080, "M!K!", 4) && memcmp(d + 1080, "4CHN", 4) && memcmp(d + 1080, "FLT4", 4)) return 0;
    m->data = d; m->size = size;
    m->song_len = d[950]; if (m->song_len < 1 || m->song_len > 128) m->song_len = 128;
    int maxp = 0;
    for (int i = 0; i < 128; i++) { m->order[i] = d[952 + i] & 127; if (m->order[i] > maxp) maxp = m->order[i]; }
    m->npat = maxp + 1;
    uint32_t pat_end = 1084u + (uint32_t)m->npat * 1024u;
    if (pat_end > size) return 0;
    m->pat = d + 1084;
    uint32_t off = pat_end;
    for (int i = 0; i < 31; i++) {
        const uint8_t *h = d + 20 + i * 30;
        int len = be16(h + 22) * 2, ls = be16(h + 26) * 2, ll = be16(h + 28) * 2;
        if (off + (uint32_t)len > size) len = (int)(size > off ? size - off : 0);
        m->smp[i].pcm = (const int8_t *)(d + off);
        m->smp[i].len = len; m->smp[i].fine = h[24] & 15; m->smp[i].vol = h[25] > 64 ? 64 : h[25];
        if (ll > 2 && ls < len) { if (ls + ll > len) ll = len - ls; m->smp[i].loop_start = ls; m->smp[i].loop_len = ll; }
        off += (uint32_t)len;
    }
    m->speed = 6; m->tempo = 125; m->gain = 170;
    m->brk_ord = m->brk_row = -1;
    return 1;
}

static void trigger(ModPlayer *m, int c, int period, int cmd)
{
    (void)m;
    int ft = m->ch[c].smp ? m->smp[m->ch[c].smp - 1].fine : 0;
    int note = note_of(period);
    if (cmd == 3 || cmd == 5) { m->ch[c].target = period_ft(note, ft); return; }
    m->ch[c].note = note; m->ch[c].period = period_ft(note, ft);
    m->ch[c].pos = 0; m->ch[c].active = m->ch[c].smp != 0; m->ch[c].vib_pos = 0;
}

static int clampp(int p) { return p < 113 ? 113 : (p > 856 ? 856 : p); }

static void row_start(ModPlayer *m)
{
    const uint8_t *p = m->pat + ((int)m->order[m->ord] * 64 + m->row) * 16;
    for (int c = 0; c < 4; c++) {
        const uint8_t *q = p + c * 4;
        int sn = (q[0] & 0xf0) | (q[2] >> 4), period = ((q[0] & 15) << 8) | q[1], cmd = q[2] & 15, par = q[3];
        m->ch[c].cmd = cmd; m->ch[c].par = par;
        if (sn >= 1 && sn <= 31) { m->ch[c].smp = sn; m->ch[c].vol = m->smp[sn - 1].vol; }
        int ecmd = cmd == 14 ? par >> 4 : -1, ex = par & 15;
        if (ecmd == 13 && ex > 0) m->ch[c].delay_note = period;
        else if (period) {
            trigger(m, c, period, cmd);
            if (cmd == 9 && par) m->ch[c].off = par;
            if (cmd == 9) m->ch[c].pos = (uint32_t)m->ch[c].off << 24;
        }
        switch (cmd) {
        case 3: if (par) m->ch[c].port_spd = par; break;
        case 4:
            if (par & 0xf0) m->ch[c].vib_cmd = (m->ch[c].vib_cmd & 15) | (par & 0xf0);
            if (par & 15) m->ch[c].vib_cmd = (m->ch[c].vib_cmd & 0xf0) | (par & 15);
            break;
        case 11: m->brk_ord = par; break;
        case 12: m->ch[c].vol = par > 64 ? 64 : par; break;
        case 13: { int r = (par >> 4) * 10 + (par & 15); m->brk_row = r > 63 ? 0 : r; break; }
        case 15: if (par) { if (par < 32) m->speed = par; else m->tempo = par; } break;
        case 14:
            switch (ecmd) {
            case 1: m->ch[c].period = clampp(m->ch[c].period - ex); break;
            case 2: m->ch[c].period = clampp(m->ch[c].period + ex); break;
            case 6:
                if (ex == 0) m->loop_row[c] = m->row;
                else {
                    if (m->loop_cnt[c] == 0) m->loop_cnt[c] = ex; else m->loop_cnt[c]--;
                    if (m->loop_cnt[c] > 0) { m->brk_ord = m->ord; m->brk_row = m->loop_row[c]; }
                }
                break;
            case 10: m->ch[c].vol += ex; if (m->ch[c].vol > 64) m->ch[c].vol = 64; break;
            case 11: m->ch[c].vol -= ex; if (m->ch[c].vol < 0) m->ch[c].vol = 0; break;
            case 12: if (ex == 0) m->ch[c].vol = 0; break;
            case 14: m->delay_rows = ex; break;
            }
            break;
        }
    }
}

static void vol_slide(ModPlayer *m, int c)
{
    int par = m->ch[c].par, up = par >> 4, dn = par & 15;
    m->ch[c].vol += up ? up : -dn;
    if (m->ch[c].vol < 0) m->ch[c].vol = 0;
    if (m->ch[c].vol > 64) m->ch[c].vol = 64;
}

static void tick_fx(ModPlayer *m)
{
    for (int c = 0; c < 4; c++) {
        int cmd = m->ch[c].cmd, par = m->ch[c].par;
        switch (cmd) {
        case 1: m->ch[c].period = clampp(m->ch[c].period - par); break;
        case 2: m->ch[c].period = clampp(m->ch[c].period + par); break;
        case 3: case 5:
            if (m->ch[c].period < m->ch[c].target) { m->ch[c].period += m->ch[c].port_spd; if (m->ch[c].period > m->ch[c].target) m->ch[c].period = m->ch[c].target; }
            else if (m->ch[c].period > m->ch[c].target) { m->ch[c].period -= m->ch[c].port_spd; if (m->ch[c].period < m->ch[c].target) m->ch[c].period = m->ch[c].target; }
            if (cmd == 5) vol_slide(m, c);
            break;
        case 4: case 6:
            m->ch[c].vib_pos = (m->ch[c].vib_pos + (m->ch[c].vib_cmd >> 4)) & 63;
            if (cmd == 6) vol_slide(m, c);
            break;
        case 10: vol_slide(m, c); break;
        case 14: {
            int e = par >> 4, x = par & 15;
            if (e == 9 && x && m->tick % x == 0) m->ch[c].pos = 0;
            else if (e == 12 && m->tick == x) m->ch[c].vol = 0;
            else if (e == 13 && m->tick == x && m->ch[c].delay_note) { trigger(m, c, m->ch[c].delay_note, 0); m->ch[c].delay_note = 0; }
            break;
        }
        }
    }
}

static void do_tick(ModPlayer *m)
{
    if (m->tick == 0) row_start(m); else tick_fx(m);
    for (int c = 0; c < 4; c++) {
        int per = m->ch[c].period;
        if (m->ch[c].cmd == 0 && m->ch[c].par) {
            int ft = m->ch[c].smp ? m->smp[m->ch[c].smp - 1].fine : 0, a = m->tick % 3;
            per = period_ft(m->ch[c].note + (a == 0 ? 0 : a == 1 ? (m->ch[c].par >> 4) : (m->ch[c].par & 15)), ft);
        } else if (m->ch[c].cmd == 4 || m->ch[c].cmd == 6) {
            int v = sine_tab[m->ch[c].vib_pos & 31] * (m->ch[c].vib_cmd & 15) >> 7;
            per += (m->ch[c].vib_pos & 32) ? -v : v;
        }
        if (per < 54) per = 54;
        m->ch[c].step = (uint32_t)(3546894.6 * 65536.0 / ((double)per * MODPLAY_RATE));
    }
    m->tick++;
    if (m->tick >= m->speed) {
        if (m->delay_rows > 0) { m->delay_rows--; m->tick = 1; return; }
        m->tick = 0;
        int nrow = m->row + 1, nord = m->ord;
        if (m->brk_ord >= 0 || m->brk_row >= 0) { nord = m->brk_ord >= 0 ? m->brk_ord : m->ord + 1; nrow = m->brk_row >= 0 ? m->brk_row : 0; }
        else if (nrow >= 64) { nord++; nrow = 0; }
        if (nord >= m->song_len) { nord = 0; if (m->brk_ord < 0) nrow = nrow >= 64 ? 0 : nrow; }
        m->brk_ord = m->brk_row = -1;
        if (nord != m->ord) memset(m->loop_cnt, 0, sizeof m->loop_cnt);
        m->ord = nord; m->row = nrow > 63 ? 0 : nrow;
    }
}

void modplay_render(ModPlayer *m, int16_t *out, int frames)
{
    int done = 0;
    while (done < frames) {
        if (m->samples_left <= 0) {
            do_tick(m);
            m->tick_frac += 110250.0f / (float)m->tempo;
            m->samples_left = (int)m->tick_frac; m->tick_frac -= (float)m->samples_left;
            if (m->samples_left < 1) m->samples_left = 1;
        }
        int n = frames - done; if (n > m->samples_left) n = m->samples_left; if (n > 512) n = 512;
        int32_t L[512], R[512];
        memset(L, 0, n * sizeof *L); memset(R, 0, n * sizeof *R);
        for (int c = 0; c < 4; c++) {
            if (!m->ch[c].active || !m->ch[c].smp) continue;
            const int8_t *pcm = m->smp[m->ch[c].smp - 1].pcm; int slen = m->smp[m->ch[c].smp - 1].len, lstart = m->smp[m->ch[c].smp - 1].loop_start, llen = m->smp[m->ch[c].smp - 1].loop_len;
            int vol = m->ch[c].vol;
            int lm = (c == 0 || c == 3) ? 256 : 77, rm = (c == 0 || c == 3) ? 77 : 256;
            uint32_t pos = m->ch[c].pos, step = m->ch[c].step;
            int end = llen ? lstart + llen : slen, active = 1;
            for (int i = 0; i < n; i++) {
                int idx = (int)(pos >> 16);
                if (idx >= end) {
                    if (llen) { while (idx >= end) { pos -= (uint32_t)llen << 16; idx = (int)(pos >> 16); } }
                    else { active = 0; break; }
                }
                int a = pcm[idx], nx = idx + 1;
                if (nx >= end) nx = llen ? lstart : idx;
                int b = pcm[nx], v = a + (((b - a) * (int)((pos >> 8) & 255)) >> 8);
                v *= vol;
                L[i] += (v * lm) >> 8; R[i] += (v * rm) >> 8;
                pos += step;
            }
            m->ch[c].pos = pos; m->ch[c].active = active;
        }
        for (int i = 0; i < n; i++) {
            int l = (L[i] * m->gain) >> 8, r = (R[i] * m->gain) >> 8;
            out[(done + i) * 2]     = (int16_t)(l < -32768 ? -32768 : l > 32767 ? 32767 : l);
            out[(done + i) * 2 + 1] = (int16_t)(r < -32768 ? -32768 : r > 32767 ? 32767 : r);
        }
        done += n; m->samples_left -= n;
    }
}

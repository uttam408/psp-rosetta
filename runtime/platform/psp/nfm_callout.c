#include "nfm_callout.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Port of the call-out half of xtGraphics.stat.  Counters keep the original's names. */
static int tcnt = 30, auscnt = 45, pwcnt, cntan, cntovn, s_clear = -1, s_dested[8];
static int flk, tflk, aflk, pwflk, wasay;
static char say[96], asay[96];
static unsigned rng = 12345;
static CalloutLine out[6];
static int nout;

#define RGB(r, g, b) (0xFF000000u | ((uint32_t)(b) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(r))

static const char *const adj[5][3] = {
    { "Cool", "Alright", "Nice" }, { "Wicked", "Amazing", "Super" }, { "Awesome", "Ripping", "Radical" },
    { "What the...?", "You're a super star!!!!", "Who are you again...?" },
    { "surf style", "off the lip", "bounce back" } };
static const char *const exlm[3] = { "!", "!!", "!!!" };

void callout_reset(void)
{
    tcnt = 30; auscnt = 45; pwcnt = cntan = cntovn = 0; s_clear = -1; wasay = 0;
    say[0] = asay[0] = 0; memset(s_dested, 0, sizeof s_dested); nout = 0;
}

static int pick3(void) { rng = rng * 1664525u + 1013904223u; return (int)((rng >> 16) % 3); }

static void put(int native_y, const char *s, uint32_t col)
{
    if (nout >= 6) return;
    snprintf(out[nout].text, sizeof out[nout].text, "%s", s);
    out[nout].row = native_y * 270 / 450 / 8;
    out[nout].col = col;
    nout++;
}

int callout_get(CalloutLine *o, int max)
{
    int n = nout < max ? nout : max;
    memcpy(o, out, n * sizeof *o);
    return n;
}

static void cat(char *d, size_t n, const char *s) { size_t l = strlen(d); snprintf(d + l, n - l, "%s", s); }

int callout_tick(Mad *m, CheckPoints *cp, int nai, float ana, int racing, const char *const *ai_names)
{
    int stunt = 0;
    nout = 0;
    if (s_clear < 0) s_clear = m->clear;
    if (racing) {
        /* arrow pointing at the track (not at cars): missed checkpoint, wrong way */
        if (auscnt == 45 && m->capcnt == 0) {
            if (m->missedcp > 0) {
                if (m->missedcp > 15 && m->missedcp < 50) put(70, "Checkpoint Missed!", flk ? RGB(255, 0, 0) : RGB(255, 150, 0));
                flk = !flk;
                if (++m->missedcp == 70) m->missedcp = -2;
            } else if (m->mtouch && cntovn < 70) {
                if (fabsf(ana) > 100) cntan++; else if (cntan) cntan--;
                if (cntan > 40) {
                    cntovn++; cntan = 40;
                    put(70, "Wrong Way!", flk ? RGB(255, 150, 0) : RGB(255, 0, 0));
                    flk = !flk;
                }
            }
        }
        if (m->power < 45.0f) {
            if (tcnt == 30 && auscnt == 45 && m->mtouch && m->capcnt == 0) {
                if (pwcnt < 70) { put(110, "Power low, perform stunt!", pwflk ? RGB(60, 120, 255) : RGB(255, 100, 0)); pwflk = !pwflk; }
                if (++pwcnt == 300) pwcnt = 0;
            }
        } else if (pwcnt) pwcnt = 0;
    }
    if (m->capcnt == 0) {
        if (tcnt < 30) {
            put(105, say, tflk ? RGB(255, 255, 255) : (wasay ? RGB(255, 128, 0) : RGB(0, 128, 255)));
            tflk = !tflk; tcnt++;
        } else if (wasay) wasay = 0;
        if (auscnt < 45) {
            put(85, asay, aflk ? RGB(98, 176, 255) : RGB(0, 128, 255));
            aflk = !aflk; auscnt++;
        }
    } else {
        put(110, "Bad Landing!", tflk ? RGB(60, 120, 255) : RGB(255, 100, 0));
        tflk = !tflk;
    }
    if (m->trcnt == 10) {
        char loop[48] = "", spin[32] = "";
        int n5 = 0, tzy = m->travzy, txy = abs(m->travxy), txz = abs(m->travxz);
        while (tzy > 225) { tzy -= 360; n5++; }
        while (tzy < -225) { tzy += 360; n5--; }
        asay[0] = 0;
        if (n5 == 1) strcpy(loop, "Forward loop");
        if (n5 == 2) strcpy(loop, "double Forward");
        if (n5 == 3) strcpy(loop, "triple Forward");
        if (n5 >= 4) strcpy(loop, "massive Forward looping");
        if (n5 == -1) strcpy(loop, "Backloop");
        if (n5 == -2) strcpy(loop, "double Back");
        if (n5 == -3) strcpy(loop, "triple Back");
        if (n5 <= -4) strcpy(loop, "massive Back looping");
        if (n5 == 0) {
            if (m->ftab && m->btab) strcpy(loop, "Tabletop and reversed Tabletop");
            else if (m->ftab || m->btab) strcpy(loop, "Tabletop");
        }
        if ((n5 > 0 && m->btab) || (n5 < 0 && m->ftab)) { char t[48]; snprintf(t, sizeof t, "Hanged %s", loop); strcpy(loop, t); }
        if (loop[0]) { cat(asay, sizeof asay, " "); cat(asay, sizeof asay, loop); }
        int n6 = 0;
        while (txy > 270) { txy -= 360; n6++; }
        if (n6 == 0 && m->rtab) strcpy(spin, loop[0] ? "Flipside" : "Tabletop");
        if (n6 == 1) strcpy(spin, "Rollspin");
        if (n6 == 2) strcpy(spin, "double Rollspin");
        if (n6 == 3) strcpy(spin, "triple Rollspin");
        if (n6 >= 4) strcpy(spin, "massive Roll spinning");
        int n7 = 0, beyond = 0;
        while (txz > 90) { txz -= 180; n7 += 180; if (n7 > 900) { n7 = 900; beyond = 1; } }
        char num[24];
        if (n7) {
            if (!loop[0] && !spin[0]) {
                snprintf(num, sizeof num, " %d", n7); cat(asay, sizeof asay, num);
            } else {
                if (spin[0]) { cat(asay, sizeof asay, loop[0] ? " with " : " "); cat(asay, sizeof asay, spin); }
                snprintf(num, sizeof num, " by %d", n7); cat(asay, sizeof asay, num);
            }
            if (beyond) cat(asay, sizeof asay, " and beyond");
        } else if (spin[0]) {
            cat(asay, sizeof asay, loop[0] ? " by " : " "); cat(asay, sizeof asay, spin);
        }
        if (asay[0]) auscnt -= 15;
        if (loop[0]) auscnt -= 25;
        if (spin[0]) auscnt -= 25;
        if (n7) auscnt -= 25;
        if (auscnt < 45) {
            stunt = 1;
            if (auscnt < -20) auscnt = -20;
            int n8 = m->powerup > 150.0f ? 3 : m->powerup > 40.0f ? 2 : m->powerup > 20.0f ? 1 : 0;
            char t[96];
            if (m->surfer) { snprintf(t, sizeof t, " %s%s", adj[4][pick3()], asay); strcpy(asay, t); }
            if (n8 != 3) snprintf(t, sizeof t, "%s%s%s", adj[n8][pick3()], asay, exlm[n8]);
            else snprintf(t, sizeof t, "%s", adj[3][pick3()]);
            snprintf(asay, sizeof asay, "%s", t);
            if (!wasay) {
                tcnt = auscnt;
                if (m->power != 98.0f) snprintf(say, sizeof say, "Power Up %d%%", (int)(100.0f * m->powerup / 98.0f));
                else strcpy(say, "Power To The MAX");
            }
        }
    }
    if (m->newcar && !wasay) { strcpy(say, "Car Fixed"); tcnt = 0; }
    for (int k = 0; k < nai; k++) {
        int d = cp->dested[k + 1];
        if (d != s_dested[k + 1]) {
            s_dested[k + 1] = d;
            if (d == 1) { wasay = 1; snprintf(say, sizeof say, "%s has been wasted!", ai_names[k]); tcnt = -15; }
            if (d == 2) { wasay = 1; snprintf(say, sizeof say, "You wasted %s!", ai_names[k]); tcnt = -15; }
        }
    }
    if (s_clear != m->clear && m->clear != 0) {
        if (!wasay) { strcpy(say, "Checkpoint!"); tcnt = 15; }
        s_clear = m->clear; cntovn = 0; cntan = 0;
    }
    return stunt;
}

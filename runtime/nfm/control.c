/* control.c — the CPU driver (Control.preform) and the race bookkeeping (CheckPoints.checkstat).
 * preform/reset come from gen/control_gen.inc (spec/gen_nfm_control.py). */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "mad.h"

#define JABS(x) _Generic((x), int: abs, float: fabsf, double: fabs)(x)

static float ctl_rand(Control *C) {
    C->rng = C->rng * 1664525u + 1013904223u;
    return (float)(C->rng >> 8) / 16777216.0f;
}

void control_init(Control *C, uint32_t seed) {
    memset(C, 0, sizeof *C);
    C->rng = 0x85ebca6bu ^ (seed * 2654435761u);
    C->skiplev = 1.0f;
    C->clrnce = 5;
    C->saftey = 30;
    C->mustland = 0.5f;
    C->trickprf = 0.5f;
    C->wall = C->lwall = -1;
}

static int cpy(int n, int n2, int n3, int n4);
static int cpys(int n, int n2, int n3, int n4);
#include "gen/control_gen.inc"

static int cp_py(int a, int b, int c, int d) { return (a - b) * (a - b) + (c - d) * (c - d); }

/* CheckPoints.checkstat: per-tick race standings.  pos[i] = number of cars ahead of car i (0 = leading).  The
 * finish-photo/Record parts (catchfin, cotchinow) only feed the replay and are dropped. */
void checkpoints_checkstat(CheckPoints *cp, Mad *mads, CarObj *objs, int n, int im) {
    if (!cp->haltall) {
        cp->pcleared = mads[im].pcleared;
        for (int i = 0; i < n; ++i) {
            cp->magperc[i] = (float)mads[i].hitmag / (float)mads[i].cd[mads[i].cn].maxmag;
            if (cp->magperc[i] > 1.0f) cp->magperc[i] = 1.0f;
            cp->pos[i] = 0;
            cp->onscreen[i] = objs[i].in->dist;
            cp->opx[i] = objs[i].in->x;
            cp->opz[i] = objs[i].in->z;
            cp->omxz[i] = mads[i].mxz;
            cp->clear[i] = cp->dested[i] == 0 ? mads[i].clear : -1;
            mads[i].outshakedam = mads[i].shakedam;
            mads[i].shakedam = 0;
        }
        for (int j = 0; j < n; ++j) {
            for (int k = j + 1; k < n; ++k) {
                if (cp->clear[j] != cp->clear[k]) {
                    if (cp->clear[j] < cp->clear[k]) ++cp->pos[j]; else ++cp->pos[k];
                } else {
                    int n6 = mads[j].pcleared + 1;
                    if (n6 >= cp->n) n6 = 0;
                    for (int guard = 0; cp->typ[n6] <= 0 && guard < cp->n; ++guard)
                        if (++n6 >= cp->n) n6 = 0;
                    if (cp_py(objs[j].in->x / 100, cp->x[n6] / 100, objs[j].in->z / 100, cp->z[n6] / 100) >
                        cp_py(objs[k].in->x / 100, cp->x[n6] / 100, objs[k].in->z / 100, cp->z[n6] / 100))
                        ++cp->pos[j];
                    else
                        ++cp->pos[k];
                }
            }
        }
    }
    cp->wasted = 0;
    for (int i = 0; i < n; ++i)
        if (i != im && mads[i].dest) ++cp->wasted;
}

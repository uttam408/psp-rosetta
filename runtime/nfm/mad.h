/* mad.h — port of NFM's Mad (car physics), Trackers (collision boxes) and the checkpoint state Mad reads.
 * The physics bodies are generated from Mad.java by spec/gen_nfm_mad.py; this header is the
 * hand-written state they run against.  Cosmetics (sound, dust, sparks, HSB tinting, replay
 * recording) are stubbed out; damage bending (per-poly vertex offsets) is kept because it feeds
 * hitmag/destruction. */
#ifndef NFM_MAD_H
#define NFM_MAD_H

#include <stdint.h>
#include <stdbool.h>
#include "medium.h"
#include "gen/cardefs.h"

/* Control.java's state: the input flags Mad reads plus everything preform() (the CPU driver) keeps between ticks */
typedef struct {
    bool left, right, up, down, handb, zyinv;
    int wall;
    int lookback, chatup, multion;
    bool enter, exit, arrace, mutem, mutes, radar;
    int pan, attack, acr, trfix, runbul, acuracy, upwait, clrnce, rampp, turntyp, saftey, stuntf, hold, lwall, stcnt, statusque;
    int turncnt, randtcnt, upcnt, trickfase, swat, lrdirect, uddirect, lrstart, udstart, oxy, ozy, flycnt, actwait, cntrn;
    int revstart, oupnt, wtz, wtx, frx, frz, frad, apunch, avoidnlev;
    int fpnt[5];
    bool afta, forget, bulistc, agressed, perfection, usebounce, lastl, wlastl, udcomp, lrcomp, udbare, lrbare;
    bool onceu, onced, oncel, oncer, lrswt, udswt, gowait, exitattack;
    float skiplev, aim, mustland, trickprf;
    uint32_t rng;
} Control;

typedef struct { int *v; int length; } Sect;

typedef struct {
    int n;                         /* tracker count */
    int *x, *y, *z, *xy, *zy, *skd, *dam, *radx, *radz, *rady;
    bool *notwall, *decor;
    int sx, sz, ncx, ncz;
    Sect **sect;                   /* [ncx+1][ncz+1] sector lists */
} Trackers;

#define MAD_MAXCP 200
typedef struct {
    int n, nsp, nlaps, haltall, fn, nfix, stage, pcs;
    int typ[MAD_MAXCP], x[MAD_MAXCP], y[MAD_MAXCP], z[MAD_MAXCP];
    int dested[8];
    int pos[8], clear[8], onscreen[8], opx[8], opz[8], omxz[8];
    float magperc[8];
    bool special[5];
    int wasted, pcleared, catchfin, postwo;
    int fx[MAD_MAXCP], fy[MAD_MAXCP], fz[MAD_MAXCP];
    bool roted[MAD_MAXCP];
} CheckPoints;

/* per-poly damage state (Plane.ox/oy/oz + the cosmetic fields Mad pokes) */
typedef struct {
    int n, wz, gr, bfase, chip, glass, embos;
    bool nocol;
    float ctmag, hsb[3];
    int c[3];
    int *ox, *oy, *oz;
} MadPoly;

/* the ContO fields Mad touches: instance transform lives in `in` */
typedef struct {
    Inst *in;
    int keyx[4], keyz[4], grat, maxR, npl, fcnt;
    bool fix;
    MadPoly *p;
    int *vbuf;                     /* backing store for p[].ox/oy/oz */
} CarObj;

/* what Mad reads/writes of xtGraphics / Medium / Record */
typedef struct {
    int im, multion, resdown, noelec, checkpoint;
    bool lan, mutes, lastcheck;
    bool isbot[8];
    int laptime, fastestlap, starcnt, beststunt;
    int dcrashes[8];
    int rpd_dest[8], rpd_fix[8];
    int rpd_powered, rpd_wasted, rpd_whenwasted;
    bool rpd_hcaught;
} MadEnv;

typedef struct Mad {
    MadEnv *env;
    const CarStats *cd;            /* NFM_CARS */
    uint32_t rng;
    int cn, im, mxz, cxz;
    bool dominate[8], caught[8];
    int pzy, pxy;
    float speed, forca, scy[4], scz[4], scx[4], drag;
    bool mtouch, wtouch;
    int cntouch;
    bool capsized;
    int txz, fxz, pmlt, nmlt, dcnt, skid;
    bool pushed, gtouch, pl, pr, pd, pu;
    int loop;
    float ucomp, dcomp, lcomp, rcomp;
    int lxz, travxy, travzy, travxz, trcnt, capcnt, srfcnt;
    bool rtab, ftab, btab, surfer;
    float powerup;
    int xtpower;
    float tilt;
    int crank[4][4], lcrank[4][4];
    int squash, nbsq, hitmag, cntdest;
    bool dest, newcar;
    int pan, pcleared, clear, nlaps, focus;
    float power;
    int missedcp, lastcolido, point;
    bool nofocus;
    int rpdcatch, newedcar, fixes, shakedam, outshakedam;
    bool colidim;
    /* driving assist layered over the generated physics by mad_drive's wrapper (see mad.c) */
    int steer_cap;                 /* max |wheel steer angle|; 36 is the original's limit */
} Mad;

#define MAD_STEER_ORIG 36

void mad_init(Mad *M, MadEnv *env, int im);
float mad_rand(Mad *M);            /* replaces Math.random(): deterministic per car */

/* CarObj: wraps an Inst + its mesh with per-car mutable damage vertices */
bool carobj_init(CarObj *o, Inst *in);
void carobj_free(CarObj *o);
#define MAD_MAXTRK 6700
bool trackers_init(Trackers *T);                                        /* Trackers() */
void trackers_add_piece(Trackers *T, const PMesh *pm, int x, int y, int z, int xz, bool decor);   /* ContO ctor's tracker copy */
void trackers_add_bumproad(Trackers *T, const PMesh *pm, int x, int y, int z, int xz);   /* bumproad: sloped bump floors */
void trackers_add_wall(Trackers *T, int x, int y, int z, int radx, int radz, int rady, int xy, int zy);   /* maxr/l/t/b bounding box (dam 167) */
void trackers_divide(Trackers *T, int sx, int n, int sz, int n2);       /* devidetrackers */
void trackers_free(Trackers *T);

void mad_reseto(Mad *M, int cn, CarObj *o, CheckPoints *cp);
void mad_drive(Mad *M, Control *ctl, CarObj *o, Trackers *T, CheckPoints *cp);
int  mad_regy(Mad *M, int n, float n2, CarObj *o);
int  mad_regx(Mad *M, int n, float n2, CarObj *o);
int  mad_regz(Mad *M, int n, float n2, CarObj *o);
void mad_colide(Mad *M, CarObj *o, Mad *mad, CarObj *o2);

/* Control.java + CheckPoints.checkstat */
void control_init(Control *C, uint32_t seed);
void control_reset(Control *C, CheckPoints *cp, int n);
void control_preform(Control *C, Mad *mad, CarObj *o, CheckPoints *cp, Trackers *T);
void checkpoints_checkstat(CheckPoints *cp, Mad *mads, CarObj *objs, int n, int im);

#endif

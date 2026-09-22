/* stage.h — .pstg loader (layout: pipeline/nfm/pstg.py) and scene instantiation. */
#ifndef NFM_STAGE_H
#define NFM_STAGE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "medium.h"
#include "mad.h"

enum { ST_SET, ST_CHK, ST_FIX, ST_PILE, ST_MAXR, ST_MAXL, ST_MAXT, ST_MAXB };

#define NFM_MAXRACERS 6   /* player + up to 5 AI, per scene_add_racer slot */

typedef struct { uint8_t op, flags; int16_t id; int32_t a[5]; } StObj;  /* 24 bytes */

typedef struct {
    bool lightson;
    uint16_t present;
    int16_t snap[3], sky[3], ground[3], polys[3], fog[3], texture[4], clouds[5];
    int32_t fadefrom, density, mountains, nlaps;
    uint32_t nobjs;
    StObj *objs;                 /* malloc'd copy */
    char name[256], track[256];
    int16_t volume; int32_t size;
} Stage;

bool stage_load(Stage *s, const uint8_t *d, size_t n);
void stage_free(Stage *s);

/* replay the environment directives through Medium's setters, in stage-file order */
void stage_apply_env(const Stage *s, Medium *m);

/* the placed pieces of a stage */
typedef struct {
    Inst *inst;
    uint32_t n;
    PMesh *meshes;               /* one per distinct piece id */
    PMesh *piles;                /* one procedural mesh per `pile` directive */
    uint32_t npiles;
    int nmesh_slots;
    PMesh car_src, car;          /* optional test car (scene_add_car) */
    Inst *car_inst;
    PMesh racer_src[NFM_MAXRACERS], racer[NFM_MAXRACERS];   /* multi-car slots (scene_add_racer) */
    Inst *racer_inst[NFM_MAXRACERS];
    int nracers;
    bool physics;                /* set before scene_build: also build trk/cp (collision boxes, checkpoints) */
    Trackers trk;
    CheckPoints cp;
    uint32_t skipped;           /* directives not yet instantiated (unknown ids, missing meshes) */
} Scene;

/* looks pieces up in the pak as mesh/piece/<name>; needs env applied first (snap colours) */
bool scene_build(Scene *sc, const Stage *s, Medium *m);
void scene_free(Scene *sc);
/* places a car (pak id e.g. "mesh/car/audir8", wheels built in) standing on the ground at (x,z); the spare Inst slot of
 * scene_build holds it.  p1/p2 = first/second paint 0xRRGGBB or -1.  Returns the instance or NULL. */
Inst *scene_add_car(Scene *sc, Medium *m, const char *id, int x, int z, int xz, int32_t p1, int32_t p2);
/* like scene_add_car, but for one of NFM_MAXRACERS independent race-car slots (own mesh, own Inst), so several
 * cars (player + AI) can be on scene together; slot must be < NFM_MAXRACERS and scene_build's spare-Inst
 * reservation covers it.  Returns the instance or NULL. */
Inst *scene_add_racer(Scene *sc, Medium *m, int slot, const char *id, int x, int z, int xz, int32_t p1, int32_t p2);
void scene_draw(Medium *m, Scene *sc);   /* backdrop, then objects far-to-near by ContO.dist */

#endif

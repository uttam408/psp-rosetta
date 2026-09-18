#include "fx.h"
#include "game.h"
#include "pool.h"

/* ------------------------------------------------------------------ fx_anim */
typedef struct {
    Entity e;
    const PakAsset *a;
    GfxTex *t;
    real frame, rate;
    bool   bottom_pin;   /* true: bottom edge sits at y (water-surface FX) */
} Anim;

POOL(anim_pool, Anim, 192)
static void anim_recycle(Entity *e) { anim_pool_put(e->user); }

static void anim_update(Entity *e)
{
    Anim *an = e->user;
    an->frame += an->rate;
    if (an->a && an->frame >= an->a->nframes) world_remove(e->world, e);
}

static void anim_render(Entity *e)
{
    Anim *an = e->user;
    if (!an->t || !an->a || an->a->nframes == 0) return;
    int i = (int)an->frame;
    if (i < 0) i = 0;
    if (i >= an->a->nframes) i = an->a->nframes - 1;
    PakRect r = an->a->frames[i];
    real oy = an->bottom_pin ? r.h : r.h / 2.0;
    gfx_draw(an->t, e->x, e->y, 0, r.w / 2.0, oy, 1, 1, 0xFFFFFF,
             r.x, r.y, r.w, r.h);
}

static Entity *anim_spawn(const char *tex_id, real x, real y, real rate, bool bottom_pin)
{
    Anim *an = anim_pool_get();
    if (!an) return NULL;
    ent_init(&an->e, ETYPE_FX);
    an->e.user = an;
    an->e.update = anim_update;
    an->e.render = anim_render;
    an->e.recycle = anim_recycle;
    an->e.layer = LAYER_FX;
    an->e.collidable = false;
    an->e.x = x; an->e.y = y;
    an->a = pak_find(tex_id);
    an->t = tex(tex_id);
    an->rate = rate;
    an->bottom_pin = bottom_pin;
    return world_add(&g_game.world, &an->e);
}

Entity *fx_anim(const char *tex_id, real x, real y, real rate)
{
    return anim_spawn(tex_id, x, y, rate, false);
}

/* WaterSplash/BigWaterSplash.as: "bottom pinned to water line" — the sprite
 * rises up out of the surface instead of straddling it. */
Entity *fx_anim_bottom(const char *tex_id, real x, real y, real rate)
{
    return anim_spawn(tex_id, x, y, rate, true);
}

/* ------------------------------------------------------------------ fx_part */
typedef struct {
    Entity e;
    const PakAsset *a;
    GfxTex *t;
    int frame, life;
} Part;

POOL(part_pool, Part, 192)
static void part_recycle(Entity *e) { part_pool_put(e->user); }

static void part_update(Entity *e)
{
    Part *p = e->user;
    ent_update(e);
    if (fp_rand(15) < 1)      /* debris occasionally trails smoke (the Part FX classes) */
        fx_smoke(e->x, e->y, fp_rand(360), fp_random() * 2.0);
    if (--p->life <= 0) world_remove(e->world, e);
}

static void part_render(Entity *e)
{
    Part *p = e->user;
    if (!p->t || !p->a || p->a->nframes == 0) return;
    PakRect r = p->a->frames[p->frame % p->a->nframes];
    gfx_draw(p->t, e->x, e->y, 0, r.w / 2.0, r.h / 2.0, 1, 1, 0xFFFFFF,
             r.x, r.y, r.w, r.h);
}

Entity *fx_part(const char *tex_id, real x, real y,
                real toss_angle, real toss_speed, int life)
{
    Part *p = part_pool_get();
    if (!p) return NULL;
    ent_init(&p->e, ETYPE_FX);
    p->e.user = p;
    p->e.update = part_update;
    p->e.render = part_render;
    p->e.recycle = part_recycle;
    p->e.layer = LAYER_PART;
    p->e.collidable = false;
    p->e.x = x; p->e.y = y;
    p->e.gravity = 0.2;
    p->a = pak_find(tex_id);
    p->t = tex(tex_id);
    p->frame = p->a && p->a->nframes ? (int)fp_rand(p->a->nframes) : 0;
    p->life = life;
    ent_motion_add(&p->e, toss_angle, toss_speed);
    ent_set_vspeed(&p->e, ent_vspeed(&p->e) - (real)fp_rand(2));
    return world_add(&g_game.world, &p->e);
}

/* ----------------------------------------------------------------- fx_blurb */
typedef struct { Entity e; int life; int amount; } Blurb;

POOL(blurb_pool, Blurb, 32)
static void blurb_recycle(Entity *e) { blurb_pool_put(e->user); }

static void blurb_update(Entity *e)
{
    Blurb *b = e->user;
    e->y -= 1;
    if (--b->life <= 0) world_remove(e->world, e);
}

static void blurb_render(Entity *e)
{
    /* TODO: bitmap-font "+N". Placeholder: a small dark-red tick. */
    GfxTex *t = tex("image/interaction/bullet/bullet");
    if (t) gfx_draw(t, e->x, e->y, 0, 8, 8, 0.5, 0.5, 0x610C1D, 0, 0, 16, 16);
}

Entity *fx_blurb(int amount, real x, real y)
{
    Blurb *b = blurb_pool_get();
    if (!b) return NULL;
    ent_init(&b->e, ETYPE_FX);
    b->e.user = b;
    b->e.update = blurb_update;
    b->e.render = blurb_render;
    b->e.recycle = blurb_recycle;
    b->e.layer = LAYER_BLURB;
    b->e.collidable = false;
    b->e.x = x; b->e.y = y;
    b->life = 60;
    b->amount = amount;
    return world_add(&g_game.world, &b->e);
}

/* ----------------------------------------------------------------- fx_smoke */
/* FX/Smoke.as: 16x16, 3 frames @0.2, friction=0.1 (no gravity), drifts with
 * whatever motionAdd gave it, self-removes when the animation completes. */
typedef struct {
    Entity e;
    const PakAsset *a;
    GfxTex *t;
    real frame;
} Smoke;

POOL(smoke_pool, Smoke, 96)
static void smoke_recycle(Entity *e) { smoke_pool_put(e->user); }

static void smoke_update(Entity *e)
{
    Smoke *s = e->user;
    ent_update(e);
    s->frame += 0.2;
    if (s->a && s->frame >= s->a->nframes) world_remove(e->world, e);
}

static void smoke_render(Entity *e)
{
    Smoke *s = e->user;
    if (!s->t || !s->a || s->a->nframes == 0) return;
    int i = (int)s->frame;
    if (i >= s->a->nframes) i = s->a->nframes - 1;
    PakRect r = s->a->frames[i];
    gfx_draw(s->t, e->x, e->y, 0, r.w / 2.0, r.h / 2.0, 1, 1, 0xFFFFFF,
             r.x, r.y, r.w, r.h);
}

Entity *fx_smoke(real x, real y, real angle, real speed)
{
    Smoke *s = smoke_pool_get();
    if (!s) return NULL;
    ent_init(&s->e, ETYPE_FX);
    s->e.user = s;
    s->e.update = smoke_update;
    s->e.render = smoke_render;
    s->e.recycle = smoke_recycle;
    s->e.layer = LAYER_PART;
    s->e.collidable = false;
    s->e.friction = 0.1;
    s->e.x = x; s->e.y = y;
    s->a = pak_find("image/interaction/fx/smoke/smoke");
    s->t = tex("image/interaction/fx/smoke/smoke");
    ent_motion_add(&s->e, angle, speed);
    return world_add(&g_game.world, &s->e);
}

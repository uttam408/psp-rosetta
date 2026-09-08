#include "fx.h"
#include "game.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ fx_anim */
typedef struct {
    Entity e;
    const PakAsset *a;
    GfxTex *t;
    double frame, rate;
} Anim;

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
    gfx_draw(an->t, e->x, e->y, 0, r.w / 2.0, r.h / 2.0, 1, 1, 0xFFFFFF,
             r.x, r.y, r.w, r.h);
}

Entity *fx_anim(const char *tex_id, double x, double y, double rate)
{
    Anim *an = calloc(1, sizeof *an);
    ent_init(&an->e, ETYPE_FX);
    an->e.user = an;
    an->e.update = anim_update;
    an->e.render = anim_render;
    an->e.layer = 5;
    an->e.collidable = false;
    an->e.x = x; an->e.y = y;
    an->a = pak_find(tex_id);
    an->t = tex(tex_id);
    an->rate = rate;
    return world_add(&g_game.world, &an->e);
}

/* ------------------------------------------------------------------ fx_part */
typedef struct {
    Entity e;
    const PakAsset *a;
    GfxTex *t;
    int frame, life;
} Part;

static void part_update(Entity *e)
{
    Part *p = e->user;
    ent_update(e);
    if (--p->life <= 0) world_remove(e->world, e);
    /* TODO: over-water -> WaterSplash + remove; timeout -> SmallExplosion */
}

static void part_render(Entity *e)
{
    Part *p = e->user;
    if (!p->t || !p->a || p->a->nframes == 0) return;
    PakRect r = p->a->frames[p->frame % p->a->nframes];
    gfx_draw(p->t, e->x, e->y, 0, r.w / 2.0, r.h / 2.0, 1, 1, 0xFFFFFF,
             r.x, r.y, r.w, r.h);
}

Entity *fx_part(const char *tex_id, double x, double y,
                double toss_angle, double toss_speed, int life)
{
    Part *p = calloc(1, sizeof *p);
    ent_init(&p->e, ETYPE_FX);
    p->e.user = p;
    p->e.update = part_update;
    p->e.render = part_render;
    p->e.layer = 6;
    p->e.collidable = false;
    p->e.x = x; p->e.y = y;
    p->e.gravity = 0.2;
    p->a = pak_find(tex_id);
    p->t = tex(tex_id);
    p->frame = p->a && p->a->nframes ? (int)fp_rand(p->a->nframes) : 0;
    p->life = life;
    ent_motion_add(&p->e, toss_angle, toss_speed);
    ent_set_vspeed(&p->e, ent_vspeed(&p->e) - (double)fp_rand(2));
    return world_add(&g_game.world, &p->e);
}

/* ----------------------------------------------------------------- fx_blurb */
typedef struct { Entity e; int life; int amount; } Blurb;

static void blurb_update(Entity *e)
{
    Blurb *b = e->user;
    e->y -= 1;
    if (--b->life <= 0) world_remove(e->world, e);
}

static void blurb_render(Entity *e)
{
    /* TODO: bitmap-font "+N". Placeholder: a small dark-red tick. */
    Blurb *b = e->user;
    GfxTex *t = tex("image/interaction/bullet/bullet");
    if (t) gfx_draw(t, e->x, e->y, 0, 8, 8, 0.5, 0.5, 0x610C1D, 0, 0, 16, 16);
    (void)b;
}

Entity *fx_blurb(int amount, double x, double y)
{
    Blurb *b = calloc(1, sizeof *b);
    ent_init(&b->e, ETYPE_FX);
    b->e.user = b;
    b->e.update = blurb_update;
    b->e.render = blurb_render;
    b->e.layer = 1;
    b->e.collidable = false;
    b->e.x = x; b->e.y = y;
    b->life = 60;
    b->amount = amount;
    return world_add(&g_game.world, &b->e);
}

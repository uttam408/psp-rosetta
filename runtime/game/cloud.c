/* cloud.c — Interaction/FX/Cloud.as: 6 background parallax clouds, one random
 * frame each, drifting at a constant per-cloud speed, wrapping around the
 * camera. Faithful to the exact wrap math (Cloud.as:18,29-35). */
#include "game.h"
#include "pool.h"

typedef struct {
    Entity e;
    GfxTex *t;
    PakRect frame;
} Cloud;

POOL(cloud_pool, Cloud, 8)
static void cloud_recycle(Entity *e) { cloud_pool_put(e->user); }

static void cloud_update(Entity *e)
{
    Cloud *c = e->user;
    e->x += ent_hspeed(e);
    double w = fp_width, cw = c->frame.w;
    if (e->x < fp_camera.x - 600)
        e->x = fp_camera.x + w + cw + 500;
    if (e->x > fp_camera.x + w + cw + 600)
        e->x = fp_camera.x - 500;
}

static void cloud_render(Entity *e)
{
    Cloud *c = e->user;
    if (!c->t || c->frame.w == 0) return;
    gfx_draw(c->t, e->x, e->y, 0, c->frame.w / 2.0, c->frame.h / 2.0, 1, 1,
             0xFFFFFF, c->frame.x, c->frame.y, c->frame.w, c->frame.h);
}

Entity *cloud_spawn(void)
{
    Cloud *c = cloud_pool_get();
    if (!c) return NULL;
    ent_init(&c->e, ETYPE_CLOUD);
    c->e.user = c;
    c->e.update = cloud_update;
    c->e.render = cloud_render;
    c->e.recycle = cloud_recycle;
    c->e.layer = LAYER_CLOUD;
    c->e.collidable = false;

    const PakAsset *a = pak_find("image/interaction/fx/cloud/cloud");
    c->t = tex("image/interaction/fx/cloud/cloud");
    if (a && a->nframes) c->frame = a->frames[fp_rand(a->nframes)];

    c->e.x = fp_camera.x - 500 + (double)fp_rand((uint32_t)(fp_width + 1000));
    c->e.y = 100 + fp_rand(400);
    ent_set_hspeed(&c->e, fp_choose2(-1.0, 1.0) * fp_random());
    return world_add(&g_game.world, &c->e);
}

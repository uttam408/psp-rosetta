/* backdrop.c — Interaction/Water.as + Space.as.
 * Both are horizontal bands (FlashPunk TiledImage) that follow the camera in X.
 * Water surface sits at world y = SPEC_WORLD_WATER_Y; Space band tops out the sky. */
#include "game.h"
#include "pool.h"

typedef struct { Entity e; GfxTex *t; double band_y; } Band;

POOL(band_pool, Band, 4)
static void band_recycle(Entity *e) { band_pool_put(e->user); }

static void band_render(Entity *e)
{
    Band *b = e->user;
    if (!b->t) return;
    /* cover the visible width plus a tile of slop on each side */
    double x = fp_camera.x - 64;
    gfx_draw_tiled(b->t, x, b->band_y, fp_width + 128, 240);
}

static Entity *band_spawn(EntityType type, const char *tex_id, double band_y, int layer)
{
    Band *b = band_pool_get();
    if (!b) return NULL;
    ent_init(&b->e, type);
    b->e.user = b;
    b->e.render = band_render;      /* no update */
    b->e.update = NULL;
    b->e.recycle = band_recycle;
    b->e.layer = layer;
    b->e.collidable = false;
    b->t = tex(tex_id);
    b->band_y = band_y;
    return world_add(&g_game.world, &b->e);
}

Entity *backdrop_spawn_water(void)
{
    return band_spawn(ETYPE_WATER, "image/interaction/water/water",
                      SPEC_WORLD_WATER_Y, LAYER_WATER);
}

Entity *backdrop_spawn_space(void)
{
    return band_spawn(ETYPE_SPACE, "image/interaction/space/space",
                      SPEC_WORLD_SPACE_Y, LAYER_SPACE);
}

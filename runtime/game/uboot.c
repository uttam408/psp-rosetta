/* uboot.c — Interaction/UBoot.as, one-to-one (attract-mode launcher).
 * Milestone: rise, ready, launch the player, dive, remove. Not yet: the floating
 * logo, the "PRESS UP TO LAUNCH" text, enemy spawn on removal (TODO). */
#include "game.h"
#include "pool.h"
#include "../src/input.h"
#include "../src/audio.h"

typedef struct {
    Entity e;
    int    t_ready;     /* frames until vspeed=0 + ready            */
    int    t_dive;      /* after launch: frames until dive          */
    int    t_remove;    /* after dive: frames until removal         */
    bool   ready;
    bool   launched;
    GfxTex *hull;
} UBoot;

POOL(uboot_pool, UBoot, 2)
static void uboot_recycle(Entity *e) { uboot_pool_put(e->user); }

static void uboot_update(Entity *e)
{
    UBoot *u = e->user;

    if (!u->ready) {
        if (--u->t_ready <= 0) { ent_set_vspeed(e, 0); u->ready = true; }
    }

    if (u->ready && !u->launched && in_pressed(ACT_THRUST)) {
        if (!g_game.music_started) {
            snd_music("audio/worlds/game/music", 0.5f);
            g_game.music_started = true;
        }
        player_spawn(e->x, e->y);
        u->launched = true;
        u->t_dive = SPEC_UBOOT_LAUNCH_DIVE_AFTER;
    }

    if (u->launched) {
        if (u->t_dive > 0 && --u->t_dive == 0) {
            ent_set_vspeed(e, 1);
            u->t_remove = SPEC_UBOOT_LAUNCH_REMOVE_AFTER;
        } else if (u->t_remove > 0 && --u->t_remove == 0) {
            game_begin_combat();               /* Game.spawnEnemies() */
            world_remove(e->world, e);
        }
    }

    ent_update(e);

    /* camera hard-locked while there is no Player (UBoot.as:57-61) */
    if (world_count_type(&g_game.world, ETYPE_PLAYER) == 0) {
        fp_camera.x = e->x - fp_half_width;
        fp_camera.y = SPEC_UBOOT_CAMERA_LOCK_Y - fp_half_height;
    }
}

static void uboot_render(Entity *e)
{
    UBoot *u = e->user;
    if (u->hull) {
        const PakAsset *a = pak_find("image/interaction/uboot/uboot");
        int w = a ? a->w : 128, h = a ? a->h : 64;
        gfx_draw(u->hull, e->x, e->y, 0, w / 2.0, h / 2.0, 1, 1,
                 0xFFFFFF, 0, 0, w, h);
    }
}

Entity *uboot_spawn(void)
{
    UBoot *u = uboot_pool_get();
    if (!u) return NULL;
    ent_init(&u->e, ETYPE_UBOOT);
    u->e.user = u;
    u->e.update = uboot_update;
    u->e.render = uboot_render;
    u->e.recycle = uboot_recycle;
    u->e.layer = LAYER_ENEMY;

    const PakAsset *a = pak_find("image/interaction/uboot/uboot");
    double hh = a ? a->h / 2.0 : 24;
    u->e.x = SPEC_UBOOT_SPAWN_X;
    u->e.y = SPEC_WORLD_WATER_Y + hh;           /* just below the water line */
    ent_set_vspeed(&u->e, SPEC_UBOOT_RISE_VSPEED);
    u->t_ready = SPEC_UBOOT_READY_AFTER;
    u->hull = tex("image/interaction/uboot/uboot");

    return world_add(&g_game.world, &u->e);
}

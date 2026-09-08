/* sea.c — Interaction/Enemies/Boot.as (battleship) + Bootje.as (boat).
 * Shared sea-unit behaviour: locked to the water surface, drift horizontally,
 * screen-wrap around the camera, sink on death. one-to-one.
 * TODO: exact Boot fire/fireSecondary two-alarm dance is approximated as a
 * timed burst; ram knockback direction is "straight up/away", not per-quadrant. */
#include "game.h"
#include "enemy.h"
#include "fx.h"
#include "../src/audio.h"
#include <stdlib.h>

typedef struct {
    Enemy   en;
    int     ship_w, ship_h;
    GfxTex *spr;
    int     t_fire;
    int     burst;            /* shots left in the current burst           */
    int     burst_gap;        /* frames between burst shots                */
    int     reload_min, reload_rand;
    double  bullet_speed;
    double  firex, firey;     /* snapshotted aim (Boot); live for Bootje   */
    bool    aim_snapshot;
    double  bullet_ox, bullet_oy;
    const char *fire_snd;
    const char *sink_fx;      /* explosion spawned x3 on death + randomly   */
} Sea;

static Entity *player_e(void) { return world_first_type(&g_game.world, ETYPE_PLAYER); }

static void sea_sink_start(Enemy *en)
{
    Sea *s = (Sea *)en;
    if (en->dying) return;
    en->dying = true;
    en->e.collidable = false;
    en->e.type = ETYPE_NONE;                 /* AS3 sets type="" */
    if (player_e() && en->die_snd) snd_play(en->die_snd, 1.0f, 0.0f);
    for (int i = 0; i < 3; i++)
        fx_anim(s->sink_fx, en->e.x + fp_rand(60), en->e.y + fp_rand(20), 0.5);
    fx_blurb(en->score, en->e.x, en->e.y);
}

static void sea_fire(Sea *s)
{
    Entity *e = &s->en.e;
    Entity *pl = player_e();
    if (!pl) { s->t_fire = 60; return; }

    if (s->burst > 0) {
        s->burst--;
        s->t_fire = s->burst_gap;
        if (player_health(pl) > 0) {
            double ax = s->aim_snapshot ? s->firex : pl->x;
            double ay = s->aim_snapshot ? s->firey : pl->y;
            snd_play(s->fire_snd, 1.0f, 0.0f);
            ebullet_spawn(e->x + s->bullet_ox, e->y + s->bullet_oy,
                          fp_angle(e->x, e->y, ax, ay), s->bullet_speed);
        }
    } else {
        s->burst = s->aim_snapshot ? 4 : 3;
        s->firex = pl->x; s->firey = pl->y;      /* snapshot for Boot */
        s->t_fire = s->reload_min + (int)fp_rand(s->reload_rand);
    }
}

static void sea_update(Entity *e)
{
    Sea *s = e->user;
    Enemy *en = &s->en;

    if (!en->dying) {
        e->y = SPEC_WORLD_WATER_Y - s->ship_h;             /* water-locked */

        if (--s->t_fire <= 0) sea_fire(s);

        Entity *pl = player_e();
        if (pl && world_collide(&g_game.world, ETYPE_PLAYER, e, e->x, e->y)) {
            player_health_delta(pl, -(double)SPEC_PLAYER_DAMAGE_DMG_BOAT_CONTACT);
            ent_motion_add(pl, fp_angle(e->x, e->y, pl->x, pl->y), 2);
            en->health -= 2;
            if (en->health <= 0) { sea_sink_start(en); }
        }
    } else {
        ent_set_hspeed(e, 0);
        if (e->y > SPEC_WORLD_WATER_Y - s->ship_h + 60) {
            enemy_on_removed(en);
            world_remove(e->world, e);
            return;
        }
        if (fp_rand(20) < 1)
            fx_anim(fp_rand(9) == 8 ? "image/interaction/largeexplosion/explosion"
                                    : "image/interaction/explosion/explosion",
                    e->x + 50 + fp_rand(100), e->y + fp_rand(40), 0.5);
        if (ent_vspeed(e) < 0.2) ent_set_vspeed(e, ent_vspeed(e) + 0.01);
    }

    /* screen-wrap around the camera (always) */
    if (e->x < fp_camera.x - 500)                 e->x = fp_camera.x + fp_width + 400;
    if (e->x > fp_camera.x + fp_width + 500)      e->x = fp_camera.x - 400;

    ent_update(e);
}

static void sea_render(Entity *e)
{
    Sea *s = e->user;
    if (s->spr)
        gfx_draw(s->spr, e->x, e->y, 0, 0, 0,
                 ent_hspeed(e) < 0 ? -1 : 1, 1, 0xFFFFFF, 0, 0, s->ship_w, s->ship_h);
}

static Sea *sea_new(EntityType t, double x, const char *tex_id, int layer)
{
    Sea *s = calloc(1, sizeof *s);
    ent_init(&s->en.e, t);
    s->en.e.user = s;
    s->en.e.update = sea_update;
    s->en.e.render = sea_render;
    s->en.e.layer = layer;
    s->en.on_death = sea_sink_start;

    const PakAsset *a = pak_find(tex_id);
    s->ship_w = a ? a->w : 64;
    s->ship_h = a ? a->h : 32;
    s->spr = tex(tex_id);

    s->en.e.x = x;
    s->en.e.y = SPEC_WORLD_WATER_Y - s->ship_h;
    double dir = (fp_random() * 0.1) + 0.2;
    ent_set_hspeed(&s->en.e, fp_rand(2) ? dir : -dir);
    return s;
}

/* Boot — battleship (Interaction/Enemies/Boot.as) */
Entity *boot_spawn(double x)
{
    Sea *s = sea_new(ETYPE_ENEMY, x, "image/interaction/enemies/boot/ship", 2000);
    s->en.health = 60;
    s->en.score = SPEC_SCORE_BOOT;
    s->en.kill_slot = 3;
    s->en.die_snd = "audio/interaction/enemies/boot/snd";
    ent_set_hitbox(&s->en.e, 204, 32, 0, -16);
    s->t_fire = 90;
    s->burst = 0;
    s->burst_gap = 5;
    s->reload_min = 120; s->reload_rand = 120;
    s->bullet_speed = 8;
    s->aim_snapshot = true;
    s->bullet_ox = s->ship_w * 0.75; s->bullet_oy = 24;
    s->fire_snd = "audio/interaction/enemies/boot/snd2";
    s->sink_fx = "image/interaction/largeexplosion/explosion";
    return world_add(&g_game.world, &s->en.e);
}

/* Bootje — boat (Interaction/Enemies/Bootje.as) */
Entity *bootje_spawn(double x)
{
    Sea *s = sea_new(ETYPE_ENEMY, x, "image/interaction/enemies/bootje/ship", 90);
    s->en.health = 20;
    s->en.score = SPEC_SCORE_BOOTJE;
    s->en.kill_slot = 2;
    s->en.die_snd = "audio/interaction/enemies/bootje/snd";
    ent_set_hitbox(&s->en.e, 48, 20, 0, -12);
    s->t_fire = 40 + (int)fp_rand(40);
    s->burst = 0;
    s->burst_gap = 10;
    s->reload_min = 100; s->reload_rand = 100;
    s->bullet_speed = 6;
    s->aim_snapshot = false;
    s->bullet_ox = 0; s->bullet_oy = 16;
    s->fire_snd = "audio/interaction/enemies/bootje/snd2";
    s->sink_fx = "image/interaction/explosion/explosion";
    return world_add(&g_game.world, &s->en.e);
}

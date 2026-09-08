/* jet.c — Interaction/Enemies/Jet.as (fast air interceptor). one-to-one.
 * Constant-speed (14-17) missile that curves gently toward a jittered player
 * position; leaves a dense contrail; fires straight ahead. */
#include "game.h"
#include "enemy.h"
#include "fx.h"
#include "../src/audio.h"
#include <stdlib.h>

typedef struct {
    Enemy   en;
    double  myspeed;
    int     t_shoot;
    double  body_angle, wings_scale_y;
    GfxTex *body, *wings;
} Jet;

static Entity *player_e(void) { return world_first_type(&g_game.world, ETYPE_PLAYER); }
static double  roll_speed(void) { return 14.0 + fp_rand(4); }

static void jet_shoot(Jet *j)
{
    j->t_shoot = 30 + (int)fp_rand(300);
    ebullet_spawn(j->en.e.x, j->en.e.y, j->body_angle, 8);   /* straight ahead */
}

static void jet_update(Entity *e)
{
    Jet *j = e->user;
    if (j->en.health <= 0) return;

    if ((fp_frame & 1) == 0)
        fx_anim("image/interaction/fx/jettrail/trail", e->x, e->y, 0.2);

    if (j->t_shoot > 0 && --j->t_shoot == 0) jet_shoot(j);

    ent_set_speed(e, j->myspeed);              /* constant cruise */
    j->body_angle = e->direction;

    Entity *pl = player_e();
    if (pl) {
        double tx = pl->x + (double)fp_rand(10) - 5;
        double ty = pl->y + (double)fp_rand(20) - 10;
        ent_motion_add(e, fp_angle(e->x, e->y, tx, ty), 0.2);
        if (fp_distance(e->x, e->y, pl->x, pl->y) > 900) {
            j->myspeed = roll_speed();
            e->direction = fp_angle(e->x, e->y, pl->x, pl->y);
        }
    } else {
        j->myspeed = roll_speed();
        Entity *tgt = world_furthest_type(&g_game.world, ETYPE_ENEMY, e);
        if (tgt) {
            e->direction = fp_angle(e->x, e->y, tgt->x, tgt->y);
            ent_motion_add(e, fp_angle(e->x, e->y,
                           tgt->x + fp_rand(10) - 5, tgt->y + fp_rand(20) - 10), 0.2);
        }
    }

    j->wings_scale_y = sin(FP_RAD * j->body_angle);

    if (e->y > SPEC_WORLD_WATER_Y - 100) ent_set_vspeed(e, ent_vspeed(e) - 0.3);
    if (e->y > SPEC_WORLD_WATER_Y - 40)  ent_set_vspeed(e, ent_vspeed(e) - 0.7);
    if (e->y > SPEC_WORLD_WATER_Y)        { enemy_air_die(&j->en); return; }
    if (e->y < SPEC_WORLD_SPACE_Y + 200)  ent_set_vspeed(e, ent_vspeed(e) + 2);

    if (pl && world_collide(&g_game.world, ETYPE_PLAYER, e, e->x, e->y)) {
        player_getdamage(pl, SPEC_PLAYER_DAMAGE_DMG_JET_RAM);
        enemy_air_die(&j->en);
        return;
    }

    ent_update(e);
}

static void jet_render(Entity *e)
{
    Jet *j = e->user;
    if (j->body)
        gfx_draw(j->body, e->x, e->y, j->body_angle, 8, 8, 1, 1, 0xFFFFFF, 0, 0, 16, 16);
    if (j->wings)
        gfx_draw(j->wings, e->x, e->y, j->body_angle, 8, 8, 1, j->wings_scale_y,
                 0xFFFFFF, 0, 0, 16, 16);
}

Entity *jet_spawn(double x, double y)
{
    Jet *j = calloc(1, sizeof *j);
    ent_init(&j->en.e, ETYPE_ENEMY);
    j->en.e.user = j;
    j->en.e.update = jet_update;
    j->en.e.render = jet_render;
    j->en.e.layer = 90;
    j->en.e.x = x; j->en.e.y = y;
    j->en.e.gravity = 0.02;
    j->en.e.friction = 0.1;
    ent_set_hitbox(&j->en.e, 16, 16, 8, 8);

    j->en.health = 3;
    j->en.score = SPEC_SCORE_JET;
    j->en.kill_slot = 1;
    j->en.die_snd = "audio/interaction/enemies/jet/snd";
    j->en.part_tex = "image/interaction/fx/jetpart/parts";

    j->myspeed = roll_speed();
    j->t_shoot = 30 + (int)fp_rand(300);
    j->body = tex("image/interaction/enemies/jet/enemybody");
    j->wings = tex("image/interaction/enemies/jet/enemywings");
    return world_add(&g_game.world, &j->en.e);
}

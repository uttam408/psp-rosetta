/* brit.c — Interaction/Enemies/Brit.as (the staple air enemy). one-to-one.
 * TODO: no-player branch (target furthest enemy); PreRotation 4deg quantization
 * is visual-only so skipped. */
#include "game.h"
#include "enemy.h"
#include "fx.h"
#include "pool.h"
#include "../src/audio.h"

typedef struct {
    Enemy  en;
    double maxspeed, minspeed, reaction, turn;
    int    t_shoot, t_ai;
    double body_angle, wings_scale_y;
    GfxTex *body, *wings;
} Brit;

POOL(brit_pool, Brit, 96)
static void brit_recycle(Entity *e) { brit_pool_put(e->user); }

static Entity *player_e(void) { return world_first_type(&g_game.world, ETYPE_PLAYER); }

static void brit_shoot(Brit *b)
{
    Entity *pl = player_e();
    b->t_shoot = 30 + (int)fp_rand(300);
    if (!pl) return;
    snd_play("audio/interaction/enemies/brit/snd2", 1.0f, 0.0f);
    ebullet_spawn(b->en.e.x, b->en.e.y, b->body_angle, 8);
}

static void brit_perform_ai(Brit *b)
{
    Entity *pl = player_e();
    Entity *e = &b->en.e;
    if (pl) {
        b->maxspeed = 4 + fp_rand(2);
        double dist = fp_distance(e->x, e->y, pl->x, pl->y);
        if (dist > 200) {
            b->maxspeed += 2;
            b->minspeed = 2 + fp_rand(2);
            b->t_ai = 60 + (int)fp_rand(120);            /* re-arm only when far */
            b->turn = (b->turn == 0) ? (fp_rand(6) - 3.0) : 0;
        }
        b->reaction = 0.2 + fp_random() * 0.3;
    }
    /* else: TODO target furthest enemy */
}

static void brit_update(Entity *e)
{
    Brit *b = e->user;
    if (b->en.health <= 0) return;   /* enemy_hit already fired the death path */

    if (b->t_shoot > 0 && --b->t_shoot == 0) brit_shoot(b);
    if (b->t_ai   > 0 && --b->t_ai   == 0) brit_perform_ai(b);

    /* clamp speed into the AI's band (re-projected onto direction) */
    if (ent_speed(e) < b->minspeed) ent_set_speed(e, b->minspeed);
    if (ent_speed(e) > b->maxspeed) ent_set_speed(e, b->maxspeed);

    b->body_angle = e->direction;

    Entity *pl = player_e();
    if (pl) {
        ent_motion_add(e, fp_angle(e->x, e->y, pl->x, pl->y), b->reaction);
    } else {
        Entity *tgt = world_furthest_type(&g_game.world, ETYPE_ENEMY, e);
        if (tgt) e->direction = fp_angle(e->x, e->y, tgt->x, tgt->y);
    }

    e->direction += b->turn;

    b->wings_scale_y = sin(FP_RAD * b->body_angle);

    /* altitude guards */
    if (e->y > SPEC_WORLD_WATER_Y - 100) ent_set_vspeed(e, ent_vspeed(e) - 0.1);
    if (e->y > SPEC_WORLD_WATER_Y - 40)  ent_set_vspeed(e, ent_vspeed(e) - 0.2);
    if (e->y > SPEC_WORLD_WATER_Y)        { enemy_air_die(&b->en); return; }
    if (e->y < SPEC_WORLD_SPACE_Y + 200)  ent_set_vspeed(e, ent_vspeed(e) + 2);

    if (pl && world_collide(&g_game.world, ETYPE_PLAYER, e, e->x, e->y)) {
        player_getdamage(pl, SPEC_PLAYER_DAMAGE_DMG_BRIT_RAM);
        enemy_air_die(&b->en);
        return;
    }

    ent_update(e);
}

static void brit_render(Entity *e)
{
    Brit *b = e->user;
    if (b->body)
        gfx_draw(b->body, e->x, e->y, b->body_angle, 8, 8, 1, 1, 0xFFFFFF, 0, 0, 16, 16);
    if (b->wings)
        gfx_draw(b->wings, e->x, e->y, b->body_angle, 8, 8, 1, b->wings_scale_y,
                 0xFFFFFF, 0, 0, 16, 16);
}

Entity *brit_spawn(double x, double y)
{
    Brit *b = brit_pool_get();
    if (!b) return NULL;
    ent_init(&b->en.e, ETYPE_ENEMY);
    b->en.e.user = b;
    b->en.e.update = brit_update;
    b->en.e.render = brit_render;
    b->en.e.recycle = brit_recycle;
    b->en.e.layer = LAYER_ENEMY;
    b->en.e.x = x; b->en.e.y = y;
    b->en.e.gravity = 0.02;
    b->en.e.friction = 0.1;
    ent_set_hitbox(&b->en.e, 16, 16, 8, 8);

    b->en.health = 4;
    b->en.score = SPEC_SCORE_BRIT;
    b->en.kill_slot = 0;
    b->en.die_snd = "audio/interaction/enemies/brit/snd";
    b->en.part_tex = "image/interaction/fx/britpart/parts";

    b->maxspeed = 6;
    b->minspeed = 2;
    b->reaction = 0.3 + fp_random() * 0.3;
    b->turn = 0;
    b->t_shoot = 30 + (int)fp_rand(300);
    b->t_ai = (int)fp_rand(60) + 1;

    b->body = tex("image/interaction/enemies/brit/enemybody");
    b->wings = tex("image/interaction/enemies/brit/enemywings");
    return world_add(&g_game.world, &b->en.e);
}

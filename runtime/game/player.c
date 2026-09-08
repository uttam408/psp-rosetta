/* player.c — Interaction/Player.as, one-to-one.
 * Milestone scope: full flight model (justSpawned + normal flight steps 2,3,5-9,
 * 11,12,14,15,16). Not yet: weapon/bullets, smoke/particle FX, death spiral,
 * water-splash visuals. Marked TODO inline. */
#include "game.h"
#include "fx.h"
#include "../src/input.h"
#include "../src/audio.h"
#include <stdlib.h>

typedef struct {
    Entity e;
    double health;
    double turn;
    int    can_shoot;
    double shake;
    bool   is_boosting;
    bool   just_spawned;
    double body_angle;
    double wings_scale_y;
    GfxTex *body, *wings, *boost, *boost_start;
} Player;

static double space_y(void) { return SPEC_WORLD_SPACE_Y; }
static double water_y(void) { return SPEC_WORLD_WATER_Y; }

/* Player.as step 4 — one shot per (cooldown+1) frames while FIRE held. */
static void do_shooting(Player *p)
{
    if (p->can_shoot <= 0 && in_down(ACT_FIRE)) {
        snd_play("audio/interaction/player/sndshoot", 1.0f, 0.0f);
        bullet_spawn(p->e.x, p->e.y, p->body_angle);
        p->can_shoot = SPEC_PLAYER_WEAPON_COOLDOWN_FRAMES;
    } else if (p->can_shoot > 0) {
        p->can_shoot--;
    }
}

/* Player.as:380 getDamage(amount) */
void player_getdamage(Entity *e, int amount)
{
    Player *p = e->user;
    p->health -= amount;
    p->shake += amount * SPEC_PLAYER_DAMAGE_GETDAMAGE_SHAKE;
    if (p->health < 0)
        for (int i = 0; i < 5; i++)
            fx_part("image/interaction/fx/playerpart/parts", e->x, e->y,
                    fp_rand(360), 2 + fp_rand(5), (int)fp_rand(300) + 1);
}

/* --- Player.as:126-183  "just spawned" --------------------------------------*/
static void update_spawn(Player *p)
{
    Entity *e = &p->e;

    if (in_pressed(ACT_THRUST)) {
        p->turn = SPEC_PLAYER_PHYSICS_TURN_NORMAL;
        e->gravity = SPEC_PLAYER_PHYSICS_GRAVITY;
        e->friction = SPEC_PLAYER_PHYSICS_FRICTION;
        ent_set_speed(e, SPEC_PLAYER_PHYSICS_SPEED_ON_THRUST);
        p->just_spawned = false;
    } else if (!in_down(ACT_THRUST)) {
        p->turn = SPEC_PLAYER_PHYSICS_TURN_NORMAL;
        ent_set_vspeed(e, SPEC_PLAYER_PHYSICS_SPAWN_IDLE_VSPEED);
    }

    if (in_down(ACT_LEFT))  p->body_angle += SPEC_PLAYER_PHYSICS_SPAWN_TURN_STEP;
    if (in_down(ACT_RIGHT)) p->body_angle -= SPEC_PLAYER_PHYSICS_SPAWN_TURN_STEP;

    do_shooting(p);   /* Player.as:126-183 allows firing during spawn (X or SPACE) */

    fp_camera.x = e->x - fp_half_width + ent_hspeed(e) * SPEC_PLAYER_CAMERA_SPAWN_LEAD_HSPEED;
    fp_camera.y = e->y - fp_half_height;

    if (e->y < space_y() + 248) {
        p->turn = SPEC_PLAYER_PHYSICS_TURN_NORMAL;
        e->gravity = SPEC_PLAYER_PHYSICS_GRAVITY;
        e->friction = SPEC_PLAYER_PHYSICS_FRICTION;
        p->body_angle = 270;
        p->just_spawned = false;
    }

    /* TODO: shooting is allowed during spawn (X or SPACE) */

    ent_update(e);
}

/* --- Player.as:184-378  normal flight --------------------------------------*/
static void update_normal(Player *p)
{
    Entity *e = &p->e;

    /* 2. boost / thrust */
    if (p->health > 0 && in_down(ACT_THRUST)) {
        if (in_pressed(ACT_THRUST)) {
            snd_play("audio/interaction/player/sndboost", 1.0f, 0.0f);
            p->is_boosting = true;
        }
        ent_motion_add(e, p->body_angle, SPEC_PLAYER_PHYSICS_THRUST_ACCEL);
    } else {
        if (p->is_boosting)
            snd_play("audio/interaction/player/sndboostleave", 1.0f, 0.0f);
        p->is_boosting = false;
    }

    /* 3. steering */
    if (in_down(ACT_LEFT))  p->body_angle += p->turn;
    if (in_down(ACT_RIGHT)) p->body_angle -= p->turn;

    /* 4. shooting */
    do_shooting(p);

    /* 5. speed clamp — re-projects velocity onto `direction` (set at step 7) */
    if (ent_speed(e) < SPEC_PLAYER_PHYSICS_SPEED_MIN)
        ent_set_speed(e, SPEC_PLAYER_PHYSICS_SPEED_MIN);
    if (ent_speed(e) > SPEC_PLAYER_PHYSICS_SPEED_MAX)
        ent_set_speed(e, SPEC_PLAYER_PHYSICS_SPEED_MAX);

    /* 6. */
    p->turn = p->is_boosting ? SPEC_PLAYER_PHYSICS_TURN_BOOSTING
                             : SPEC_PLAYER_PHYSICS_TURN_NORMAL;

    /* 7. */
    e->direction = p->body_angle;

    /* 8. underwater */
    if (e->y > water_y()) {
        p->health -= SPEC_PLAYER_WATER_UNDERWATER_HEALTH_DRAIN;
        ent_set_vspeed(e, ent_vspeed(e) + SPEC_PLAYER_WATER_UNDERWATER_BUOYANCY);
    }
    /* 9. ceiling */
    if (e->y < space_y() + 248)
        ent_set_vspeed(e, ent_vspeed(e) + SPEC_PLAYER_WATER_CEILING_PUSH);

    /* 10. TODO water-proximity splash */

    /* 11. camera follow (Player.as:305-313) */
    {
        fp_vec lead = {0, 0};
        fp_angle_xy(&lead, p->body_angle, ent_speed(e) * SPEC_PLAYER_CAMERA_LEAD_MULT, 0, 0);
        double tx = e->x - fp_half_width + lead.x;
        double ty = e->y - fp_half_height + lead.y;
        double s = p->shake;
        fp_camera.x = tx + (tx - fp_camera.x) * SPEC_PLAYER_CAMERA_FOLLOW_LERP
                    + (double)fp_rand((uint32_t)s) - s / 2.0;
        fp_camera.y = ty + (ty - fp_camera.y) * SPEC_PLAYER_CAMERA_FOLLOW_LERP
                    + (double)fp_rand((uint32_t)s) - s / 2.0;
    }

    /* 12. repair while not firing (0 < health < 10). TODO: smoke FX */
    if (p->health > 0 && p->health < SPEC_PLAYER_INIT_HEALTH) {
        if (!in_down(ACT_FIRE))
            p->health += SPEC_PLAYER_DAMAGE_REGEN_PER_FRAME;
    }

    /* 13. death spiral — minimal: remove on water crash. TODO: FX + parts */
    if (p->health <= 0 && e->y > water_y())
        world_remove(e->world, e);

    /* 14. shake decay */
    p->shake *= SPEC_PLAYER_CAMERA_SHAKE_DECAY_MULT;
    if (p->shake > 0) p->shake -= SPEC_PLAYER_CAMERA_SHAKE_DECAY_SUB;
    else p->shake = 0;

    /* 15. wings squash */
    p->wings_scale_y = sin(FP_RAD * p->body_angle);

    /* 16. */
    ent_update(e);
}

static void player_update(Entity *e)
{
    Player *p = e->user;
    if (p->just_spawned) update_spawn(p);
    else update_normal(p);
}

static void player_render(Entity *e)
{
    Player *p = e->user;
    /* boost flame behind the ship */
    if (p->is_boosting && p->boost)
        gfx_draw(p->boost, e->x, e->y, p->body_angle, 8 + 12, 8, 1, 1,
                 0xFFFFFF, 0, 0, 16, 16);
    if (p->body)
        gfx_draw(p->body, e->x, e->y, p->body_angle, 8, 8, 1, 1,
                 0xFFFFFF, 0, 0, 16, 16);
    if (p->wings)
        gfx_draw(p->wings, e->x, e->y, p->body_angle, 8, 8, 1, p->wings_scale_y,
                 0xFFFFFF, 0, 0, 16, 16);
}

Entity *player_spawn(double px, double py)
{
    Player *p = calloc(1, sizeof *p);
    ent_init(&p->e, ETYPE_PLAYER);
    p->e.user = p;
    p->e.update = player_update;
    p->e.render = player_render;
    p->e.layer = 100;

    p->e.x = px;
    p->e.y = py;
    p->health = SPEC_PLAYER_INIT_HEALTH;
    p->turn = SPEC_PLAYER_INIT_TURN;
    p->can_shoot = SPEC_PLAYER_INIT_CAN_SHOOT;
    p->shake = SPEC_PLAYER_INIT_SHAKE;
    p->just_spawned = true;
    p->body_angle = SPEC_PLAYER_INIT_SPAWN_ANGLE;
    p->wings_scale_y = sin(FP_RAD * p->body_angle);
    ent_set_vspeed(&p->e, SPEC_PLAYER_INIT_SPAWN_VSPEED);
    ent_set_hitbox(&p->e, SPEC_PLAYER_INIT_HITBOX_W, SPEC_PLAYER_INIT_HITBOX_H,
                   SPEC_PLAYER_INIT_HITBOX_OX, SPEC_PLAYER_INIT_HITBOX_OY);

    p->body = tex("image/interaction/player/playerbody");
    p->wings = tex("image/interaction/player/playerwings");
    p->boost = tex("image/interaction/player/boost");
    p->boost_start = tex("image/interaction/player/booststart");

    snd_play("audio/interaction/player/sndspawn", 1.0f, 0.0f);
    return world_add(&g_game.world, &p->e);
}

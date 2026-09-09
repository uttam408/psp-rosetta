/* bullet.c — Interaction/Bullet.as (player weapon) + Interaction/EBullet.as. */
#include "game.h"
#include "enemy.h"
#include "fx.h"
#include "pool.h"
#include "../src/audio.h"

static double player_x(void)
{
    Entity *p = world_first_type(&g_game.world, ETYPE_PLAYER);
    return p ? p->x : 0;
}

static void splash(double x)
{
    fx_anim("image/interaction/fx/watersplash/splash", x, SPEC_WORLD_WATER_Y, 0.3);
}

/* --- player Bullet -------------------------------------------------------- */
typedef struct { Entity e; int life; GfxTex *t; } Bullet;
POOL(bullet_pool, Bullet, 96)
static void bullet_recycle(Entity *e) { bullet_pool_put(e->user); }

static void bullet_update(Entity *e)
{
    Bullet *b = e->user;
    ent_update(e);

    Entity *hit = world_collide(&g_game.world, ETYPE_ENEMY, e, e->x, e->y);
    if (hit) {
        float pan = (float)fp_scale_clamp(e->x - player_x(), -300, 300, -0.25, 0.25);
        snd_play("audio/interaction/bullet/snd", 1.0f, pan);
        fx_anim("image/interaction/fx/bullethit/bullethit", e->x, e->y, 0.5);
        enemy_hit((Enemy *)hit->user, SPEC_PLAYER_WEAPON_BULLET_DAMAGE);
        world_remove(e->world, e);
        return;
    }
    if (e->y > SPEC_WORLD_WATER_Y) { splash(e->x); world_remove(e->world, e); return; }
    if (--b->life <= 0) {
        fx_anim("image/interaction/fx/bullethit/bullethit", e->x, e->y, 0.5);
        world_remove(e->world, e);
    }
}

static void bullet_render(Entity *e)
{
    Bullet *b = e->user;
    if (b->t) gfx_draw(b->t, e->x, e->y, 0, 8, 8, 1, 1, 0xFFFFFF, 0, 0, 16, 16);
}

Entity *bullet_spawn(double x, double y, double angle)
{
    Bullet *b = bullet_pool_get();
    if (!b) return NULL;
    ent_init(&b->e, ETYPE_BULLET);
    b->e.user = b;
    b->e.update = bullet_update;
    b->e.render = bullet_render;
    b->e.recycle = bullet_recycle;
    b->e.layer = LAYER_BEHIND;
    b->e.x = x; b->e.y = y;
    ent_set_hitbox(&b->e, SPEC_PLAYER_WEAPON_BULLET_HITBOX,
                   SPEC_PLAYER_WEAPON_BULLET_HITBOX, 8, 8);
    b->life = SPEC_PLAYER_WEAPON_BULLET_LIFE;
    b->t = tex("image/interaction/bullet/bullet");
    ent_motion_add(&b->e, angle, SPEC_PLAYER_WEAPON_BULLET_SPEED);
    b->e.x += ent_hspeed(&b->e);      /* Player.as: advance one step on spawn */
    b->e.y += ent_vspeed(&b->e);
    return world_add(&g_game.world, &b->e);
}

/* --- enemy EBullet ------------------------------------------------------- */
typedef struct { Entity e; int life; const PakAsset *a; GfxTex *t; double fr; } EBullet;
POOL(ebullet_pool, EBullet, 128)
static void ebullet_recycle(Entity *e) { ebullet_pool_put(e->user); }

static void ebullet_update(Entity *e)
{
    EBullet *b = e->user;
    ent_update(e);
    b->fr += 0.5;

    Entity *pl = world_collide(&g_game.world, ETYPE_PLAYER, e, e->x, e->y);
    if (pl) {
        fx_anim("image/interaction/fx/bullethit/bullethit", e->x, e->y, 0.5);
        player_getdamage(pl, SPEC_PLAYER_DAMAGE_DMG_EBULLET);
        world_remove(e->world, e);
        return;
    }
    if (e->y > SPEC_WORLD_WATER_Y) { splash(e->x); world_remove(e->world, e); return; }
    if (--b->life <= 0) world_remove(e->world, e);
}

static void ebullet_render(Entity *e)
{
    EBullet *b = e->user;
    if (!b->t || !b->a || !b->a->nframes) return;
    PakRect r = b->a->frames[((int)b->fr) % b->a->nframes];
    gfx_draw(b->t, e->x, e->y, 0, r.w / 2.0, r.h / 2.0, 1, 1, 0xFFFFFF,
             r.x, r.y, r.w, r.h);
}

Entity *ebullet_spawn(double x, double y, double angle, double speed)
{
    EBullet *b = ebullet_pool_get();
    if (!b) return NULL;
    ent_init(&b->e, ETYPE_EBULLET);
    b->e.user = b;
    b->e.update = ebullet_update;
    b->e.render = ebullet_render;
    b->e.recycle = ebullet_recycle;
    b->e.layer = LAYER_BEHIND;
    b->e.x = x; b->e.y = y;
    ent_set_hitbox(&b->e, 8, 8, 4, 4);
    b->life = 120;                    /* EBullet.as Alarm(120) */
    b->a = pak_find("image/interaction/ebullet/bullet");
    b->t = tex("image/interaction/ebullet/bullet");
    ent_motion_add(&b->e, angle, speed);
    return world_add(&g_game.world, &b->e);
}

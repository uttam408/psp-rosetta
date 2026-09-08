/* game.h — Luftrauser one-to-one port. Shared context + entity factories.
 * Every constant comes from gen/spec_luftrauser.h (spec/luftrauser.toml). */
#ifndef RT_GAME_H
#define RT_GAME_H

#include "../src/world.h"
#include "../src/gfx.h"
#include "gen/spec_luftrauser.h"

typedef struct Game {
    World  world;
    int    game_score;
    int    kills[4];       /* brit, jet, bootje, boot */
    double game_hard;
    int    high_score;

    Entity *water;         /* world-space y == SPEC_WORLD_WATER_Y band */
    Entity *space;
    bool    music_started;

    bool    spawning;      /* combat begun (UBoot gone)               */
    int     t_spawn;       /* frames to next spawnMoreEnemies tick    */
    bool    game_over;     /* player entity removed                   */
} Game;

extern Game g_game;

/* cached texture by pak id (NULL on miss) */
GfxTex *tex(const char *id);

/* factories — each allocates, wires update/render, world_add's, returns the Entity */
Entity *player_spawn(double x, double y);
Entity *uboot_spawn(void);
Entity *backdrop_spawn_water(void);
Entity *backdrop_spawn_space(void);
Entity *bullet_spawn(double x, double y, double angle);
Entity *ebullet_spawn(double x, double y, double angle, double speed);
Entity *brit_spawn(double x, double y);
Entity *boot_spawn(double x);      /* battleship */
Entity *bootje_spawn(double x);    /* boat */

void player_getdamage(Entity *player, int amount);   /* Player.as:380 getDamage */
void player_health_delta(Entity *player, double d);  /* raw, no shake */
double player_health(Entity *player);
void game_begin_combat(void);   /* UBoot.removeThis -> spawnEnemies + start loop */

void game_start(void);   /* build the attract-mode world */
void game_tick(void);    /* per fixed frame: orchestration around world_update  */
void game_draw(void);    /* clear + world_render + HUD                          */

#endif

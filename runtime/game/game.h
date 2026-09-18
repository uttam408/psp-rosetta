/* game.h — Luftrauser one-to-one port. Shared context + entity factories.
 * Every constant comes from gen/spec_luftrauser.h (spec/luftrauser.toml). */
#ifndef RT_GAME_H
#define RT_GAME_H

#include "../src/world.h"
#include "../src/gfx.h"
#include "gen/spec_luftrauser.h"

/* draw order — FlashPunk convention: higher layer renders first (further back) */
enum {
    LAYER_SPACE  = 5000,
    LAYER_CLOUD  = 4000,
    LAYER_BEHIND = 2000,   /* Bullet / EBullet / Boot / Bootje — AS3 layer=2000 */
    LAYER_ENEMY  = 300,
    LAYER_FX     = 200,
    LAYER_PART   = 150,
    LAYER_PLAYER = 100,
    LAYER_BLURB  = 50,
    LAYER_WATER  = 0,      /* front-most: draws OVER anything below the surface
                             * (sinking ships, the player underwater) so they
                             * visually submerge instead of floating on top */
};

typedef struct Game {
    World  world;
    int    game_score;
    int    kills[4];       /* brit, jet, bootje, boot */
    real game_hard;
    int    high_score;

    Entity *water;         /* world-space y == SPEC_WORLD_WATER_Y band */
    Entity *space;
    bool    music_started;

    bool    spawning;      /* combat begun (UBoot gone)               */
    int     t_spawn;       /* frames to next spawnMoreEnemies tick    */
    bool    game_over;     /* player entity removed                   */
    bool    paused;        /* Start toggles; freezes world_update     */
} Game;

extern Game g_game;

/* cached texture by pak id (NULL on miss) */
GfxTex *tex(const char *id);

/* factories — each allocates, wires update/render, world_add's, returns the Entity */
Entity *player_spawn(real x, real y);
Entity *uboot_spawn(void);
Entity *backdrop_spawn_water(void);
Entity *backdrop_spawn_space(void);
Entity *bullet_spawn(real x, real y, real angle);
Entity *ebullet_spawn(real x, real y, real angle, real speed);
Entity *brit_spawn(real x, real y);
Entity *jet_spawn(real x, real y);
Entity *boot_spawn(real x);      /* battleship */
Entity *bootje_spawn(real x);    /* boat */
Entity *cloud_spawn(void);

void player_getdamage(Entity *player, int amount);   /* Player.as:380 getDamage */
void player_health_delta(Entity *player, real d);  /* raw, no shake */
real player_health(Entity *player);
void game_begin_combat(void);   /* UBoot.removeThis -> spawnEnemies + start loop */

void game_start(void);   /* build the attract-mode world */
void game_tick(void);    /* per fixed frame: orchestration around world_update  */
void game_draw(void);    /* clear + world_render + HUD                          */
void game_splash_start(void);   /* optional boot logo (PSP) */
void game_set_fps(float fps);   /* platform reports real (render) fps for the HUD */

#endif

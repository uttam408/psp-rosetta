/* game.c — Worlds/Game.as orchestration (attract -> launch).
 * Milestone: build the world, run world_update, render. Not yet: enemy spawning,
 * difficulty, scoring, HUD, game-over timeline (TODO — port-spec §2,§5,§6,§7). */
#include "game.h"
#include "../src/input.h"
#include "../src/audio.h"
#include <stdio.h>
#include <string.h>

Game g_game;

/* --- texture cache --------------------------------------------------------- */
#define TEXCACHE 64
static struct { char id[96]; GfxTex *t; } g_texcache[TEXCACHE];
static int g_texcache_n;

GfxTex *tex(const char *id)
{
    for (int i = 0; i < g_texcache_n; i++)
        if (strcmp(g_texcache[i].id, id) == 0) return g_texcache[i].t;
    const PakAsset *a = pak_find(id);
    GfxTex *t = a ? gfx_tex_load(a) : NULL;
    if (g_texcache_n < TEXCACHE) {
        snprintf(g_texcache[g_texcache_n].id, sizeof g_texcache[0].id, "%s", id);
        g_texcache[g_texcache_n].t = t;
        g_texcache_n++;
    }
    return t;
}

/* --- lifecycle ------------------------------------------------------------- */
void game_start(void)
{
    memset(&g_game, 0, sizeof g_game);
    world_init(&g_game.world);
    fp_seed(1);   /* TODO: match FlashPunk's seed init for replay parity */

    g_game.space = backdrop_spawn_space();
    g_game.water = backdrop_spawn_water();
    uboot_spawn();
}

/* Game.as:372-377 — 2 Brits + start the spawn loop */
void game_begin_combat(void)
{
    double w = fp_width;
    for (int i = 0; i < SPEC_SPAWN_FIRST_BRITS; i++)
        brit_spawn(fp_choose2(fp_camera.x - SPEC_SPAWN_SPAWN_X_OFFSCREEN,
                              fp_camera.x + w + SPEC_SPAWN_SPAWN_X_OFFSCREEN),
                   SPEC_SPAWN_BRIT_SPAWN_Y_BASE + fp_rand(SPEC_SPAWN_BRIT_SPAWN_Y_RAND));
    g_game.spawning = true;
    g_game.t_spawn = SPEC_SPAWN_FIRST_ALARM_MIN + (int)fp_rand(SPEC_SPAWN_FIRST_ALARM_RAND);
}

/* Game.as:308-365 — spawnMoreEnemies() */
static void spawn_more_enemies(void)
{
    g_game.t_spawn = 30 + (int)fp_rand(60);            /* re-arm 30-89 frames  */
    double sx = fp_choose2(fp_camera.x - 500, fp_camera.x + fp_width + 500);
    double sy = 100 + fp_rand(500);
    g_game.game_hard += SPEC_DIFFICULTY_GAMEHARD_PER_TICK;

    int live = world_count_type(&g_game.world, ETYPE_ENEMY);
    if (live >= g_game.game_hard || live >= SPEC_DIFFICULTY_ENEMY_COUNT_CAP) return;

    double gh = g_game.game_hard;
    int cls;
    if (gh < 3)       cls = 0;
    else if (gh < 7)  { const int c[] = {0, 0, 1};             cls = c[fp_rand(3)]; }
    else if (gh < 15) { const int c[] = {0, 1, 2};             cls = c[fp_rand(3)]; }
    else              { const int c[] = {0, 0, 1, 1, 2, 2, 3}; cls = c[fp_rand(7)]; }

    if (cls == 0) {
        for (int i = 0; i <= (int)(gh / 5) + 1; i++) brit_spawn(sx, sy);
    } else {
        brit_spawn(sx, sy);   /* TODO: Bootje (1) / Jet (2) / Boot (3) */
    }
}

void game_tick(void)
{
    world_update(&g_game.world);

    Entity *pl = world_first_type(&g_game.world, ETYPE_PLAYER);

    if (g_game.spawning && !g_game.game_over && (pl || !g_game.game_over)) {
        if (pl && --g_game.t_spawn <= 0) spawn_more_enemies();
    }
    if (g_game.spawning && !pl && !g_game.game_over) {
        g_game.game_over = true;   /* TODO: game-over timeline (port-spec §2e) */
    }

    /* music ducks with altitude once flying (Game.as:301-304) */
    if (pl && g_game.music_started)
        snd_music_volume((float)fp_scale_clamp(pl->y, 100, 600, 0.5, 0.75));
}

void game_draw(void)
{
    gfx_frame_begin(SPEC_ENGINE_CLEAR_RGB);
    world_render(&g_game.world);
    /* TODO: HUD (SCORE), attract text, game-over overlay */
    gfx_frame_end();
}

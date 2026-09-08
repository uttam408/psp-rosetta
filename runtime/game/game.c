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

void game_tick(void)
{
    world_update(&g_game.world);

    /* music ducks with altitude once flying (Game.as:301-304) */
    Entity *pl = world_first_type(&g_game.world, ETYPE_PLAYER);
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

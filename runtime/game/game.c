/* game.c — Worlds/Game.as orchestration (attract -> launch).
 * Milestone: build the world, run world_update, render. Not yet: enemy spawning,
 * difficulty, scoring, HUD, game-over timeline (TODO — port-spec §2,§5,§6,§7). */
#include "game.h"
#include "text.h"
#include "logo_data.h"
#include "../src/input.h"
#include "../src/audio.h"
#include <stdio.h>
#include <string.h>

#define HUD_RGB   0x8D420D   /* Game.as HUD colour */
#define BLURB_RGB 0x610C1D

static float g_fps;
void game_set_fps(float fps) { g_fps = fps; }

/* Boot splash: the PSPRosetta logo fades in, holds, fades out (any button skips).
 * Platform-enabled (PSP) so the headless SDL screenshot tests are unaffected. */
#define SPLASH_IN    20
#define SPLASH_HOLD  45
#define SPLASH_OUT   25
static int g_splash = -1;                 /* -1 = off, else ticks elapsed */
static GfxTex *g_logo;

void game_splash_start(void)
{
    g_logo = gfx_tex_from_pixels(logo_px, LOGO_TEX_W, LOGO_TEX_H);
    g_splash = g_logo ? 0 : -1;
}

static void splash_tick(void)
{
    if (in_pressed(ACT_FIRE) || in_pressed(ACT_THRUST) || in_pressed(ACT_START) ||
        ++g_splash >= SPLASH_IN + SPLASH_HOLD + SPLASH_OUT)
        g_splash = -1;
}

static void splash_draw(void)
{
    int t = g_splash;
    int lvl = 255;
    if (t < SPLASH_IN) lvl = t * 255 / SPLASH_IN;
    else if (t >= SPLASH_IN + SPLASH_HOLD) lvl = (SPLASH_IN + SPLASH_HOLD + SPLASH_OUT - t) * 255 / SPLASH_OUT;
    if (lvl < 0) lvl = 0;
    uint32_t tint = (uint32_t)lvl * 0x010101u;
    gfx_frame_begin(0x000000);
    gfx_draw(g_logo, fp_camera.x + (fp_width - LOGO_W) / 2, fp_camera.y + (fp_height - LOGO_H) / 2,
             0, 0, 0, 1, 1, tint, 0, 0, LOGO_W, LOGO_H);
    gfx_frame_end();
}

/* --- high score (SharedObject "Luftrauser" / "Highscore" -> a 4-byte file) -- */
static int hiscore_load(void)
{
    FILE *f = fopen("hiscore.dat", "rb");
    int v = 0;
    if (f) { if (fread(&v, sizeof v, 1, f) != 1) v = 0; fclose(f); }
    return v < 0 ? 0 : v;
}
static void hiscore_save(int v)
{
    FILE *f = fopen("hiscore.dat", "wb");
    if (f) { fwrite(&v, sizeof v, 1, f); fclose(f); }
}

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
    static bool text_ready = false;
    if (!text_ready) { text_init(); text_ready = true; }

    int hi = hiscore_load();
    world_clear(&g_game.world);          /* recycle the previous game's entities */
    memset(&g_game, 0, sizeof g_game);
    world_init(&g_game.world);
    g_game.world.clip_on = true;
    g_game.world.clip_layer = LAYER_WATER;
    g_game.world.clip_world_y = SPEC_WORLD_WATER_Y;
    fp_seed(1);   /* TODO: match FlashPunk's seed init for replay parity */
    g_game.high_score = hi;

    g_game.space = backdrop_spawn_space();
    g_game.water = backdrop_spawn_water();
    for (int i = 0; i < 6; i++) cloud_spawn();   /* Game ctor: 6 background clouds */
    uboot_spawn();
}

/* Game.as:372-377 — 2 Brits + start the spawn loop */
void game_begin_combat(void)
{
    real w = fp_width;
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
    real sx = fp_choose2(fp_camera.x - 500, fp_camera.x + fp_width + 500);
    real sy = 100 + fp_rand(500);
    g_game.game_hard += SPEC_DIFFICULTY_GAMEHARD_PER_TICK;

    int live = world_count_type(&g_game.world, ETYPE_ENEMY);
    if (live >= g_game.game_hard || live >= SPEC_DIFFICULTY_ENEMY_COUNT_CAP) return;

    real gh = g_game.game_hard;
    int cls;
    if (gh < 3)       cls = 0;
    else if (gh < 7)  { const int c[] = {0, 0, 1};             cls = c[fp_rand(3)]; }
    else if (gh < 15) { const int c[] = {0, 1, 2};             cls = c[fp_rand(3)]; }
    else              { const int c[] = {0, 0, 1, 1, 2, 2, 3}; cls = c[fp_rand(7)]; }

    switch (cls) {
    case 0:                                        /* Brit swarm */
        for (int i = 0; i <= (int)(gh / 5) + 1; i++) brit_spawn(sx, sy);
        break;
    case 1: bootje_spawn(sx); break;               /* one boat */
    case 2:                                        /* Jet swarm */
        for (int i = 0; i <= (int)(gh / 15) + 1; i++)
            jet_spawn(sx + fp_rand(100) - 50, sy + fp_rand(100) - 50);
        break;
    case 3: boot_spawn(sx); break;                 /* one battleship */
    }
}

void game_tick(void)
{
    if (g_splash >= 0) { splash_tick(); return; }
    if (in_pressed(ACT_START) && !g_game.game_over)
        g_game.paused = !g_game.paused;
    if (g_game.paused) return;                     /* freeze the sim entirely */

    world_update(&g_game.world);

    Entity *pl = world_first_type(&g_game.world, ETYPE_PLAYER);

    if (g_game.game_over) {
        /* accept Cross (PSP "X button") or Fire — the on-screen "X TO RESTART"
           refers to the original keyboard binding, not the PSP Cross button,
           so take either. */
        if (in_pressed(ACT_FIRE) || in_pressed(ACT_THRUST)) {
            snd_music_stop();
            game_start();
        }
        return;
    }

    if (g_game.spawning && pl && --g_game.t_spawn <= 0) spawn_more_enemies();

    if (g_game.spawning && !pl) {
        /* player entity gone -> game over (port-spec §2e; full timeline is TODO) */
        if (g_game.game_score > g_game.high_score) {
            g_game.high_score = g_game.game_score;
            hiscore_save(g_game.high_score);
        }
        g_game.game_over = true;
    }

    /* music ducks with altitude once flying (Game.as:301-304) */
    if (pl && g_game.music_started)
        snd_music_volume((float)fp_scale_clamp(pl->y, 100, 600, 0.5, 0.75));
}

static void hud(void)
{
    char buf[96];
    Entity *pl = world_first_type(&g_game.world, ETYPE_PLAYER);
    bool attract = world_count_type(&g_game.world, ETYPE_UBOOT) > 0 && !pl;

    snprintf(buf, sizeof buf, "%d FPS  %d ENT",
             (int)(g_fps + 0.5f), world_count(&g_game.world));
    text_fill(fp_width - 8 - text_width(buf) - 3, 5, text_width(buf) + 6, 14, 0x1F2A9C);
    text_draw(buf, fp_width - 8, 8, 0xFFFFFF, TEXT_RIGHT);

    if (g_game.game_over) {
        real cx = fp_half_width, cy = fp_half_height - 40;
        text_draw("GAME OVER", cx, cy, HUD_RGB, TEXT_CENTER);
        snprintf(buf, sizeof buf,
                 "KILLS %d\nPLANES %d   JETS %d\nBOAT %d   SHIP %d",
                 g_game.kills[0] + g_game.kills[1] + g_game.kills[2] + g_game.kills[3],
                 g_game.kills[0], g_game.kills[1], g_game.kills[2], g_game.kills[3]);
        text_draw(buf, cx, cy + 24, HUD_RGB, TEXT_CENTER);
        snprintf(buf, sizeof buf, "SCORE %d    BEST %d",
                 g_game.game_score, g_game.high_score);
        text_draw(buf, cx, cy + 80, HUD_RGB, TEXT_CENTER);
        text_draw("X TO RESTART", cx, cy + 100, BLURB_RGB, TEXT_CENTER);
        return;
    }

    if (attract) {
        real cx = fp_half_width;
        text_draw("PRESS UP TO LAUNCH", cx, 70, HUD_RGB, TEXT_CENTER);
        text_draw("ARROWS + X    RELEASE X TO REPAIR", cx, 86, BLURB_RGB, TEXT_CENTER);
        snprintf(buf, sizeof buf, "BEST %d", g_game.high_score);
        text_draw(buf, cx, 110, HUD_RGB, TEXT_CENTER);
        return;
    }

    if (pl && g_game.game_score > 0) {
        snprintf(buf, sizeof buf, "SCORE %d", g_game.game_score);
        text_draw(buf, 8, 8, HUD_RGB, TEXT_LEFT);
    }

    if (g_game.paused)
        text_draw("PAUSED\nSTART TO RESUME", fp_half_width, fp_half_height - 8,
                  HUD_RGB, TEXT_CENTER);
}

void game_draw(void)
{
    if (g_splash >= 0) { splash_draw(); return; }
    gfx_frame_begin(SPEC_ENGINE_CLEAR_RGB);
    world_render(&g_game.world);
    hud();
    gfx_frame_end();
}

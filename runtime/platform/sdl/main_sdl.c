/* main_sdl.c — desktop host: fixed 30 Hz sim, vsync'd render.
 *   ./luftrauser [assets.pak]
 * Controls: Arrows/WASD steer + thrust, X/Space fire, Enter start, Esc quit. */
#include <SDL.h>
#include <stdio.h>
#include "../../src/fp.h"
#include "../../src/pak.h"
#include "../../src/input.h"
#include "../../src/audio.h"
#include "../../src/gfx.h"
#include "../../game/game.h"

/* headless capture: --shot <ticks> <out.bmp> [inputscript]
 * inputscript: one char/tick, last char repeats — T thrust, L left, R right,
 * F fire, S start, . none. */
static int run_shot(int ticks, const char *out, const char *script);

int main(int argc, char **argv)
{
    const char *pak = argc > 1 ? argv[1] : "assets.pak";
    if (!pak_open(pak)) {
        fprintf(stderr, "could not open pak: %s\n", pak);
        return 1;
    }
    printf("pak: %d assets\n", pak_count());

    fp_init(480, 272);   /* PSP viewport; game space is 480x320, view is native 272 */
    if (!gfx_init("Luftrauser \xC2\xB7 psp-rosetta", 480, 272, 2)) return 1;
    snd_init();
    game_start();

    for (int i = 2; i + 2 < argc; i++) {
        if (SDL_strcmp(argv[i], "--shot") == 0) {
            int rc = run_shot(SDL_atoi(argv[i + 1]), argv[i + 2],
                              i + 3 < argc ? argv[i + 3] : "");
            snd_shutdown(); gfx_shutdown(); pak_close();
            return rc;
        }
    }

    const double DT = 1.0 / (double)SPEC_ENGINE_FPS;
    const double FREQ = (double)SDL_GetPerformanceFrequency();
    uint64_t prev = SDL_GetPerformanceCounter();
    double accum = 0;
    bool running = true;

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) running = false;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)
                running = false;
        }
        const Uint8 *k = SDL_GetKeyboardState(NULL);

        uint64_t now = SDL_GetPerformanceCounter();
        double dt = (double)(now - prev) / FREQ;
        prev = now;
        if (dt > 0.25) dt = 0.25;
        accum += dt;

        int steps = 0;
        while (accum >= DT && steps < SPEC_ENGINE_MAX_FRAME_SKIP) {
            in_begin_frame();
            in_set(ACT_THRUST, k[SDL_SCANCODE_UP] || k[SDL_SCANCODE_W]);
            in_set(ACT_LEFT,   k[SDL_SCANCODE_LEFT] || k[SDL_SCANCODE_A]);
            in_set(ACT_RIGHT,  k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]);
            in_set(ACT_FIRE,   k[SDL_SCANCODE_X] || k[SDL_SCANCODE_SPACE]);
            in_set(ACT_START,  k[SDL_SCANCODE_RETURN]);
            in_set(ACT_MUTE,   k[SDL_SCANCODE_M]);
            game_tick();
            accum -= DT;
            steps++;
        }
        if (steps == SPEC_ENGINE_MAX_FRAME_SKIP) accum = 0;

        game_draw();
    }

    snd_shutdown();
    gfx_shutdown();
    pak_close();
    return 0;
}

static void feed(char c)
{
    in_begin_frame();
    in_set(ACT_THRUST, c == 'T');
    in_set(ACT_LEFT,   c == 'L');
    in_set(ACT_RIGHT,  c == 'R');
    in_set(ACT_FIRE,   c == 'F');
    in_set(ACT_START,  c == 'S');
}

static int run_shot(int ticks, const char *out, const char *script)
{
    char buf[4096];
    if (script[0] == '@') {                  /* @file — read script from a file */
        FILE *f = fopen(script + 1, "rb");
        size_t n = f ? fread(buf, 1, sizeof buf - 1, f) : 0;
        if (f) fclose(f);
        while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) n--;
        buf[n] = 0;
        script = buf;
    }
    size_t sl = SDL_strlen(script);
    for (int i = 0; i < ticks; i++) {
        char c = sl ? script[(size_t)i < sl ? (size_t)i : sl - 1] : '.';
        feed(c);
        game_tick();
    }
    game_draw();
    bool ok = gfx_save_bmp(out);
    printf("shot: %d ticks, script=\"%s\" -> %s (%s)\n",
           ticks, script, out, ok ? "ok" : "FAIL");
    return ok ? 0 : 2;
}

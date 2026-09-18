/* main_psp.c — PSP entry: module info, exit callback, fixed 30 Hz sim loop.
 *   assets.pak is read from the EBOOT's directory on the Memory Stick. */
#include <pspkernel.h>
#include <pspdebug.h>
#include <psputils.h>
#include <stdio.h>
#include <math.h>

#include "../../src/fp.h"
#include "../../src/pak.h"
#include "../../src/audio.h"
#include "../../src/gfx.h"
#include "../../game/game.h"
#include "../../game/enemy.h"
#include "../../game/fx.h"

PSP_MODULE_INFO("Luftrauser", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);   /* leave 1 MB for the system, rest to malloc */

void psp_input_init(void);
void psp_input_poll(void);

static volatile int g_running = 1;

static int exit_cb(int a1, int a2, void *arg) { (void)a1; (void)a2; (void)arg; g_running = 0; return 0; }
static int cb_thread(SceSize s, void *p)
{
    (void)s; (void)p;
    int id = sceKernelCreateCallback("exit", exit_cb, NULL);
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}
static void setup_callbacks(void)
{
    int th = sceKernelCreateThread("cb", cb_thread, 0x11, 0xFA0, 0, 0);
    if (th >= 0) sceKernelStartThread(th, 0, 0);
}


extern int gfx_dbg_nocap, gfx_dbg_skipdraw;
extern unsigned long long gfx_dbg_sync_us;

/* Debug bench: if "bench.on" exists next to the EBOOT, stuff ~270 entities and
 * log timings for several configurations to bench.txt, then exit. */
static void run_bench(void)
{
    FILE *out = fopen("bench.txt", "w");
    if (!out) return;
    for (int i = 0; i < 100; i++) game_tick();
    fprintf(out, "baseline ents=%d\n", world_count(&g_game.world));
    static const struct { const char *name; int stuff, tick, skipdraw; } cfg[] = {
        {"base  9 ents  tick+draw", 0, 1, 0},
        {"270 ents  tick+draw   ", 1, 1, 0},
        {"270 ents  draw only   ", 0, 0, 0},
        {"270 ents  tick only   ", 0, 1, 1},
        {"270 ents  draw calls off", 0, 0, 1},
    };
    {
        game_start();
        for (int i = 0; i < 100; i++) game_tick();
        for (int i = 0; i < 60; i++) brit_spawn(100 + i, 100);
        game_tick();
        volatile float fa = 0.7f, fr = 0; volatile double da = 0.7, dr = 0;
        const int M = 2000;
        unsigned long long t0;
#define TIME(label, expr) t0 = sceKernelGetSystemTimeWide(); for (int i = 0; i < M; i++) { expr; } \
        fprintf(out, "%-22s %8.3f us/call\n", label, (sceKernelGetSystemTimeWide() - t0) / (double)M);
        TIME("atan2f", fr = atan2f(fa + i * 1e-4f, 0.3f))
        TIME("sinf", fr = sinf(fa + i * 1e-4f))
        TIME("cosf", fr = cosf(fa + i * 1e-4f))
        TIME("sqrtf", fr = sqrtf(fa + i * 1e-4f))
        TIME("sin (double)", dr = sin(da + i * 1e-4))
        TIME("atan2 (double)", dr = atan2(da + i * 1e-4, 0.3))
        TIME("double mul+add", dr = da * 1.0001 + i)
        TIME("fp_random", dr = fp_random())
        TIME("world_first_type(PL)", (void)world_first_type(&g_game.world, ETYPE_PLAYER))
        TIME("fp_angle", dr = fp_angle(1, 2, 3 + i * 1e-3, 4))
        fprintf(out, "ents=%d\n", world_count(&g_game.world));
        fflush(out);
    }
    gfx_dbg_nocap = 1;
    static const char *names[] = {"brit x60", "jet x30", "ebullet x90", "smoke x90", "none"};
    for (int c = 0; c < 5; c++) {
        game_start();
        for (int i = 0; i < 100; i++) game_tick();
        int base = world_count(&g_game.world);
        if (c == 0) for (int i = 0; i < 60; i++) brit_spawn(fp_camera.x + 40 + (i % 12) * 30, fp_camera.y + 20 + (i / 12) * 30);
        if (c == 1) for (int i = 0; i < 30; i++) jet_spawn(fp_camera.x + 40 + (i % 10) * 40, fp_camera.y + 150 + (i / 10) * 20);
        if (c == 2) for (int i = 0; i < 90; i++) ebullet_spawn(fp_camera.x + 20 + (i % 30) * 15, fp_camera.y + 60 + (i / 30) * 40, i * 12, 0.5);
        if (c == 3) for (int i = 0; i < 90; i++) fx_smoke(fp_camera.x + 20 + (i % 30) * 15, fp_camera.y + 100 + (i / 30) * 30, i * 7, 0.3);
        game_tick();
        int n0 = world_count(&g_game.world);
        const int N = 20;
        unsigned long long tt = 0;
        for (int i = 0; i < N; i++) {
            unsigned long long a = sceKernelGetSystemTimeWide();
            game_tick();
            tt += sceKernelGetSystemTimeWide() - a;
        }
        fprintf(out, "%-12s base=%d ents=%d->%d | tick %8.3f ms\n", names[c], base, n0,
                world_count(&g_game.world), tt / 1000.0 / N);
        fflush(out);
    }
    fprintf(out, "done\n");
    fclose(out);
}

int main(void)
{
    setup_callbacks();

    if (!pak_open("assets.pak")) {
        pspDebugScreenInit();
        pspDebugScreenPrintf("assets.pak not found next to EBOOT.PBP\n");
        sceKernelDelayThread(4 * 1000 * 1000);
        sceKernelExitGame();
        return 0;
    }

    fp_init(480, 272);
    gfx_init("Luftrauser", 480, 272, 1);
    snd_init();
    psp_input_init();
    game_start();
    {
        FILE *f = fopen("bench.on", "r");
        if (f) { fclose(f); run_bench(); sceKernelExitGame(); return 0; }
    }
    game_splash_start();

    const double DT_US = 1000000.0 / (double)SPEC_ENGINE_FPS;
    double accum = 0;
    unsigned long long prev = sceKernelGetSystemTimeWide();
    unsigned long long prev_render = prev;
    float fps_ema = (float)SPEC_ENGINE_FPS;

    while (g_running) {
        unsigned long long now = sceKernelGetSystemTimeWide();
        double dt = (double)(now - prev);
        prev = now;
        if (dt > 250000.0) dt = 250000.0;
        accum += dt;

        int steps = 0;
        while (accum >= DT_US && steps < SPEC_ENGINE_MAX_FRAME_SKIP) {
            psp_input_poll();
            game_tick();
            accum -= DT_US;
            steps++;
        }
        if (steps == SPEC_ENGINE_MAX_FRAME_SKIP) accum = 0;

        /* real (render) fps — this is what stutter shows up as, not sim rate */
        unsigned long long rnow = sceKernelGetSystemTimeWide();
        double rdt = (double)(rnow - prev_render);
        prev_render = rnow;
        if (rdt > 1.0) {
            float inst = (float)(1000000.0 / rdt);
            fps_ema += (inst - fps_ema) * 0.1f;
            game_set_fps(fps_ema);
        }

        game_draw();
    }

    snd_shutdown();
    gfx_shutdown();
    pak_close();
    sceKernelExitGame();
    return 0;
}

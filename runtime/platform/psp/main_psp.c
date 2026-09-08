/* main_psp.c — PSP entry: module info, exit callback, fixed 30 Hz sim loop.
 *   assets.pak is read from the EBOOT's directory on the Memory Stick. */
#include <pspkernel.h>
#include <pspdebug.h>
#include <psputils.h>
#include <stdio.h>

#include "../../src/fp.h"
#include "../../src/pak.h"
#include "../../src/audio.h"
#include "../../src/gfx.h"
#include "../../game/game.h"

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

    const double DT_US = 1000000.0 / (double)SPEC_ENGINE_FPS;
    double accum = 0;
    unsigned long long prev = sceKernelGetSystemTimeWide();

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

        game_draw();
    }

    snd_shutdown();
    gfx_shutdown();
    pak_close();
    sceKernelExitGame();
    return 0;
}

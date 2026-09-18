/* main_nfm_psp.c — NFM stage fly-around viewer for PSP / PPSSPP.
 *   assets.pak is read from the EBOOT's directory.
 *   Analog stick: move (Ly) / strafe (Lx).  D-pad L/R: turn.  D-pad U/D: pitch.
 *   Cross/Circle: camera down/up.  Square: fast.  L/R trigger: previous/next stage.
 *   Start: toggle fps overlay.  fps/poly stats are also appended to nfm_log.txt. */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspge.h>
#include <psppower.h>
#include <psputils.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../../src/pak.h"
#include "../../nfm/medium.h"
#include "../../nfm/stage.h"

PSP_MODULE_INFO("NFM Viewer", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

#define W 480
#define H 270
#define STRIDE 512
#define FBSZ (STRIDE * 272 * 4)

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

static uint32_t g_px[W * H];
static char g_names[64][96];
static int g_nstages;

/* g_px is already PSP ABGR8888 (built with -DNFM_ABGR): plain row copy into VRAM */
static void blit(uint32_t *vram)
{
    for (int y = 0; y < H; y++) memcpy(vram + y * STRIDE, g_px + y * W, W * 4);
}

int main(void)
{
    int th = sceKernelCreateThread("cb", cb_thread, 0x11, 0xFA0, 0, 0);
    if (th >= 0) sceKernelStartThread(th, 0, 0);

    scePowerSetClockFrequency(333, 333, 166);
    pspDebugScreenInit();
    if (!pak_open("assets.pak")) {
        pspDebugScreenPrintf("assets.pak not found next to EBOOT.PBP\n");
        sceKernelDelayThread(4 * 1000 * 1000);
        sceKernelExitGame();
        return 0;
    }
    for (int i = 0; i < pak_count() && g_nstages < 64; i++) {
        const char *id = pak_at(i)->id;
        if (strncmp(id, "data/stage/", 11) == 0) {
            size_t n = strlen(id);
            if (n > 5 && strcmp(id + n - 5, ".pstg") == 0) n -= 5;
            memcpy(g_names[g_nstages], id, n); g_names[g_nstages][n] = 0;
            g_nstages++;
        }
    }
    if (!g_nstages) {
        pspDebugScreenPrintf("no data/stage/* in assets.pak\n");
        sceKernelDelayThread(4 * 1000 * 1000);
        sceKernelExitGame();
        return 0;
    }

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    uint32_t *vram[2] = { (uint32_t *)(0x40000000 | (uintptr_t)sceGeEdramGetAddr()),
                          (uint32_t *)(0x40000000 | ((uintptr_t)sceGeEdramGetAddr() + FBSZ)) };
    int cur = 0, stage_i = 0, want = 0, overlay = 1;
    unsigned prevb = 0;
    FILE *log = fopen("nfm_log.txt", "w");

    static Stage st; static Scene sc; static Medium med;
    bool loaded = false;
    Frame f = { W, H, g_px };
    unsigned long long tlast = sceKernelGetSystemTimeWide();
    int frames = 0; float fps = 0; unsigned long long acc_draw = 0, acc_blit = 0;

    while (g_running) {
        if (!loaded || want) {
            if (loaded) { scene_free(&sc); stage_free(&st); }
            stage_i = (stage_i + want + g_nstages) % g_nstages; want = 0;
            char id[112];
            snprintf(id, sizeof id, "%s.pstg", g_names[stage_i]);
            const PakAsset *a = pak_find(g_names[stage_i]);
            if (!a) a = pak_find(id);
            loaded = a && stage_load(&st, a->data, a->size);
            if (!loaded) { want = 1; continue; }
            medium_init(&med, &f);
            stage_apply_env(&st, &med);
            scene_build(&sc, &st, &med);
            int camx = 0, camz = 0;
            if (sc.n) { camx = sc.inst[0].x; camz = sc.inst[0].z - 1200; }
            med.x = camx - med.cx; med.z = camz; med.y = -300; med.xz = 0; med.zy = 10;
            if (log) { fprintf(log, "stage %s '%s' pieces=%u skipped=%u\n", g_names[stage_i], st.name, (unsigned)sc.n, (unsigned)sc.skipped); fflush(log); }
        }

        SceCtrlData pad;
        sceCtrlReadBufferPositive(&pad, 1);
        unsigned b = pad.Buttons, edge = b & ~prevb; prevb = b;
        int ax = (int)pad.Lx - 128, ay = (int)pad.Ly - 128;
        if (ax > -30 && ax < 30) ax = 0;
        if (ay > -30 && ay < 30) ay = 0;
        if (edge & PSP_CTRL_LTRIGGER) want = -1;
        if (edge & PSP_CTRL_RTRIGGER) want = 1;
        if (edge & PSP_CTRL_START) overlay = !overlay;
        float sy = m_sin(med.xz), cy = m_cos(med.xz);
        int sp = (b & PSP_CTRL_SQUARE) ? 120 : 40;
        int fwd = -ay * sp / 128, side = ax * sp / 128;
        med.x += (int)(sy * fwd) + (int)(cy * side);
        med.z += (int)(cy * fwd) - (int)(sy * side);
        if (b & PSP_CTRL_LEFT)  med.xz = (med.xz + 359) % 360;
        if (b & PSP_CTRL_RIGHT) med.xz = (med.xz + 1) % 360;
        if (b & PSP_CTRL_UP)    med.zy += med.zy < 90;
        if (b & PSP_CTRL_DOWN)  med.zy -= med.zy > -90;
        if (b & PSP_CTRL_CROSS)  med.y += sp;
        if (b & PSP_CTRL_CIRCLE) med.y -= sp;

        g_polys_in = g_polys_drawn = 0;
        unsigned long long t_a = sceKernelGetSystemTimeWide();
        scene_draw(&med, &sc);
        unsigned long long t_b = sceKernelGetSystemTimeWide();

        uint32_t *fb = vram[cur];
        blit(fb);
        unsigned long long t_c = sceKernelGetSystemTimeWide();
        acc_draw += t_b - t_a; acc_blit += t_c - t_b;
        if (overlay) {
            pspDebugScreenSetOffset(cur * FBSZ);
            pspDebugScreenSetXY(0, 0);
            pspDebugScreenPrintf("%s  %.1f fps  %d/%d polys ", g_names[stage_i], fps, g_polys_drawn, g_polys_in);
        }
        sceDisplayWaitVblankStart();
        /* topaddr must be the real VRAM address: 0 means "disable display" */
        sceDisplaySetFrameBuf((void *)((uintptr_t)sceGeEdramGetAddr() + cur * FBSZ), STRIDE, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);
        cur ^= 1;

        frames++;
        unsigned long long now = sceKernelGetSystemTimeWide();
        if (now - tlast >= 1000000) {
            fps = frames * 1e6f / (float)(now - tlast);
            if (log) { fprintf(log, "%s %.1f fps %d polys | draw %.1f ms blit %.1f ms\n", g_names[stage_i], fps, g_polys_drawn, acc_draw / 1000.0 / frames, acc_blit / 1000.0 / frames); acc_draw = acc_blit = 0; fflush(log); }
            frames = 0; tlast = now;
        }
    }
    if (log) fclose(log);
    pak_close();
    sceKernelExitGame();
    return 0;
}

/* main_nfm_psp.c — NFM stage fly-around viewer for PSP / PPSSPP.
 *   assets.pak is read from the EBOOT's directory.
 *   Analog stick: move (Ly) / strafe (Lx).  D-pad L/R: turn.  D-pad U/D: pitch.
 *   Cross/Circle: camera down/up.  Square: fast.  L/R trigger: previous/next stage.
 *   Select: cycle draw distance 100/80/60/40/20%.  Start: toggle fps overlay.  Launch flags: R=333 MHz, Square=CPU blit, L=trace.  fps/poly stats are also appended to nfm_log.txt. */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspge.h>
#include <psppower.h>
#include <psputils.h>
#include <pspgu.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>

#include "../../src/pak.h"
#include "../../nfm/medium.h"
#include "../../nfm/stage.h"

PSP_MODULE_INFO("NFM Viewer", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

#define W 480
#define H 270
#define STRIDE 512
#ifndef NFM_LOWRES_DEFAULT
#define NFM_LOWRES_DEFAULT 0   /* -DNFM_LOWRES_DEFAULT=1: start at 400x225 (emulator testing without a held button) */
#endif
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

/* crash diagnostics: every step is appended and closed so the card keeps it even if we die */
static void step(const char *fmt, ...)
{
    FILE *f = fopen("nfm_boot.txt", "a");
    if (!f) return;
    va_list ap; va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f); fclose(f);
}
/* overwrite one fixed-size marker per call; after a crash it names the last thing started */
static FILE *g_trace_f;
static void trace_marker(const char *tag, int a, int b)
{
    fseek(g_trace_f, 0, SEEK_SET);
    fprintf(g_trace_f, "%-14s %6d %6d\n", tag, a, b);
    fflush(g_trace_f);
}

static uint32_t __attribute__((aligned(16))) g_px[W * H];
static unsigned int __attribute__((aligned(16))) g_gelist[4096];
static char g_names[64][96];
static int g_nstages;

/* g_px is already PSP ABGR8888.  The CPU only flushes its cache; the GE copies RAM -> VRAM
 * (a CPU memcpy into VRAM cost ~8 ms/frame on hardware). */
static void blit_cpu(uint32_t *vram_uncached)
{
    for (int y = 0; y < H; y++) memcpy(vram_uncached + y * STRIDE, g_px + y * W, W * 4);
}

static void blit_ge(uint32_t *vram_phys)
{
    sceKernelDcacheWritebackRange(g_px, sizeof g_px);
    sceGuStart(GU_DIRECT, g_gelist);
    sceGuCopyImage(GU_PSM_8888, 0, 0, W, H, W, g_px, 0, 0, STRIDE, vram_phys);
    sceGuTexSync();
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
}

/* Low-res mode: g_px holds rw x rh pixels (stride rw); the GE draws it as a bilinear-filtered
 * textured sprite stretched to the 480x270 picture (the game's 16:9). */
typedef struct { float u, v; float x, y, z; } SpriteVtx;

static void blit_ge_scaled(int rw, int rh, void *vram_off)
{
    sceKernelDcacheWritebackRange(g_px, (size_t)rw * rh * 4);
    sceGuStart(GU_DIRECT, g_gelist);
    sceGuDrawBufferList(GU_PSM_8888, vram_off, STRIDE);
    sceGuOffset(2048 - 240, 2048 - 136);
    sceGuViewport(2048, 2048, 480, 272);
    sceGuScissor(0, 0, 480, 272);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_BLEND);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, 512, 256, rw, g_px);
    sceGuTexFunc(GU_TFX_REPLACE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexFlush();
    SpriteVtx *v = (SpriteVtx *)sceGuGetMemory(2 * sizeof(SpriteVtx));
    v[0] = (SpriteVtx){ 0.0f, 0.0f, 0.0f, 0.0f, 0.0f };
    v[1] = (SpriteVtx){ (float)rw, (float)rh, 480.0f, 270.0f, 0.0f };
    sceGuDrawArray(GU_SPRITES, GU_TEXTURE_32BITF | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 2, NULL, v);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
}

#ifdef NFM_PROF
static unsigned long long prof_now(void) { return sceKernelGetSystemTimeWide(); }
#endif

/* NaN/inf must produce values (as in Java), not FPU traps: clear the FCSR exception-enable bits */
static void fpu_mask_exceptions(void)
{
    unsigned t;
    __asm__ volatile("cfc1 %0, $31" : "=r"(t));
    t &= ~0x0F80u;
    __asm__ volatile("ctc1 %0, $31" : : "r"(t));
}

int main(void)
{
    fpu_mask_exceptions();
#ifdef NFM_PROF
    g_prof_now = prof_now;
#endif
    int th = sceKernelCreateThread("cb", cb_thread, 0x11, 0xFA0, 0, 0);
    if (th >= 0) sceKernelStartThread(th, 0, 0);

    { FILE *z = fopen("nfm_boot.txt", "w"); if (z) fclose(z); }
    step("start, free mem %u, max block %u", (unsigned)sceKernelTotalFreeMemSize(), (unsigned)sceKernelMaxFreeMemSize());
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    SceCtrlData p0; sceCtrlReadBufferPositive(&p0, 1);
    int fast = (p0.Buttons & PSP_CTRL_RTRIGGER) != 0;
    int cpu_blit = (p0.Buttons & PSP_CTRL_SQUARE) != 0;   /* hold Square at launch: old CPU memcpy blit, for A/B timing */
    int trace_on = (p0.Buttons & PSP_CTRL_LTRIGGER) != 0;   /* hold L at launch: crash-bisect trace of frame 1 (slow) */
    if (fast) scePowerSetClockFrequency(333, 333, 166);   /* default stays at the system's 222 */
    step("clock (R held at launch = 333): cpu %d bus %d; blit=%s", scePowerGetCpuClockFrequency(), scePowerGetBusClockFrequency(), cpu_blit ? "cpu" : "ge");
    pspDebugScreenInit();
    pspDebugScreenPrintf("NFM viewer starting...\n");
    sceGuInit();
    if (!pak_open("assets.pak")) {
        pspDebugScreenPrintf("assets.pak not found next to EBOOT.PBP\n");
        sceKernelDelayThread(4 * 1000 * 1000);
        sceKernelExitGame();
        return 0;
    }
    step("pak opened: %d assets, free mem %u", pak_count(), (unsigned)sceKernelTotalFreeMemSize());
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

    step("stages listed: %d", g_nstages);
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    uint32_t *vram[2] = { (uint32_t *)(0x40000000 | (uintptr_t)sceGeEdramGetAddr()),
                          (uint32_t *)(0x40000000 | ((uintptr_t)sceGeEdramGetAddr() + FBSZ)) };
#ifdef NFM_FASTXF_DEFAULT
    bool fastxf = true;
#else
    bool fastxf = false;
#endif
    int cur = 0, stage_i = 0, want = 0, overlay = 1, far_pct = 100, lowres = 0;
#ifdef NFM_LOWRES
    lowres = NFM_LOWRES_DEFAULT || (p0.Buttons & PSP_CTRL_TRIANGLE) != 0;
#endif
    unsigned prevb = 0;
    FILE *log = fopen("nfm_log.txt", "w");

    static Stage st; static Scene sc; static Medium med;
    bool loaded = false;
    int first = 1;
    Frame f = { lowres ? 400 : W, lowres ? 225 : H, g_px };
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
            step("loading %s", g_names[stage_i]);
            loaded = a && stage_load(&st, a->data, a->size);
            step("stage_load -> %d", (int)loaded);
            if (!loaded) { want = 1; continue; }
            medium_init(&med, &f);
            med.fastxf = fastxf;
            stage_apply_env(&st, &med);
            med.far_pct = far_pct;
            step("medium ready, building scene");
            scene_build(&sc, &st, &med);
            step("scene built: %u pieces", (unsigned)sc.n);
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
        if (edge & PSP_CTRL_TRIANGLE) med.fastxf = fastxf = !fastxf;   /* float-composed transform on/off (A/B on device) */
#ifdef NFM_LOWRES   /* experimental, froze on real hardware: build with XCFLAGS=-DNFM_LOWRES to try */
        if (edge & PSP_CTRL_CIRCLE) {   /* 480x270 <-> 400x225 (0.5x the game's native 800x450) */
            lowres = !lowres;
            f.w = lowres ? 400 : W; f.h = lowres ? 225 : H;
            med.scale = (float)f.w / 800.0f;
        }
#endif
        if (edge & PSP_CTRL_SELECT) { far_pct = far_pct <= 30 ? 100 : far_pct - 20; med.far_pct = far_pct; }
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
        if (first && trace_on) { { FILE *z = fopen("nfm_trace.txt", "w"); if (z) { for (int k = 0; k < 64; k++) fputc(' ', z); fclose(z); } } g_trace_f = fopen("nfm_trace.txt", "r+"); if (g_trace_f) g_nfm_trace = trace_marker; step("first draw (tracing to nfm_trace.txt)"); }
        scene_draw(&med, &sc);
        if (first && trace_on) { g_nfm_trace = NULL; if (g_trace_f) { fclose(g_trace_f); g_trace_f = NULL; } step("first draw done"); }
        unsigned long long t_b = sceKernelGetSystemTimeWide();

        if (lowres) blit_ge_scaled(f.w, f.h, (void *)(uintptr_t)(cur * FBSZ));
        else if (cpu_blit) blit_cpu(vram[cur]);
        else blit_ge((uint32_t *)((uintptr_t)sceGeEdramGetAddr() + cur * FBSZ));
        unsigned long long t_c = sceKernelGetSystemTimeWide();
        acc_draw += t_b - t_a; acc_blit += t_c - t_b;
        if (overlay) {
            pspDebugScreenSetOffset(cur * FBSZ);
            pspDebugScreenSetXY(0, 0);
            pspDebugScreenPrintf("%s  %.1f fps  %d/%d polys  far %d%% %dx%d %s ", g_names[stage_i], fps, g_polys_drawn, g_polys_in, far_pct, f.w, f.h, med.fastxf ? "FAST" : "exact");
        }
        sceDisplayWaitVblankStart();
        if (first) { step("first blit, setting framebuf"); }
        /* topaddr must be the real VRAM address: 0 means "disable display" */
        sceDisplaySetFrameBuf((void *)((uintptr_t)sceGeEdramGetAddr() + cur * FBSZ), STRIDE, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);
        cur ^= 1;
        if (first) { step("first frame shown"); first = 0; }

        frames++;
        unsigned long long now = sceKernelGetSystemTimeWide();
        if (now - tlast >= 1000000) {
            fps = frames * 1e6f / (float)(now - tlast);
            if (log) { fprintf(log, "%s far %d %dx%d %s %s %.1f fps %d polys | draw %.1f ms blit %.1f ms\n", g_names[stage_i], far_pct, f.w, f.h, lowres ? "gescale" : cpu_blit ? "cpublit" : "geblit", med.fastxf ? "fast" : "exact", fps, g_polys_drawn, acc_draw / 1000.0 / frames, acc_blit / 1000.0 / frames); acc_draw = acc_blit = 0;
#ifdef NFM_PROF
                fprintf(log, "  prof/frame ms: sort %.1f  plane-total %.1f (shade %.1f fill %.1f => xform+cull %.1f)  [rot %.1f proj %.1f]\n", g_prof[PROF_SORT] / 1000.0 / frames, g_prof[PROF_PLANE] / 1000.0 / frames, g_prof[PROF_SHADE] / 1000.0 / frames, g_prof[PROF_FILL] / 1000.0 / frames, (g_prof[PROF_PLANE] - g_prof[PROF_SHADE] - g_prof[PROF_FILL]) / 1000.0 / frames, g_prof[PROF_ROT] / 1000.0 / frames, g_prof[PROF_PROJ] / 1000.0 / frames);
                memset(g_prof, 0, sizeof g_prof);
#endif
                fflush(log); }
            frames = 0; tlast = now;
        }
    }
    if (log) fclose(log);
    pak_close();
    sceKernelExitGame();
    return 0;
}

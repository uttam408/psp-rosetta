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

#ifdef NFM_GEFILL   /* polygon fill on the GE: see ge_fill.c; medium.c emits triangles instead of writing g_px */
void nfm_ge_begin(unsigned int *list, void *vram_off, int w, int h);
void nfm_ge_finish(void);
void nfm_ge_sync(void);
extern unsigned g_ge_dropped, g_ge_nv, g_ge_cmd;
static unsigned int __attribute__((aligned(16))) g_gelist_fill[32768];
#endif

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
    int cpu_blit = (p0.Buttons & PSP_CTRL_SQUARE) != 0;   /* hold Square at launch: GE builds fall back to the CPU rasteriser + GE blit; GEFILL=0 builds use the CPU memcpy blit */
#ifdef NFM_FORCECPU   /* PPSSPP test of the Square-at-launch fallback */
    cpu_blit = 1;
#endif
#ifdef NFM_GEFILL
    g_nfm_gefill = !cpu_blit;
#define MEMCPY_BLIT 0
#define GEFILL_ON g_nfm_gefill
#else
#define MEMCPY_BLIT cpu_blit
#define GEFILL_ON 0
#endif
    int trace_on = (p0.Buttons & PSP_CTRL_LTRIGGER) != 0;   /* hold L at launch: crash-bisect trace of frame 1 (slow) */
    if (fast) scePowerSetClockFrequency(333, 333, 166);   /* default stays at the system's 222 */
    step("clock (R held at launch = 333): cpu %d bus %d; fill=%s", scePowerGetCpuClockFrequency(), scePowerGetBusClockFrequency(),
#ifdef NFM_GEFILL
         cpu_blit ? "cpu" : "ge");
#else
         cpu_blit ? "cpu-memcpy" : "cpu+geblit");
#endif
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
#ifndef NFM_TINY_FAR
#define NFM_TINY_FAR 12   /* drop polys whose projected bbox is <= T native px in both axes; T ramps NFM_TINY_NEAR at the camera -> NFM_TINY_FAR at the draw-distance limit; 0 = off */
#endif
#ifndef NFM_TINY_NEAR
#define NFM_TINY_NEAR 3
#endif
#ifndef NFM_LOD
#define NFM_STEER_CAP 22   /* max wheel steer angle for the player's car (original: 36); analog stick scales 12..22 */
#define NFM_LOD 150  /* mesh LOD: skip polys whose estimated projected area is under NFM_LOD% of the tiny threshold's area; 0 = off */
#endif
#ifdef NFM_EXACT_DEFAULT
    bool fastxf = false;
#else
    bool fastxf = true;   /* ~10-15% cheaper per poly on hardware, 0.03% of pixels differ from the Java-exact path */
#endif
#ifndef NFM_START_STAGE
#define NFM_START_STAGE 0   /* -DNFM_START_STAGE=N: boot straight into stage N (0-based), for A/B profiling of a heavy stage */
#endif
    int cur = 0, stage_i = NFM_START_STAGE, want = 0, overlay = 1, far_pct = 60, lowres = 0, lod = NFM_LOD;
#ifdef NFM_LOWRES
    lowres = NFM_LOWRES_DEFAULT || (p0.Buttons & PSP_CTRL_TRIANGLE) != 0;
#endif
    unsigned prevb = p0.Buttons;   /* buttons held at launch must not count as presses */
    /* append, not truncate: a GE-fill run followed by a CPU-fill run must both survive for A/B comparison */
    FILE *log = fopen("nfm_log.txt", "a");
    if (log) fprintf(log, "==== boot: fill=%s cpu %d MHz ====\n", g_nfm_gefill ? "ge" : "cpu", (int)scePowerGetCpuClockFrequency());

    static Stage st; static Scene sc; static Medium med;
    static MadEnv menv; static Mad mad; static CarObj co;
    bool driving = false; Inst *dci = NULL;
#ifndef NFM_RACE_N
#define NFM_RACE_N 3   /* -DNFM_RACE_N=N: player + (N-1) AI opponents; 1 = no AI (original single-car behaviour) */
#endif
    static Mad ai_mad[NFM_MAXRACERS]; static CarObj ai_co[NFM_MAXRACERS]; static Control ai_ctl[NFM_MAXRACERS];
    int nai = 0;   /* set once the player car is placed, below */
    unsigned long long tphys = 0;
    bool loaded = false, reload = false;
    int car_i = 12;   /* NFM_CARS index; 12 = audir8 */
    int first = 1;
    Frame f = { lowres ? 400 : W, lowres ? 225 : H, g_px };
    unsigned long long tlast = sceKernelGetSystemTimeWide();
    int frames = 0; float fps = 0; unsigned long long acc_draw = 0, acc_blit = 0, acc_phys = 0, max_phys = 0, max_frame = 0, tprev = 0; unsigned nphys = 0;

    while (g_running) {
        if (!loaded || want || reload) {
            reload = false;
            if (loaded) {
                if (driving) { carobj_free(&co); for (int k = 0; k < nai; k++) carobj_free(&ai_co[k]); }
                driving = false; dci = NULL; nai = 0; scene_free(&sc); stage_free(&st);
            }
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
            med.tiny = NFM_TINY_NEAR; med.tinyfar = NFM_TINY_FAR; med.lod = lod;
            stage_apply_env(&st, &med);
            med.far_pct = far_pct;
            step("medium ready, building scene");
            sc.physics = true;
            scene_build(&sc, &st, &med);
            step("scene built: %u pieces", (unsigned)sc.n);
            char cid[64];
            snprintf(cid, sizeof cid, "mesh/car/%s", NFM_CARS[car_i].mesh);
            dci = scene_add_racer(&sc, &med, 0, cid, 0, -760, 0, 0xc83232, 0x282828);
            if (dci && carobj_init(&co, dci)) {
                memset(&menv, 0, sizeof menv);
                mad_init(&mad, &menv, 0);
                mad_reseto(&mad, car_i, &co, &sc.cp);
                driving = true; tphys = sceKernelGetSystemTimeWide();
                step("car %s ready: %d trackers, %d checkpoints", NFM_CARS[car_i].mesh, sc.trk.n, sc.cp.n);
                int want_ai = NFM_RACE_N - 1;
                if (want_ai > NFM_MAXRACERS - 1) want_ai = NFM_MAXRACERS - 1;
                for (int k = 0; k < want_ai; k++) {
                    int acn = (car_i + 1 + k) % 16;
                    char aid[64]; snprintf(aid, sizeof aid, "mesh/car/%s", NFM_CARS[acn].mesh);
                    int ox = (k % 2 ? 220 : -220) * (k / 2 + 1), oz = -760 - 180 * (k + 1);
                    Inst *ai = scene_add_racer(&sc, &med, k + 1, aid, ox, oz, 0, 0x3232c8, 0x282828);
                    if (!ai || !carobj_init(&ai_co[k], ai)) break;
                    menv.isbot[k + 1] = true;
                    mad_init(&ai_mad[k], &menv, k + 1);
                    mad_reseto(&ai_mad[k], acn, &ai_co[k], &sc.cp);
                    control_init(&ai_ctl[k], 0x9e3779b9u * (uint32_t)(k + 1) + (uint32_t)stage_i);
                    control_reset(&ai_ctl[k], &sc.cp, k + 1);
                    nai = k + 1;
                }
                step("%d AI opponent(s) ready", nai);
            }
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
#ifdef NFM_STAGECYCLE   /* -DNFM_STAGECYCLE: PPSSPP sweep, hold gas and step to the next stage every 120 frames */
        { static int nf; if (++nf % 120 == 0) want = 1; b |= PSP_CTRL_UP; }
#endif
#ifdef NFM_CARCYCLE   /* -DNFM_CARCYCLE: PPSSPP test, hold gas and step to the next car every 150 frames */
        { static int nf; if (++nf % 150 == 0) edge |= PSP_CTRL_SQUARE; b |= PSP_CTRL_UP; }
#endif
        if (edge & PSP_CTRL_SQUARE) { car_i = (car_i + 1) % 16; reload = true; }   /* next car (reloads the stage) */
        if (edge & PSP_CTRL_CIRCLE) { car_i = (car_i + 15) % 16; reload = true; }
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
        if (edge & PSP_CTRL_LTRIGGER) { lod = lod ? 0 : NFM_LOD; med.lod = lod; }   /* LOD on/off (A/B on device) */
        if (driving) {
            /* D-pad / analog: up = gas, down = brake, left/right = steer, Cross = handbrake; physics ticks at the original's 30 Hz */
            Control ctl = { .left = (b & PSP_CTRL_LEFT) || ax < -40, .right = (b & PSP_CTRL_RIGHT) || ax > 40,
                            .up = (b & PSP_CTRL_UP) || ay < -40, .down = (b & PSP_CTRL_DOWN) || ay > 40,
                            .handb = (b & PSP_CTRL_CROSS) != 0, .zyinv = false, .wall = -1 };
            {   /* d-pad steers to the full cap; the stick steers proportionally so a gentle push turns gently */
                int mag = ax < 0 ? -ax : ax;
                bool dpad = (b & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT)) != 0;
                mad.steer_cap = (dpad || mag < 40) ? NFM_STEER_CAP : 12 + (NFM_STEER_CAP - 12) * (mag - 40) / 87;
            }
            unsigned long long tn = sceKernelGetSystemTimeWide();
            for (int n = 0; tn - tphys >= 33333 && n < 3; n++, tphys += 33333) {
                unsigned long long tp = sceKernelGetSystemTimeWide();
                /* GameSparker.java order: colide all pairs -> AI preform -> drive all cars */
                for (int j = 0; j <= nai; j++)
                    for (int k = j + 1; k <= nai; k++) {
                        Mad *mj = j == 0 ? &mad : &ai_mad[j - 1]; CarObj *oj = j == 0 ? &co : &ai_co[j - 1];
                        Mad *mk = k == 0 ? &mad : &ai_mad[k - 1]; CarObj *ok = k == 0 ? &co : &ai_co[k - 1];
                        mad_colide(mj, oj, mk, ok);
                    }
                for (int k = 0; k < nai; k++) control_preform(&ai_ctl[k], &ai_mad[k], &ai_co[k], &sc.cp, &sc.trk);
                mad_drive(&mad, &ctl, &co, &sc.trk, &sc.cp);
                for (int k = 0; k < nai; k++) mad_drive(&ai_mad[k], &ai_ctl[k], &ai_co[k], &sc.trk, &sc.cp);
                if (nai) {
                    static Mad smad[NFM_MAXRACERS]; static CarObj sco[NFM_MAXRACERS];
                    smad[0] = mad; sco[0] = co;
                    for (int k = 0; k < nai; k++) { smad[k + 1] = ai_mad[k]; sco[k + 1] = ai_co[k]; }
                    checkpoints_checkstat(&sc.cp, smad, sco, nai + 1, 0);
                }
                unsigned long long dp = sceKernelGetSystemTimeWide() - tp;
                acc_phys += dp; nphys++; if (dp > max_phys) max_phys = dp;
            }
            if (tn - tphys >= 33333) tphys = tn;
            /* chase the original's lagged camera angle (Mad.cxz trails the travel direction), not the car's own
             * heading, so the car swings across the view through a turn.  cxz can leave 0..360 while unwinding. */
            int cyaw = ((mad.cxz % 360) + 360) % 360;
            med.x = dci->x + (int)(m_sin(cyaw) * 900) - med.cx; med.z = dci->z - (int)(m_cos(cyaw) * 900);
            med.y = dci->y - 490; med.xz = (360 - cyaw) % 360; med.zy = 10;
        } else {
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
        }

        g_polys_in = g_polys_drawn = 0;
        unsigned long long t_a = sceKernelGetSystemTimeWide();
        if (first && trace_on) { { FILE *z = fopen("nfm_trace.txt", "w"); if (z) { for (int k = 0; k < 64; k++) fputc(' ', z); fclose(z); } } g_trace_f = fopen("nfm_trace.txt", "r+"); if (g_trace_f) g_nfm_trace = trace_marker; step("first draw (tracing to nfm_trace.txt)"); }
#ifdef NFM_GEFILL
        if (g_nfm_gefill) nfm_ge_begin(g_gelist_fill, (void *)(uintptr_t)(cur * FBSZ), f.w, f.h);
#endif
        scene_draw(&med, &sc);
        if (first && trace_on) { g_nfm_trace = NULL; if (g_trace_f) { fclose(g_trace_f); g_trace_f = NULL; } step("first draw done"); }
        unsigned long long t_b;
#ifdef NFM_GEFILL
        if (g_nfm_gefill) {
            nfm_ge_finish();
            t_b = sceKernelGetSystemTimeWide();   /* CPU list building ends here; blit column = waiting for the GE */
            nfm_ge_sync();
        } else
#endif
        {
            t_b = sceKernelGetSystemTimeWide();

        if (lowres) blit_ge_scaled(f.w, f.h, (void *)(uintptr_t)(cur * FBSZ));
        else if (MEMCPY_BLIT) blit_cpu(vram[cur]);
        else blit_ge((uint32_t *)((uintptr_t)sceGeEdramGetAddr() + cur * FBSZ));
        }
        unsigned long long t_c = sceKernelGetSystemTimeWide();
#ifdef NFM_SHOT   /* -DNFM_SHOT: dump frame 60 (480x270 ABGR) to nfm_shot.raw for emulator checks */
        { static int nf; if (++nf == 60) {   /* GE copy VRAM -> RAM: PPSSPP keeps GPU-rendered frames out of guest VRAM until a transfer asks */
            sceGuStart(GU_DIRECT, g_gelist);
            sceGuCopyImage(GU_PSM_8888, 0, 0, W, H, STRIDE, (void *)((uintptr_t)sceGeEdramGetAddr() + cur * FBSZ), 0, 0, W, g_px);
            sceGuTexSync(); sceGuFinish(); sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
            sceKernelDcacheInvalidateRange(g_px, sizeof g_px);
            FILE *z = fopen("nfm_shot.raw", "wb"); if (z) { fwrite(g_px, 4, W * H, z); fclose(z); } } }
#endif
        acc_draw += t_b - t_a; acc_blit += t_c - t_b;
        if (overlay) {
            pspDebugScreenSetOffset(cur * FBSZ);
            pspDebugScreenSetXY(0, 0);
            pspDebugScreenPrintf("%s  %.1f fps  %d/%d polys  far %d%% lod %d %dx%d %s ", g_names[stage_i], fps, g_polys_drawn, g_polys_in, far_pct, lod, f.w, f.h, med.fastxf ? "FAST" : "exact");
            if (driving) {
                pspDebugScreenSetXY(0, 1);
                pspDebugScreenPrintf("%-14s speed %3d  cp %d  hit %d ", NFM_CARS[car_i].name, (int)mad.speed, mad.env->checkpoint, mad.hitmag);
                if (nai) { pspDebugScreenSetXY(0, 2); pspDebugScreenPrintf("pos %d/%d  lap %d/%d ", sc.cp.pos[0] + 1, nai + 1, sc.cp.pcleared / (sc.cp.n ? sc.cp.n : 1) + 1, sc.cp.nlaps); }
            }
        }
        sceDisplayWaitVblankStart();
        if (first) { step("first blit, setting framebuf"); }
        /* topaddr must be the real VRAM address: 0 means "disable display" */
        sceDisplaySetFrameBuf((void *)((uintptr_t)sceGeEdramGetAddr() + cur * FBSZ), STRIDE, PSP_DISPLAY_PIXEL_FORMAT_8888, PSP_DISPLAY_SETBUF_NEXTFRAME);
        cur ^= 1;
        if (first) { step("first frame shown"); first = 0; }

        frames++;
        { unsigned long long tf = sceKernelGetSystemTimeWide(); if (tprev && tf - tprev > max_frame) max_frame = tf - tprev; tprev = tf; }
        unsigned long long now = sceKernelGetSystemTimeWide();
        if (now - tlast >= 1000000) {
            fps = frames * 1e6f / (float)(now - tlast);
            if (log) { fprintf(log, "%s far %d lod %d %dx%d %s %s %.1f fps %d/%d polys | draw %.1f ms blit %.1f ms\n", g_names[stage_i], far_pct, lod, f.w, f.h, lowres ? "gescale" : MEMCPY_BLIT ? "cpublit" : GEFILL_ON ? "gefill" : "cpufill+geblit", med.fastxf ? "fast" : "exact", fps, g_polys_drawn, g_polys_in, acc_draw / 1000.0 / frames, acc_blit / 1000.0 / frames);
            if (log) fprintf(log, "  worst frame %.1f ms", max_frame / 1000.0);
            if (log && driving && nphys) fprintf(log, "  phys %.2f ms/tick (max %.2f, %u ticks)", acc_phys / 1000.0 / nphys, max_phys / 1000.0, nphys);
            if (log) fputc('\n', log);
            acc_draw = acc_blit = acc_phys = max_phys = max_frame = 0; nphys = 0;
#ifdef NFM_GEFILL
            if (log && g_nfm_gefill) fprintf(log, "  ge: %u verts, ~%u list words, %u dropped\n", g_ge_nv, g_ge_cmd, g_ge_dropped);
#endif
            if (log && driving) fprintf(log, "  drive: %s pos %d,%d,%d speed %.1f cp %d hit %d | car xz %d xy %d zy %d wxz %d | cam xz %d | mad mxz %d cxz %d fxz %d\n", NFM_CARS[car_i].mesh, dci->x, dci->y, dci->z, mad.speed, mad.env->checkpoint, mad.hitmag, dci->xz, dci->xy, dci->zy, dci->wxz, med.xz, mad.mxz, mad.cxz, mad.fxz);
#ifdef NFM_PROF
                fprintf(log, "  prof/frame ms: backdrop %.1f  sort %.1f  plane-total %.1f (shade %.1f fill %.1f => xform+cull %.1f)  [rot %.1f proj %.1f]\n", g_prof[PROF_BACK] / 1000.0 / frames, g_prof[PROF_SORT] / 1000.0 / frames, g_prof[PROF_PLANE] / 1000.0 / frames, g_prof[PROF_SHADE] / 1000.0 / frames, g_prof[PROF_FILL] / 1000.0 / frames, (g_prof[PROF_PLANE] - g_prof[PROF_SHADE] - g_prof[PROF_FILL]) / 1000.0 / frames, g_prof[PROF_ROT] / 1000.0 / frames, g_prof[PROF_PROJ] / 1000.0 / frames);
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

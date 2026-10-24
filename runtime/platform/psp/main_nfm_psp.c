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
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>

#include "../../src/pak.h"
#include "../../nfm/medium.h"
#include "../../nfm/stage.h"
#include "nfm_audio_psp.h"
#include "nfm_hud.h"
#include "nfm_callout.h"
#include <math.h>

/* stage N (1-based) -> its tracker module in the pak (the original's per-stage soundtrack) */
static const char *const g_stage_music[33] = { NULL,
    "stage1", "stage2", "stage3", "stage4", "stage5", "stage6", "stage7", "stage8", "stage9", "stage10",
    "ufo_tune_nfmmix", "ascender_-_overscan_nfmmix", "ability_nfmmix", "einstein_-_music_nfmmix", "warez_by_night_nfmmix",
    "gina_nfmmix", "uprising_nfmmix", "k7_game_nfmmix", "adrenochrome_nfmmix", "fleshbrain-sauron_nfmmix",
    "hiredguns_static-05_nfmmix", "winds_nfmmix", "popcorn-12_nfmmix", "ultrabass_nfmmix", "ektoplasmomatic_nfmmix",
    "shadowrun_nfmmix", "stage27", "the_logic", "jensz", "quake", "love", "roz_shuffle" };

PSP_MODULE_INFO("NFM Viewer", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

#define W 480
#ifndef NFM_TICK_US
#define NFM_TICK_US 47619   /* physics step: 33333 = the original's 30 Hz; 47619 = 0.7x game speed (default). Cars are pose-interpolated between steps. */
#endif
typedef struct { int x, y, z, xz, xy, zy; } Pose;
static void sfx(const char *name, int vol)
{
    char id[64]; snprintf(id, sizeof id, "audio/sfx/%s", name);
    const PakAsset *a = pak_find(id);
    if (a) nfm_sfx_play(a->data, a->size, vol);
}
static bool radar_on = true;   /* START toggles; the original starts with it off in single player */
static float g_ana;  /* the drawn guidance-arrow angle (xtGraphics.ana), also read by the Wrong Way call-out */
static Pose pose_of(const Inst *i) { return (Pose){ i->x, i->y, i->z, i->xz, i->xy, i->zy }; }
static void pose_set(Inst *i, Pose p) { i->x = p.x; i->y = p.y; i->z = p.z; i->xz = p.xz; i->xy = p.xy; i->zy = p.zy; }
static int lerp_i(int a, int b, float t) { return a + (int)((b - a) * t); }
static int lerp_ang(int a, int b, float t) { int d = (((b - a) % 360) + 540) % 360 - 180; return a + (int)(d * t); }
static Pose pose_lerp(Pose a, Pose b, float t)
{
    int dx = b.x - a.x, dz = b.z - a.z;
    if (dx > 4000 || dx < -4000 || dz > 4000 || dz < -4000) return b;   /* teleport (reset/fix point): don't smear */
    return (Pose){ lerp_i(a.x, b.x, t), lerp_i(a.y, b.y, t), lerp_i(a.z, b.z, t), lerp_ang(a.xz, b.xz, t), lerp_ang(a.xy, b.xy, t), lerp_ang(a.zy, b.zy, t) };
}
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
/* overwrite one fixed-size marker per call; after a crash it names roughly the last thing started.
 * NFM_TRACE fires per-polygon (thousands of calls for a full frame), and each fseek+fprintf+fflush
 * is a real, synchronous memstick write (tens of ms on hardware) -- calling this every time made
 * frame 1 look hung/crashed for minutes rather than actually faulting.  Only write every 32nd call
 * (so most polys are skipped) but always remember the most recent tag, so a real crash is still
 * findable to within ~32 calls without the write storm. */
static FILE *g_trace_f;
static void trace_marker(const char *tag, int a, int b)
{
    static unsigned n;
    unsigned i = n++;
    if (i >= 128 && i % 32 != 0) return;   /* full resolution for the first 128 calls, throttled after */
    fseek(g_trace_f, 0, SEEK_SET);
    fprintf(g_trace_f, "%-14s %6d %6d  (call #%u)\n", tag, a, b, i);
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
    int cpu_blit = 1;   /* the GE fill path looks visibly wrong on real hardware; always use the CPU rasteriser + GE blit until that's fixed. hold Square at launch to go back to GE fill. */
    if (p0.Buttons & PSP_CTRL_SQUARE) cpu_blit = 0;
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
    step("audio init: %d", nfm_audio_init());
    static HudTex h_dmg, h_pow, h_lap, h_pos, h_ord[3], h_cd[4], h_fin, h_wasted, h_cars, h_selcar, h_back, h_next, h_statb, h_statbo, h_select;
    {
        int ok = hud_load(&h_dmg, "damage") + hud_load(&h_pow, "power") + hud_load(&h_lap, "lap") + hud_load(&h_pos, "position")
               + hud_load(&h_ord[0], "1") + hud_load(&h_ord[1], "2") + hud_load(&h_ord[2], "3")
               + hud_load(&h_cd[0], "0c") + hud_load(&h_cd[1], "1c") + hud_load(&h_cd[2], "2c") + hud_load(&h_cd[3], "3c")
               + hud_load(&h_fin, "gamefinished") + hud_load(&h_wasted, "yourwasted");
        ok += hud_load(&h_cars, "cars") + hud_load(&h_selcar, "selectcar") + hud_load(&h_back, "back") + hud_load(&h_next, "next") + hud_load(&h_statb, "statb") + hud_load(&h_statbo, "statbo") + hud_load(&h_select, "select");
        step("hud textures: %d/20", ok);
    }
    int music_stage = -1;
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
#define NFM_RACE_N 6   /* -DNFM_RACE_N=N: player + (N-1) AI opponents; 1 = no AI (original single-car behaviour); 6 = max (NFM_MAXRACERS) */
#endif
    static Mad ai_mad[NFM_MAXRACERS]; static CarObj ai_co[NFM_MAXRACERS]; static Control ai_ctl[NFM_MAXRACERS];
    int nai = 0;   /* set once the player car is placed, below */
    unsigned long long tphys = 0;
    static Pose pose_prev[NFM_MAXRACERS], pose_real[NFM_MAXRACERS];
    bool pose_valid = false; int nlerp = 0; Inst *lin[NFM_MAXRACERS];
    bool loaded = false, reload = false;
    int car_i = 12;   /* NFM_CARS index; 12 = audir8 */
#ifdef NFM_PICKSKIP   /* emulator screenshots: go straight to the race */
    bool picking = false, spick = false; int pick_ang = 0;
#else
    bool picking = true, spick = true; int pick_ang = 0;   /* stage select, then the car picker, shown at launch */
#endif
    long sp_x = 0, sp_z = 0; int sp_fog = 0, sp_ang = 0, sp_ptr = 0, sp_cnt = 0;   /* stage-select orbit camera (Medium.aroundtrack) */
    int cyaw_hud = 0;
    unsigned long long wasted_t0 = 0;   /* when the player's car was destroyed (0 = intact) */
    unsigned long long cd_t0 = 0; int cd_step = -1;   /* 3-2-1-GO countdown: cars hold for 3 s, GO shows to 4 s */
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
#ifndef NFM_PICKSKIP
            if (want) picking = spick = true;   /* stage select, then the car picker, before each stage */
#endif
            stage_i = (stage_i + want + g_nstages) % g_nstages; want = 0;
            {   /* per-stage soundtrack; a car change reloads the same stage and keeps the track playing */
                int sn = atoi(g_names[stage_i] + 11);
                if (sn != music_stage) {
                    music_stage = sn;
                    char mid[96]; const PakAsset *ma = NULL;
                    if (sn >= 1 && sn <= 32) { snprintf(mid, sizeof mid, "data/music/%s", g_stage_music[sn]); ma = pak_find(mid); }
                    step("music stage %d: %s", sn, ma ? "play" : "none");
                    nfm_music_play(ma ? ma->data : NULL, ma ? ma->size : 0);
                }
            }
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
                driving = true; wasted_t0 = 0; callout_reset(); g_ana = 0; pose_valid = false; tphys = sceKernelGetSystemTimeWide(); cd_t0 = tphys; cd_step = -1;
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
            sp_fog = 0; sp_ptr = 0; sp_cnt = 0; sp_ang = 0; if (sc.cp.n) { sp_x = sc.cp.x[0]; sp_z = sc.cp.z[0]; }
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
        if (!picking && (edge & PSP_CTRL_SQUARE)) { car_i = (car_i + 1) % 16; reload = true; }   /* next car (reloads the stage) */
        if (!picking && (edge & PSP_CTRL_CIRCLE)) { car_i = (car_i + 15) % 16; reload = true; }
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
        if (driving && !picking && !wasted_t0 && (edge & PSP_CTRL_START)) radar_on = !radar_on;   /* the original's [S] radar toggle */
        if (driving && !picking && wasted_t0 && sceKernelGetSystemTimeWide() - wasted_t0 > 1500000 && (edge & (PSP_CTRL_CROSS | PSP_CTRL_START)))
            reload = true;   /* wasted: X/START retries the stage with a fresh, undamaged car */
        if (picking && driving && spick) {   /* stage select: the camera orbits the track; d-pad L/R browses stages, Cross/Start picks */
            if (edge & PSP_CTRL_RIGHT) want = 1;
            if (edge & PSP_CTRL_LEFT)  want = -1;
            med.trk = 1; med.tinyfar = 0; med.lod = 0;   /* the original's track view: no fog, no distance, tiny-polygon or LOD cull */
            if (edge & (PSP_CTRL_CROSS | PSP_CTRL_START)) { spick = false; med.trk = 0; med.tinyfar = NFM_TINY_FAR; med.lod = lod; }
            if (sc.cp.n) {   /* Medium.aroundtrack: 17000 out, 5000 up, centre easing along the checkpoints */
                sp_ang = (sp_ang + 1) % 360;
                sp_x -= (sp_x - sc.cp.x[sp_ptr]) / 10; sp_z -= (sp_z - sc.cp.z[sp_ptr]) / 10;
                if (sp_cnt == 7) { sp_ptr = (sp_ptr + 1) % sc.cp.n; sp_cnt = 0; } else sp_cnt++;
                float ra = sp_ang * 0.0174532925f;
                med.x = (int)(sp_x + 17000.0f * cosf(ra)) - med.cx; med.z = (int)(sp_z + 17000.0f * sinf(ra));
                med.y = -5000; med.xz = ((-sp_ang - 90) % 360 + 360) % 360; med.zy = 16;
            }
        } else if (picking && driving) {   /* car picker: the car turns on the grid; d-pad L/R browses, Cross/Start races */
            if (edge & PSP_CTRL_RIGHT) { car_i = (car_i + 1) % 16; reload = true; }
            if (edge & PSP_CTRL_LEFT)  { car_i = (car_i + 15) % 16; reload = true; }
            if (edge & (PSP_CTRL_CROSS | PSP_CTRL_START)) { picking = false; dci->xz = 0; dci->xy = 0; dci->zy = 0; mad_reseto(&mad, car_i, &co, &sc.cp); callout_reset(); tphys = sceKernelGetSystemTimeWide(); cd_t0 = tphys; cd_step = -1; pose_valid = false; }
            /* the original's carselect: the car turns on the spot 950 ahead of a level camera, over the showroom picture */
            pick_ang = (pick_ang + 4) % 360;
            dci->xz = pick_ang; dci->xy = 0; dci->zy = 0; dci->wzy -= 10; if (dci->wzy < -30) dci->wzy += 30;
            med.x = dci->x - med.cx; med.z = dci->z - 950;
            med.y = dci->y - 280; med.xz = 0; med.zy = 0;
        } else if (driving) {
            /* D-pad / analog: up = gas, down = brake, left/right = steer, Cross = handbrake; physics ticks at the original's 30 Hz */
            Control ctl = { .left = (b & PSP_CTRL_LEFT) || ax < -40, .right = (b & PSP_CTRL_RIGHT) || ax > 40,
                            .up = (b & PSP_CTRL_UP) || ay < -40, .down = (b & PSP_CTRL_DOWN) || ay > 40,
                            .handb = (b & PSP_CTRL_CROSS) != 0, .zyinv = false, .wall = -1 };
            /* the original only enters its air-control "loop" state on a handbrake press while airborne; arrows in flight start it too so they roll straight away */
            {   /* one aerial trick at a time: whichever axis is touched first (roll = left/right, flip = up/down) owns the jump */
                static int axis;
                bool air = !mad.wtouch && !mad.mtouch, any = ctl.left || ctl.right || ctl.up || ctl.down;
                if (!air || !any) axis = 0;
                else {
                    if (!axis) axis = (ctl.up || ctl.down) ? 2 : 1;
                    if (axis == 1) ctl.up = ctl.down = false; else ctl.left = ctl.right = false;
                    ctl.handb = true;
                }
            }
            {   /* d-pad steers to the full cap; the stick steers proportionally so a gentle push turns gently */
                int mag = ax < 0 ? -ax : ax;
                bool dpad = (b & (PSP_CTRL_LEFT | PSP_CTRL_RIGHT)) != 0;
                mad.steer_cap = (dpad || mag < 40) ? NFM_STEER_CAP : 12 + (NFM_STEER_CAP - 12) * (mag - 40) / 87;
            }
            unsigned long long tn = sceKernelGetSystemTimeWide();
            static bool tick1_traced = false;   /* bisect a hardware crash: mark each stage of the first physics tick only, so this doesn't spam the memstick every frame */
            lin[0] = co.in; for (int k = 0; k < nai; k++) lin[k + 1] = ai_co[k].in;
            int cd_s = (int)((tn - cd_t0) / 1000000ull);       /* 0..2 = 3,2,1 (cars held); 3 = GO; 4+ = racing */
            if (cd_s > 4) cd_s = 4;
            if (cd_s != cd_step) { cd_step = cd_s; if (cd_s < 4) { static const char *const cn_[4] = { "three", "two", "one", "go" }; sfx(cn_[cd_s], 200); } }
            if (cd_s < 3) tphys = tn;                          /* grid hold: no physics, nothing to catch up on at GO */
            for (int n = 0; tn - tphys >= NFM_TICK_US && n < 3; n++, tphys += NFM_TICK_US) {
                for (int k = 0; k <= nai; k++) pose_prev[k] = pose_of(lin[k]);
                pose_valid = true;
                bool trc = nai && !tick1_traced;
                unsigned long long tp = sceKernelGetSystemTimeWide();
                /* GameSparker.java order: colide all pairs -> AI preform -> drive all cars */
                if (trc) step("tick1: colide start (nai=%d)", nai);
                for (int j = 0; j <= nai; j++)
                    for (int k = j + 1; k <= nai; k++) {
                        Mad *mj = j == 0 ? &mad : &ai_mad[j - 1]; CarObj *oj = j == 0 ? &co : &ai_co[j - 1];
                        Mad *mk = k == 0 ? &mad : &ai_mad[k - 1]; CarObj *ok = k == 0 ? &co : &ai_co[k - 1];
                        /* mad_colide only pushes back when the FIRST car it's given dominates the second
                         * (its push logic gates on M->dominate[]); call it both ways per pair, or whichever
                         * car has the lower index never gets to react when the higher one actually wins. */
                        mad_colide(mj, oj, mk, ok);
                        mad_colide(mk, ok, mj, oj);
                    }
                if (trc) step("tick1: colide done, preform start");
                for (int k = 0; k < nai; k++) control_preform(&ai_ctl[k], &ai_mad[k], &ai_co[k], &sc.cp, &sc.trk);
                if (trc) step("tick1: preform done, drive start");
                mad_drive(&mad, &ctl, &co, &sc.trk, &sc.cp);
                for (int k = 0; k < nai; k++) mad_drive(&ai_mad[k], &ai_ctl[k], &ai_co[k], &sc.trk, &sc.cp);
                {   /* sound cues off the player's physics state: checkpoint passed, new impact (hitmag is cumulative) */
                    static int s_cp = -1, s_hit = 0; static unsigned s_rng = 1;
                    if (mad.hitmag < s_hit) {                               /* reset on reload / repair */
                        if (mad.hitmag == 0 && cd_s >= 4) sfx("carfixed", 200);   /* a repair hoop, not a stage load */
                        s_hit = 0;
                    }
                    if (mad.dest && !wasted_t0) { wasted_t0 = sceKernelGetSystemTimeWide(); sfx("wasted", 220); }
                    else if (!mad.dest && wasted_t0) wasted_t0 = 0;        /* repaired in time */
                    int cpn = mad.env->checkpoint;
                    if (s_cp >= 0 && cpn != s_cp) sfx("checkpoint", 200);
                    s_cp = cpn;
                    int dh = mad.hitmag - s_hit; s_hit = mad.hitmag;
                    if (dh > 40) {
                        s_rng = s_rng * 1664525u + 1013904223u;
                        char nm[16]; int pick = 1 + (int)((s_rng >> 16) % 3);
                        snprintf(nm, sizeof nm, "%s%d", dh < 300 ? "lowcrash" : "crash", pick);
                        sfx(nm, dh < 300 ? 150 : 230);
                    }
                }
                {   /* on-screen call-outs: stunt names, wrong way, checkpoint missed, power low, wasted opponents */
                    const char *ai_nm[8];
                    for (int k = 0; k < nai && k < 8; k++) ai_nm[k] = NFM_CARS[(car_i + 1 + k) % 16].name;
                    if (callout_tick(&mad, &sc.cp, nai, g_ana, cd_s >= 4, ai_nm)) sfx("powerup", 200);
                }
                if (trc) step("tick1: drive done, checkstat start");
                if (nai) {
                    static Mad smad[NFM_MAXRACERS]; static CarObj sco[NFM_MAXRACERS];
                    smad[0] = mad; sco[0] = co;
                    for (int k = 0; k < nai; k++) { smad[k + 1] = ai_mad[k]; sco[k + 1] = ai_co[k]; }
                    checkpoints_checkstat(&sc.cp, smad, sco, nai + 1, 0);
                }
                if (trc) { step("tick1: checkstat done"); tick1_traced = true; }
                unsigned long long dp = sceKernelGetSystemTimeWide() - tp;
                acc_phys += dp; nphys++; if (dp > max_phys) max_phys = dp;
            }
            if (tn - tphys >= NFM_TICK_US) tphys = tn;
            if (pose_valid) {
                float al = (float)(tn - tphys) / (float)NFM_TICK_US; if (al > 1.0f) al = 1.0f;
                nlerp = nai + 1;
                for (int k = 0; k < nlerp; k++) { pose_real[k] = pose_of(lin[k]); pose_set(lin[k], pose_lerp(pose_prev[k], pose_real[k], al)); }
            }
            /* chase the original's lagged camera angle (Mad.cxz trails the travel direction), not the car's own
             * heading, so the car swings across the view through a turn.  cxz can leave 0..360 while unwinding. */
            int cyaw = ((mad.cxz % 360) + 360) % 360; cyaw_hud = cyaw;
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
        if (first) step("before first scene_draw (nai=%d)", nai);
        /* crash-bisect: trace every scene_draw step of frame 1 to nfm_trace.txt (overwritten in place each
         * call, so whatever's left after a crash is the last thing reached).  Was gated behind L-held-at-launch
         * (trace_on); now unconditional on frame 1 so a hardware crash is diagnosable without extra user action. */
        if (first) { { FILE *z = fopen("nfm_trace.txt", "w"); if (z) { for (int k = 0; k < 64; k++) fputc(' ', z); fclose(z); } } g_trace_f = fopen("nfm_trace.txt", "r+"); if (g_trace_f) g_nfm_trace = trace_marker; step("first draw (tracing to nfm_trace.txt)"); }
#ifdef NFM_GEFILL
        if (g_nfm_gefill) nfm_ge_begin(g_gelist_fill, (void *)(uintptr_t)(cur * FBSZ), f.w, f.h);
#endif
        if (picking && driving && !g_nfm_gefill && !spick) {
            hud_begin(g_px, f.w, f.h);
            hud_clear(0x000000);
            hud_stretch(&h_cars, 0, 0, f.w, f.h, 1.0f);   /* fill the PSP screen: no letterbox bars */
            inst_draw(&med, dci);
            hud_sprite(&h_selcar, (f.w - h_selcar.w) / 2.0f, 20, 1.0f);
            (b & PSP_CTRL_LEFT ? hud_sprite_lit : hud_sprite)(&h_back, 57, 165, 1.0f);
            (b & PSP_CTRL_RIGHT ? hud_sprite_lit : hud_sprite)(&h_next, f.w - 57 - h_next.w, 165, 1.0f);
            {   /* stat bars: the formulas of xtGraphics.carselect; gradient up to the value, outline over it */
                const CarStats *cs = &NFM_CARS[car_i];
                float v[6];
                v[0] = (cs->swits[2] - 220) / 90.0f; if (v[0] < 0.2f) v[0] = 0.2f;
                v[1] = cs->acelf[1] * cs->acelf[0] * cs->acelf[2] * cs->grip / 7700.0f;
                v[2] = cs->dishandle;
                v[3] = (cs->airc * cs->airs * cs->bounce + 28.0f) / 139.0f;
                v[4] = (cs->moment + 0.5f) / 2.6f;
                v[5] = cs->outdam;
                for (int i = 0; i < 6; i++) {
                    if (v[i] > 1.0f) v[i] = 1.0f;
                    float bx = (i < 3 ? 162 : 536) * f.w / 800.0f, by = (337 + 15 * (i % 3)) * f.h / 450.0f, bw = 156 * f.w / 800.0f, bh = 7 * f.h / 450.0f;
                    hud_stretch(&h_statb, bx, by, bw, bh, v[i]);
                    hud_stretch(&h_statbo, bx, by, bw, bh, 1.0f);
                }
            }
        } else
        scene_draw(&med, &sc);
        if (picking && spick && driving && !g_nfm_gefill) {   /* stage-select overlay: title and back/next over the orbiting track */
            hud_begin(g_px, f.w, f.h);
            hud_sprite(&h_select, (f.w - h_select.w) / 2.0f, 35 * f.h / 450.0f, 1.0f);
            (b & PSP_CTRL_LEFT ? hud_sprite_lit : hud_sprite)(&h_back, 115 * f.w / 800.0f, 135 * f.h / 450.0f, 1.0f);
            (b & PSP_CTRL_RIGHT ? hud_sprite_lit : hud_sprite)(&h_next, f.w - 115 * f.w / 800.0f - h_next.w, 135 * f.h / 450.0f, 1.0f);
        }
        for (int k = 0; k < nlerp; k++) pose_set(lin[k], pose_real[k]);
        nlerp = 0;
        if (first) { g_nfm_trace = NULL; if (g_trace_f) { fclose(g_trace_f); g_trace_f = NULL; } step("first draw done"); }
        if (first) step("after first scene_draw");
        unsigned long long t_b;
        if (!g_nfm_gefill && driving && !picking) {
            hud_begin(g_px, f.w, f.h);
            {
                /* HUD, screen space 480x270.  Top-right: damage + power bars; top-centre: next-checkpoint arrow;
                 * bottom-left: position + lap; centre: countdown. */
                const float rx = 480.0f - 136.0f;
                float dm = mad.cd[mad.cn].maxmag > 0 ? (float)mad.hitmag / (float)mad.cd[mad.cn].maxmag : 0.0f;
                if (dm > 1.0f) dm = 1.0f;
                float pw = mad.power / 98.0f; if (pw < 0.0f) pw = 0.0f; if (pw > 1.0f) pw = 1.0f;
                hud_sprite(&h_dmg, rx, 6, 1.0f);
                hud_rect(rx + 46, 10, 70.0f * dm, 6, 0xE02020u);
                hud_sprite(&h_pow, rx, 22, 1.0f);
                hud_rect(rx + 46, 26, 70.0f * pw, 6, 0x20A0E0u);
                bool done = menv.lastcheck && mad.nlaps >= sc.cp.nlaps;
                if (!done) {
                    /* next target = the (clear - nlaps*nsp)-th checkpoint with typ > 0 (see the clear test in mad_drive) */
                    int want_k = mad.clear - mad.nlaps * sc.cp.nsp + 1, seen = 0, ti = -1;
                    for (int i = 0; i < sc.cp.n; i++) if (sc.cp.typ[i] > 0 && ++seen == want_k) { ti = i; break; }
                    if (ti >= 0) {
                        float c = (float)cyaw_hud * 0.01745329252f;
                        float dx = (float)(sc.cp.x[ti] - dci->x), dz = (float)(sc.cp.z[ti] - dci->z);
                        float fwd = dx * -sinf(c) + dz * cosf(c), side = dx * cosf(c) + dz * sinf(c);
                        /* xtGraphics.arrow: target angle clamped to +/-130, the drawn angle chases it at 10 deg per 30 Hz frame */
                        float ana = g_ana; static unsigned long long ta;
                        unsigned long long tq = sceKernelGetSystemTimeWide();
                        float stp = ta ? 10.0f * (float)(tq - ta) / 33333.0f : 10.0f; ta = tq;
                        float k = -atan2f(side, fwd) * 57.2957795f;
                        if (k > 130) k = 130;
                        if (k < -130) k = -130;
                        if (fabsf(ana - k) < 180) { if (fabsf(ana - k) < stp) ana = k; else ana += ana < k ? stp : -stp; }
                        else { ana += (k < 0 ? 1.5f : -1.5f) * stp; if (ana > 180) ana -= 360; if (ana < -180) ana += 360; }
                        g_ana = ana;
                        hud_arrow(ana, mad.missedcp, (frames & 4) != 0, g_px[3 * f.w + f.w / 2]);
                    }
                }
                if (radar_on && sc.cp.n > 1) {
                    int ox[8], oz[8];
                    for (int k = 0; k < nai && k < 8; k++) { ox[k] = ai_co[k].in->x; oz[k] = ai_co[k].in->z; }
                    hud_radar(sc.cp.x, sc.cp.z, sc.cp.typ, sc.cp.n, dci->x, dci->z, mad.cxz, ox, oz, nai < 8 ? nai : 8, med.csky, med.snap);
                }
                if (nai) {
                    int p = sc.cp.pos[0];
                    if (p >= 0 && p < 3) hud_sprite(&h_ord[p], 8, 236, 1.5f);
                    hud_sprite(&h_pos, 8, 224, 1.0f);
                    hud_sprite(&h_lap, 8, 252, 1.0f);
                }
                if (done) hud_sprite(&h_fin, 240 - h_fin.w, 100, 2.0f);
                if (wasted_t0) hud_sprite(&h_wasted, 240 - h_wasted.w, 90, 2.0f);
                int cs = (int)((sceKernelGetSystemTimeWide() - cd_t0) / 1000000ull);
                if (cs <= 3) { const HudTex *ct = &h_cd[cs == 3 ? 0 : 3 - cs]; hud_sprite(ct, 240.0f - ct->w, 100.0f, 2.0f); }
            }
        }
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
        if (picking && spick) {
            static int sflick;
            sflick ^= 1;
            pspDebugScreenSetOffset(cur * FBSZ);
            pspDebugScreenSetTextColor(sflick ? 0xFFF0F0F0 : 0xFFB0B0B0);
            pspDebugScreenSetXY((68 - (int)strlen(st.name)) / 2, 9);
            pspDebugScreenPrintf("%s", st.name);
            pspDebugScreenSetTextColor(0xFF909090);
            pspDebugScreenSetXY(30, 10);
            pspDebugScreenPrintf("%d / %d", stage_i + 1, g_nstages);
            pspDebugScreenSetTextColor(0xFFFFFFFF);
            pspDebugScreenSetXY(9, 31);
            pspDebugScreenPrintf("LEFT/RIGHT stage   X/START choose car   L/R stage");
        } else if (picking) {
            pspDebugScreenSetOffset(cur * FBSZ);
            {   /* car name (flickers grey/white like the original), stat labels, hint */
                static int flick;
                flick ^= 1;
                pspDebugScreenSetTextColor(flick ? 0xFFF0F0F0 : 0xFFB0B0B0);
                pspDebugScreenSetXY((68 - (int)strlen(NFM_CARS[car_i].name)) / 2, 7);
                pspDebugScreenPrintf("%s", NFM_CARS[car_i].name);
                pspDebugScreenSetTextColor(0xFF2878B5);
                static const char *const lab[6] = { "Top Speed:", "Acceleration:", "Handling:", "Stunts:", "Strength:", "Endurance:" };
                for (int i = 0; i < 6; i++) {
                    pspDebugScreenSetXY((i < 3 ? 20 : 39) - (int)strlen(lab[i]) - (i < 3 ? 8 : -1), 25 + (i % 3) * 1);
                    pspDebugScreenPrintf("%s", lab[i]);
                }
                pspDebugScreenSetTextColor(0xFF909090);
                pspDebugScreenSetXY(29, 8);
                pspDebugScreenPrintf("< %d / 16 >", car_i + 1);
                pspDebugScreenSetTextColor(0xFFFFFFFF);
                pspDebugScreenSetXY(13, 31);
                pspDebugScreenPrintf("LEFT/RIGHT car   X/START race   L/R stage");
            }
        } else if (overlay) {
            pspDebugScreenSetOffset(cur * FBSZ);
            pspDebugScreenSetXY(0, 0);
            pspDebugScreenPrintf("%s  %.1f fps  %d/%d polys  far %d%% lod %d %dx%d %s ", g_names[stage_i], fps, g_polys_drawn, g_polys_in, far_pct, lod, f.w, f.h, med.fastxf ? "FAST" : "exact");
            if (driving) {
                pspDebugScreenSetXY(0, 1);
                pspDebugScreenPrintf("%-14s speed %3d  cp %d  hit %d ", NFM_CARS[car_i].name, (int)mad.speed, mad.env->checkpoint, mad.hitmag);
                if (nai) {
                    int lap = mad.nlaps < sc.cp.nlaps ? mad.nlaps + 1 : sc.cp.nlaps;
                    pspDebugScreenSetXY(0, 2);
                    if (menv.lastcheck && mad.nlaps >= sc.cp.nlaps) pspDebugScreenPrintf("FINISHED  pos %d/%d ", sc.cp.pos[0] + 1, nai + 1);
                    else pspDebugScreenPrintf("pos %d/%d  lap %d/%d ", sc.cp.pos[0] + 1, nai + 1, lap, sc.cp.nlaps);
                }
            }
        }
        if (driving && !picking) {   /* HUD numbers: the sprites are labels only */
            pspDebugScreenSetOffset(cur * FBSZ);
            if (wasted_t0 && sceKernelGetSystemTimeWide() - wasted_t0 > 1500000) {
                pspDebugScreenSetTextColor(0xFFFFFFFF);
                pspDebugScreenSetXY(14, 22); pspDebugScreenPrintf("Press X to try again");
            }
            if (nai) {
                int p = sc.cp.pos[0];
                pspDebugScreenSetXY(9, 28); pspDebugScreenPrintf("%d/%d", p + 1, nai + 1);
                if (p >= 3) { pspDebugScreenSetXY(1, 30); pspDebugScreenPrintf("%dth", p + 1); }
                int lap = mad.nlaps < sc.cp.nlaps ? mad.nlaps + 1 : sc.cp.nlaps;
                pspDebugScreenSetXY(5, 31); pspDebugScreenPrintf("%d/%d", lap, sc.cp.nlaps);
            }
            if (!wasted_t0) {
                CalloutLine cl[6]; int ncl = callout_get(cl, 6);
                for (int i = 0; i < ncl; i++) {
                    int len = (int)strlen(cl[i].text); if (len > 68) len = 68;
                    pspDebugScreenSetTextColor(cl[i].col);
                    pspDebugScreenSetXY((68 - len) / 2, cl[i].row); pspDebugScreenPrintf("%.68s", cl[i].text);
                }
                pspDebugScreenSetTextColor(0xFFFFFFFF);
            }
            pspDebugScreenSetXY(58, 4); pspDebugScreenPrintf("%3d", (int)mad.speed);
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

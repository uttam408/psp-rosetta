/* main_nfm.c — NFM model viewer / headless harness (SDL, software rendered).
 *
 *   nfm_view <assets.pak> <mesh-id> [--shot out.bmp [yaw_deg]] [--bench] [--list]
 *
 *   mesh-id   pak id, e.g. mesh/car/mustang or mesh/piece/sroad
 *   --shot    render one 480x270 frame headless and save a BMP
 *   --bench   time 300 frames (backdrop + transform + sort + fill), print ms/frame
 *   --list    print every mesh id in the pak
 * Interactive: left/right rotate, up/down zoom, Esc quits.
 */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../src/pak.h"
#include "pmesh.h"
#include "medium.h"
#include "stage.h"

#define W 480
#define H 270

static bool save_bmp(const Frame *f, const char *path)
{
    SDL_Surface *s = SDL_CreateRGBSurfaceFrom(f->px, f->w, f->h, 32, f->w * 4,
                                              0xFF0000, 0x00FF00, 0x0000FF, 0);
    if (!s) return false;
    int rc = SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
    return rc == 0;
}

/* The world is fixed and the camera pivot is (cx,cy,cz); with xz=0 an object at
 * x = m.x + cx, z = m.z + dist sits dead ahead.  Ground is the y = 250 plane. */
static void render(Medium *m, Inst *o, int yaw, int dist)
{
    g_polys_in = g_polys_drawn = 0;
    m->x = 0; m->z = -dist; m->y = -(dist / 4);
    m->zy = 0;
    medium_draw_backdrop(m);
    o->x = m->x + m->cx; o->z = 0; o->y = 250;
    o->xz = yaw;
    inst_draw(m, o);
}

/* nfm_view <pak> data/stage/<n> [--shot out.bmp [x z yaw_deg [pitch_deg [height]]]] [--bench]
 * Camera defaults to just behind the first placed piece. */
static int stage_main(int argc, char **argv)
{
    char id[96];
    snprintf(id, sizeof id, "%s.pstg", argv[2]);
    const PakAsset *a = pak_find(argv[2]);
    if (!a) a = pak_find(id);
    Stage st;
    if (!a || !stage_load(&st, a->data, a->size)) { fprintf(stderr, "bad or missing stage: %s\n", argv[2]); return 1; }

    static uint32_t px[W * H], pxf[800 * 450];
    bool full = false;                 /* --full: native 800x450, for pixel diffs against the Java oracle */
    for (int i = 3; i < argc; i++) if (strcmp(argv[i], "--full") == 0) full = true;
    Frame f = full ? (Frame){ 800, 450, pxf } : (Frame){ W, H, px };
    Medium med;
    medium_init(&med, &f);
    stage_apply_env(&st, &med);
    Scene sc;
    memset(&sc, 0, sizeof sc);
    const char *drive_car = NULL; int drive_frames = 0;
    for (int i = 3; i + 2 < argc; i++)
        if (strcmp(argv[i], "--drive") == 0) { drive_car = argv[i + 1]; drive_frames = atoi(argv[i + 2]); sc.physics = true; }
    scene_build(&sc, &st, &med);
    printf("%s \"%s\": %u directives, %u pieces placed, %u skipped\n", argv[2], st.name, st.nobjs, sc.n, sc.skipped);

    int camx = 0, camz = 0, yaw = 0, pitch = 10, height = 300;
    if (sc.n) { camx = sc.inst[0].x; camz = sc.inst[0].z - 1200; }
    bool shot = false, bench = false, headless = false;
    const char *out = NULL;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            shot = true; out = argv[++i];
            int *v[] = { &camx, &camz, &yaw, &pitch, &height };
            for (int k = 0; k < 5 && i + 1 < argc && (isdigit((unsigned char)argv[i + 1][0]) || (argv[i + 1][0] == '-' && isdigit((unsigned char)argv[i + 1][1]))); k++)
                *v[k] = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--bench") == 0) bench = true;
        else if (strcmp(argv[i], "--headless") == 0) headless = true;   /* --drive/--race: print the trace and exit, skip the interactive SDL window */
    }
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--fast") == 0) med.fastxf = true;   /* float-composed transform */
        if (strcmp(argv[i], "--far") == 0 && i + 1 < argc) med.far_pct = atoi(argv[i + 1]);
        if (strcmp(argv[i], "--nochkalways") == 0) for (uint32_t k = 0; k < sc.n; k++) sc.inst[k].always = false;
        if (strcmp(argv[i], "--tiny") == 0 && i + 2 < argc) { med.tiny = atoi(argv[i + 1]); med.tinyfar = atoi(argv[i + 2]); }
        if (strcmp(argv[i], "--lod") == 0 && i + 1 < argc) med.lod = atoi(argv[i + 1]);
        if (strcmp(argv[i], "--loddiag") == 0 && i + 1 < argc) med.loddiag = (float)atof(argv[i + 1]);
    }
    for (int i = 3; i < argc; i++)
        if (strcmp(argv[i], "--car") == 0 && i + 1 < argc) {
            char id[96]; snprintf(id, sizeof id, "mesh/car/%s", argv[i + 1]);
            int cxp = 0, czp = 0;
            if (sc.n) { cxp = sc.inst[0].x; czp = sc.inst[0].z; }
            int spin = 0, steer = 0;
            for (int j = 3; j + 1 < argc; j++) {
                if (strcmp(argv[j], "--spin") == 0) spin = atoi(argv[j + 1]);
                if (strcmp(argv[j], "--steer") == 0) steer = atoi(argv[j + 1]);
                if (strcmp(argv[j], "--carpos") == 0 && j + 2 < argc) { cxp = atoi(argv[j + 1]); czp = atoi(argv[j + 2]); }
            }
            Inst *c = scene_add_car(&sc, &med, id, cxp, czp, 0, 0xc83232, 0x282828);
            if (c) { c->wzy = spin; c->wxz = steer; }
        }
    static MadEnv env; static Mad mad; static CarObj co;
    /* --race N: N-1 AI opponents alongside the driven car, full tick order per GameSparker.java:
     * colide (all pairs) -> preform (AI input) -> drive (all cars) -> checkstat (standings). */
    int nracers = 1;
    for (int i = 3; i + 1 < argc; i++) if (strcmp(argv[i], "--race") == 0) nracers = atoi(argv[i + 1]);
    if (nracers < 1) nracers = 1;
    if (nracers > NFM_MAXRACERS) nracers = NFM_MAXRACERS;
    static Mad ai_mad[NFM_MAXRACERS]; static CarObj ai_co[NFM_MAXRACERS]; static Control ai_ctl[NFM_MAXRACERS];
    int nai = 0;
    bool driving = false; Inst *dci = NULL;
    if (drive_car) {
        int cn = -1;
        for (int k = 0; k < 16; k++) if (strcmp(NFM_CARS[k].mesh, drive_car) == 0) cn = k;
        char id[96]; snprintf(id, sizeof id, "mesh/car/%s", drive_car);
        int sx0 = 0, sz0 = -760, sxz0 = 0;
        if (getenv("NFM_CARPOS")) sscanf(getenv("NFM_CARPOS"), "%d,%d,%d", &sx0, &sz0, &sxz0);   /* start x,z,heading */
        Inst *ci = cn < 0 ? NULL : scene_add_racer(&sc, &med, 0, id, sx0, sz0, sxz0, 0xc83232, 0x282828);
        if (!ci) { fprintf(stderr, "cannot drive car %s\n", drive_car); return 1; }
        Control ctl = { 0 };
        if (getenv("NFM_LISTTREES"))   /* solid-box (radx==radz) decor trackers: candidate obstacles to aim at */
            for (int k = 0, shown = 0; k < sc.trk.n && shown < 12; k++)
                if (sc.trk.decor[k] && sc.trk.radx[k] == sc.trk.radz[k]) { printf("tree tracker %d at %d,%d rad %d\n", k, sc.trk.x[k], sc.trk.z[k], sc.trk.radx[k]); shown++; }
        int steer_a = -1, steer_b = -1, trace_from = -1;
        if (getenv("NFM_STEER")) sscanf(getenv("NFM_STEER"), "%d,%d", &steer_a, &steer_b);
        int hb_a = -1, hb_b = -1, up_a = -1, up_b = -1;
        if (getenv("NFM_HANDB")) sscanf(getenv("NFM_HANDB"), "%d,%d", &hb_a, &hb_b);
        if (getenv("NFM_LIFT")) sscanf(getenv("NFM_LIFT"), "%d,%d", &up_a, &up_b);
        if (getenv("NFM_TRACE_FROM")) trace_from = atoi(getenv("NFM_TRACE_FROM"));   /* print every frame from here on */
        driving = true; dci = ci;
        env.im = 0; carobj_init(&co, ci); mad_init(&mad, &env, 0); mad_reseto(&mad, cn, &co, &sc.cp);
        if (getenv("NFM_STEER_CAP")) mad.steer_cap = atoi(getenv("NFM_STEER_CAP"));   /* default: the original's 36 */
        nai = nracers - 1;
        for (int k = 0; k < nai; k++) {
            int acn = (cn + 1 + k) % 16;   /* cycle to a different car model per opponent */
            char aid[96]; snprintf(aid, sizeof aid, "mesh/car/%s", NFM_CARS[acn].mesh);
            /* fan opponents out to the side of the player's start so they don't spawn stacked on top of it */
            int ox = sx0 + (int)(m_cos(sxz0) * 220 * (k + 1)) * ((k % 2) ? 1 : -1);
            int oz = sz0 - (int)(m_sin(sxz0) * 220 * (k + 1)) * ((k % 2) ? 1 : -1);
            Inst *ai = scene_add_racer(&sc, &med, k + 1, aid, ox, oz, sxz0, 0x3232c8, 0x282828);
            if (!ai) { nai = k; break; }
            env.isbot[k + 1] = true;
            carobj_init(&ai_co[k], ai);
            mad_init(&ai_mad[k], &env, k + 1);
            mad_reseto(&ai_mad[k], acn, &ai_co[k], &sc.cp);
            control_init(&ai_ctl[k], 0x9e3779b9u * (uint32_t)(k + 1));
            control_reset(&ai_ctl[k], &sc.cp, k + 1);
        }
        printf("trackers %d (%dx%d cells), checkpoints %d (nsp %d), laps %d, racers %d (%d AI)\n", sc.trk.n, sc.trk.ncx + 1, sc.trk.ncz + 1, sc.cp.n, sc.cp.nsp, sc.cp.nlaps, nai + 1, nai);
        for (int fr = 0; fr < drive_frames; fr++) {
            ctl.up = true; ctl.left = fr > 200 && fr < 230; ctl.wall = -1;
            if (steer_a >= 0) ctl.left = fr >= steer_a && fr < steer_b;   /* NFM_STEER=a,b: hold left over frames [a,b) */
            if (hb_a >= 0) ctl.handb = fr >= hb_a && fr < hb_b;            /* NFM_HANDB=a,b: hold handbrake (starts air control) */
            if (up_a >= 0) ctl.up = !(fr >= up_a && fr < up_b);            /* NFM_LIFT=a,b: release gas over [a,b) */
            /* GameSparker.java order: colide all pairs -> AI preform -> drive all cars -> checkstat */
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
            for (int k = 0; k < nai; k++) control_preform(&ai_ctl[k], &ai_mad[k], &ai_co[k], &sc.cp, &sc.trk);
            mad_drive(&mad, &ctl, &co, &sc.trk, &sc.cp);
            for (int k = 0; k < nai; k++) mad_drive(&ai_mad[k], &ai_ctl[k], &ai_co[k], &sc.trk, &sc.cp);
            {
                static Mad *stat_mads[NFM_MAXRACERS]; static CarObj *stat_objs[NFM_MAXRACERS];
                stat_mads[0] = &mad; stat_objs[0] = &co;
                for (int k = 0; k < nai; k++) { stat_mads[k + 1] = &ai_mad[k]; stat_objs[k + 1] = &ai_co[k]; }
                static Mad mflat[NFM_MAXRACERS]; static CarObj oflat[NFM_MAXRACERS];
                for (int k = 0; k <= nai; k++) { mflat[k] = *stat_mads[k]; oflat[k] = *stat_objs[k]; }
                checkpoints_checkstat(&sc.cp, mflat, oflat, nai + 1, 0);
            }
            if (fr % 20 == 0 || fr == drive_frames - 1 || (trace_from >= 0 && fr >= trace_from)) {
                printf("f%03d pos %6d %5d %6d xz %4d xy %4d zy %4d | mxz %4d cxz %4d | speed %7.2f clear %d hit %d | loop %d caps %d wtouch %d",
                       fr, ci->x, ci->y, ci->z, ci->xz, ci->xy, ci->zy, mad.mxz, mad.cxz, mad.speed, mad.clear, mad.hitmag,
                       mad.loop, mad.capsized, mad.wtouch);
                if (nai) printf(" | standing %d/%d", sc.cp.pos[0] + 1, nai + 1);
                printf(" | lap %d/%d%s\n", mad.nlaps < sc.cp.nlaps ? mad.nlaps + 1 : sc.cp.nlaps, sc.cp.nlaps,
                       (env.lastcheck && mad.nlaps >= sc.cp.nlaps) ? " FINISHED" : "");
            }
        }
        /* chase camera. The car travels along (-sin xz, cos xz) but the view looks along (sin yaw, cos yaw),
         * so the camera sits at +sin/-cos of the car and uses yaw = -xz to stay behind it.
         * The angle is Mad.cxz, the original's lagged camera angle; NFM_CAMYAW=xz locks rigidly to the car. */
        const char *cy = getenv("NFM_CAMYAW");
        int cyaw = (cy && strcmp(cy, "xz") == 0) ? ci->xz : mad.cxz;
        camx = ci->x + (int)(m_sin(cyaw) * 900); camz = ci->z - (int)(m_cos(cyaw) * 900); yaw = -cyaw;
    }
    med.x = camx - med.cx; med.z = camz; med.y = -height; med.xz = yaw; med.zy = pitch;

    if (shot || bench) {
        int frames = bench ? 300 : 3;      /* first frames seed Plane.av and ContO.dist */
        double fq = (double)SDL_GetPerformanceFrequency();
        Uint64 t0 = SDL_GetPerformanceCounter();
        for (int i = 0; i < frames; i++) { g_polys_in = g_polys_drawn = 0; scene_draw(&med, &sc); }
        Uint64 t1 = SDL_GetPerformanceCounter();
        if (bench) printf("bench: %.3f ms/frame, %d polys/frame\n", (t1 - t0) * 1000.0 / fq / frames, g_polys_drawn);
        if (shot) {
            bool ok = save_bmp(&f, out);
            printf("shot: %d/%d polys drawn -> %s (%s)\n", g_polys_drawn, g_polys_in, out, ok ? "ok" : "FAIL");
#ifdef NFM_STATS
            { extern int g_st[8]; printf("stats: behind %d offscreen %d passed-vis-tests %d rejected-after(thin/av/etc) %d thin-test-run %d\n", g_st[0], g_st[1], g_st[6], g_st[2], g_st[7]); }
#endif
            return ok ? 0 : 2;
        }
        return 0;
    }

    if (headless) return 0;   /* batch --drive/--race run: the frame-loop trace above is all the caller wants */

    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    SDL_Window *win = SDL_CreateWindow("nfm_view", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W * 2, H * 2, 0);
    SDL_Renderer *r = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture *tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
    for (bool run = true; run;) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
            if (ev.type == SDL_QUIT || (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)) run = false;
        const Uint8 *k = SDL_GetKeyboardState(NULL);
        if (driving) {      /* arrows drive, space = handbrake; physics ticks at the original's ~30 Hz, camera chases */
            static Uint32 last; Uint32 now = SDL_GetTicks();
            Control ctl = { k[SDL_SCANCODE_LEFT], k[SDL_SCANCODE_RIGHT], k[SDL_SCANCODE_UP], k[SDL_SCANCODE_DOWN], k[SDL_SCANCODE_SPACE], false, -1 };
            for (int n = 0; now - last >= 33 && n < 4; n++, last += 33) mad_drive(&mad, &ctl, &co, &sc.trk, &sc.cp);
            if (now - last >= 33) last = now;
            med.x = dci->x - (int)(m_sin(dci->xz) * 900) - med.cx; med.z = dci->z - (int)(m_cos(dci->xz) * 900);
            med.y = dci->y - 490; med.xz = dci->xz; med.zy = 10;
            scene_draw(&med, &sc);
            SDL_UpdateTexture(tex, NULL, px, W * 4); SDL_RenderCopy(r, tex, NULL, NULL); SDL_RenderPresent(r);
            continue;
        }
        float sy = m_sin(med.xz), cy = m_cos(med.xz);
        int sp = k[SDL_SCANCODE_LSHIFT] ? 120 : 40;
        if (k[SDL_SCANCODE_LEFT])  med.xz = (med.xz + 359) % 360;
        if (k[SDL_SCANCODE_RIGHT]) med.xz = (med.xz + 1) % 360;
        if (k[SDL_SCANCODE_W]) { med.x += (int)(sy * sp); med.z += (int)(cy * sp); }
        if (k[SDL_SCANCODE_S]) { med.x -= (int)(sy * sp); med.z -= (int)(cy * sp); }
        if (k[SDL_SCANCODE_A]) { med.x -= (int)(cy * sp); med.z += (int)(sy * sp); }
        if (k[SDL_SCANCODE_D]) { med.x += (int)(cy * sp); med.z -= (int)(sy * sp); }
        if (k[SDL_SCANCODE_UP])   med.zy += med.zy < 90;
        if (k[SDL_SCANCODE_DOWN]) med.zy -= med.zy > -90;
        if (k[SDL_SCANCODE_Q]) med.y -= sp;
        if (k[SDL_SCANCODE_E]) med.y += sp;
        scene_draw(&med, &sc);
        SDL_UpdateTexture(tex, NULL, px, W * 4);
        SDL_RenderCopy(r, tex, NULL, NULL);
        SDL_RenderPresent(r);
    }
    SDL_Quit();
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: nfm_view <assets.pak> <mesh-id|--list> [...]\n"); return 1; }
    if (!pak_open(argv[1])) { fprintf(stderr, "could not open pak: %s\n", argv[1]); return 1; }

    if (strcmp(argv[2], "--list") == 0) {
        for (int i = 0; i < pak_count(); i++)
            if (strncmp(pak_at(i)->id, "mesh/", 5) == 0) puts(pak_at(i)->id);
        return 0;
    }
    if (strncmp(argv[2], "data/stage/", 11) == 0) return stage_main(argc, argv);
    /* a .pmesh path on disk loads directly, so generated LOD variants can be viewed without repacking */
    const PakAsset *a = pak_find(argv[2]);
    PMesh mesh;
    uint8_t *fbuf = NULL;
    if (!a) {
        FILE *fp = fopen(argv[2], "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END); long n = ftell(fp); fseek(fp, 0, SEEK_SET);
            fbuf = malloc((size_t)n);
            if (fbuf && fread(fbuf, 1, (size_t)n, fp) == (size_t)n) {
                if (!pmesh_load(&mesh, fbuf, (size_t)n)) { fprintf(stderr, "bad .pmesh file: %s\n", argv[2]); return 1; }
            } else { free(fbuf); fbuf = NULL; }
            fclose(fp);
        }
    }
    if (!fbuf && (!a || !pmesh_load(&mesh, a->data, a->size))) {
        fprintf(stderr, "no such mesh (or bad .pmesh): %s\n", argv[2]);
        return 1;
    }
    printf("%s: %u verts, %u polys, %d wheels, %d tracks, max_r %u, disline %u\n", argv[2],
           mesh.nverts, mesh.npolys, mesh.nwheels, mesh.ntracks, mesh.max_r, mesh.disline);

    static uint32_t px[W * H];
    Frame f = { W, H, px };
    Medium med;
    medium_init(&med, &f);
    Inst inst;
    inst_init(&inst, &med, &mesh, 0, 0, 0, 0, -1, -1);
    int dist = (int)mesh.max_r * 2 + 150;
    for (int i = 3; i + 1 < argc; i++) if (strcmp(argv[i], "--dist") == 0) dist = atoi(argv[i + 1]);

    bool shot = false, bench = false;
    const char *out = NULL;
    int yaw = 35;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            shot = true; out = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') yaw = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--bench") == 0) bench = true;
    }

    if (shot) {
        render(&med, &inst, yaw, dist);   /* warm-up: Plane.av from this frame orders the next */
        render(&med, &inst, yaw, dist);
        bool ok = save_bmp(&f, out);
        printf("shot: %d/%d polys drawn -> %s (%s)\n", g_polys_drawn, g_polys_in, out, ok ? "ok" : "FAIL");
        return ok ? 0 : 2;
    }
    if (bench) {
        double fq = (double)SDL_GetPerformanceFrequency();
        Uint64 t0 = SDL_GetPerformanceCounter();
        for (int i = 0; i < 300; i++) render(&med, &inst, (yaw + i * 3) % 360, dist);
        Uint64 t1 = SDL_GetPerformanceCounter();
        printf("bench: %.3f ms/frame, %d polys/frame\n", (t1 - t0) * 1000.0 / fq / 300, g_polys_drawn);
        return 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    SDL_Window *win = SDL_CreateWindow("nfm_view", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       W * 2, H * 2, 0);
    SDL_Renderer *r = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture *tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
    bool run = true;
    while (run) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
            if (ev.type == SDL_QUIT || (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)) run = false;
        const Uint8 *k = SDL_GetKeyboardState(NULL);
        if (k[SDL_SCANCODE_LEFT])  yaw = (yaw + 359) % 360;
        if (k[SDL_SCANCODE_RIGHT]) yaw = (yaw + 1) % 360;
        if (k[SDL_SCANCODE_UP]   && dist > 200) dist -= dist / 50 + 1;
        if (k[SDL_SCANCODE_DOWN])  dist += dist / 50 + 1;
        render(&med, &inst, yaw, dist);
        SDL_UpdateTexture(tex, NULL, px, W * 4);
        SDL_RenderCopy(r, tex, NULL, NULL);
        SDL_RenderPresent(r);
    }
    SDL_Quit();
    return 0;
}

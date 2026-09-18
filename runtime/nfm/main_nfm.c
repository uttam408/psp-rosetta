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

    static uint32_t px[W * H];
    Frame f = { W, H, px };
    Medium med;
    medium_init(&med, &f);
    stage_apply_env(&st, &med);
    Scene sc;
    scene_build(&sc, &st, &med);
    printf("%s \"%s\": %u directives, %u pieces placed, %u skipped\n", argv[2], st.name, st.nobjs, sc.n, sc.skipped);

    int camx = 0, camz = 0, yaw = 0, pitch = 10, height = 300;
    if (sc.n) { camx = sc.inst[0].x; camz = sc.inst[0].z - 1200; }
    bool shot = false, bench = false;
    const char *out = NULL;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            shot = true; out = argv[++i];
            int *v[] = { &camx, &camz, &yaw, &pitch, &height };
            for (int k = 0; k < 5 && i + 1 < argc && argv[i + 1][0] != '-'; k++) *v[k] = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--bench") == 0) bench = true;
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
            return ok ? 0 : 2;
        }
        return 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;
    SDL_Window *win = SDL_CreateWindow("nfm_view", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W * 2, H * 2, 0);
    SDL_Renderer *r = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    SDL_Texture *tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, W, H);
    for (bool run = true; run;) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev))
            if (ev.type == SDL_QUIT || (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE)) run = false;
        const Uint8 *k = SDL_GetKeyboardState(NULL);
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
    const PakAsset *a = pak_find(argv[2]);
    PMesh mesh;
    if (!a || !pmesh_load(&mesh, a->data, a->size)) {
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

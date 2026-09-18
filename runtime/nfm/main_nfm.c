/* main_nfm.c — NFM model viewer / headless harness (SDL, software rendered).
 *
 *   nfm_view <assets.pak> <mesh-id> [--shot out.bmp [yaw_deg]] [--bench] [--list]
 *
 *   mesh-id   pak id, e.g. mesh/car/mustang or mesh/piece/sroad
 *   --shot    render one 480x272 frame headless and save a BMP
 *   --bench   time 300 frames (transform + sort + fill), print ms/frame
 *   --list    print every mesh id in the pak
 * Interactive: left/right rotate, up/down zoom, Esc quits.
 */
#include <SDL.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../src/pak.h"
#include "pmesh.h"
#include "render.h"

#define W 480
#define H 272

static bool save_bmp(const Frame *f, const char *path)
{
    SDL_Surface *s = SDL_CreateRGBSurfaceFrom(f->px, f->w, f->h, 32, f->w * 4,
                                              0xFF0000, 0x00FF00, 0x0000FF, 0);
    if (!s) return false;
    int rc = SDL_SaveBMP(s, path);
    SDL_FreeSurface(s);
    return rc == 0;
}

static void frame_for(Frame *f, Camera *cam, const PMesh *m, float yaw, float dist)
{
    /* aim at the model origin: camera height h = dist*tan(pitch) */
    cam->pitch = 0.22f;
    cam->x = 0; cam->y = -dist * tanf(cam->pitch); cam->z = -dist;
    cam->yaw = 0;
    frame_clear(f, 0x9FB8D8);
    draw_mesh(f, cam, m, 0, 0, 0, yaw, -1, -1);
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
    const PakAsset *a = pak_find(argv[2]);
    PMesh mesh;
    if (!a || !pmesh_load(&mesh, a->data, a->size)) {
        fprintf(stderr, "no such mesh (or bad .pmesh): %s\n", argv[2]);
        return 1;
    }
    printf("%s: %u verts, %u polys, %d wheels, %d tracks, max_r %u\n", argv[2],
           mesh.nverts, mesh.npolys, mesh.nwheels, mesh.ntracks, mesh.max_r);

    static uint32_t px[W * H];
    Frame f = { W, H, px, 0.0f };
    f.focal = 0.6f * 800.0f * 0.5f / tanf(0.5f);   /* placeholder FOV; real value comes from Medium (M3) */
    Camera cam;
    float dist = (float)mesh.max_r * 2.2f + 100.0f;

    bool shot = false, bench = false;
    const char *out = NULL;
    float yaw = 0.6f;
    for (int i = 3; i < argc; i++) {
        if (strcmp(argv[i], "--shot") == 0 && i + 1 < argc) {
            shot = true; out = argv[++i];
            if (i + 1 < argc && argv[i + 1][0] != '-') yaw = (float)atof(argv[++i]) * 3.14159265f / 180.0f;
        } else if (strcmp(argv[i], "--bench") == 0) bench = true;
    }

    if (shot) {
        frame_for(&f, &cam, &mesh, yaw, dist);
        bool ok = save_bmp(&f, out);
        printf("shot: %d/%d polys drawn -> %s (%s)\n", g_polys_drawn, g_polys_in, out, ok ? "ok" : "FAIL");
        return ok ? 0 : 2;
    }
    if (bench) {
        double fq = (double)SDL_GetPerformanceFrequency();
        Uint64 t0 = SDL_GetPerformanceCounter();
        for (int i = 0; i < 300; i++) frame_for(&f, &cam, &mesh, yaw + i * 0.05f, dist);
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
        if (k[SDL_SCANCODE_LEFT])  yaw -= 0.04f;
        if (k[SDL_SCANCODE_RIGHT]) yaw += 0.04f;
        if (k[SDL_SCANCODE_UP])    dist *= 0.98f;
        if (k[SDL_SCANCODE_DOWN])  dist *= 1.02f;
        frame_for(&f, &cam, &mesh, yaw, dist);
        SDL_UpdateTexture(tex, NULL, px, W * 4);
        SDL_RenderCopy(r, tex, NULL, NULL);
        SDL_RenderPresent(r);
    }
    SDL_Quit();
    return 0;
}

#include "../../runtime/nfm/medium.c"

static uint32_t px[800 * 450];

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "r");
    Frame fr = { 800, 450, px };
    int n, bad = 0, total = 0, shown = 0;
    while (fscanf(f, "%d", &n) == 1) {
        V2 v[16];
        for (int k = 0; k < n; k++) { int x, y; fscanf(f, "%d %d", &x, &y); v[k].x = (float)x; v[k].y = (float)y; }
        int cnt; long long h;
        fscanf(f, "%d %lld", &cnt, &h);
        memset(px, 0, sizeof px);
        fill_poly(&fr, v, n, 0xFFFFFF);
        long long hh = 0; int c = 0;
        for (int y = 0; y < 450; y++)
            for (int x = 0; x < 800; x++)
                if (px[y * 800 + x]) { c++; hh += (long long)(x + 1) * 7919LL + (long long)(y + 1) * 104729LL * 31LL; }
        total++;
        if (c != cnt || hh != h) {
            bad++;
            if (shown++ < 5) printf("MISMATCH n=%d java %d px, c %d px\n", n, cnt, c);
        }
    }
    printf("%d / %d polygons differ\n", bad, total);
    return 0;
}

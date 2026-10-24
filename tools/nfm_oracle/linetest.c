/* reads lines_java.txt, redraws each segment with medium.c's line(), compares pixel-set checksums */
#include "../../runtime/nfm/medium.c"

static uint32_t px[800 * 450];

int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "r");
    Frame fr = { 800, 450, px };
    int x0, y0, x1, y1, cnt, bad = 0, total = 0, shown = 0;
    long long h;
    while (fscanf(f, "%d %d %d %d %d %lld", &x0, &y0, &x1, &y1, &cnt, &h) == 6) {
        memset(px, 0, sizeof px);
        line(&fr, x0, y0, x1, y1, 0xFFFFFF);
        long long hh = 0; int c = 0;
        for (int y = 0; y < 450; y++)
            for (int x = 0; x < 800; x++)
                if (px[y * 800 + x]) { c++; hh += (long long)(x + 1) * 7919LL + (long long)(y + 1) * 104729LL * 31LL; }
        total++;
        if (c != cnt || hh != h) {
            bad++;
            if (shown++ < 6) printf("MISMATCH %d %d -> %d %d: java %d px, c %d px\n", x0, y0, x1, y1, cnt, c);
        }
    }
    printf("%d / %d segments differ\n", bad, total);
    return 0;
}

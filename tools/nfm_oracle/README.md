# NFM Java oracle

Renders a stage with the *original* Java renderer (headless, 800x450) so the C runtime can be
pixel-diffed against it.

Not included (decompiled game code): `Medium.java ContO.java Plane.java Trackers.java Wheels.java`.
Put the decompiled copies in `src/` next to these files. Procyon output needs patching:
- `x *= (int)0.991` -> `x = (int)(x * 0.991)` (sky), and `(int)1.6` in Plane gr==-10 -> `* 1.6`
- the top 80 rows rendering black is another decompile artifact in Medium; fix it the same way
`Madness`, `GameSparker`, `CheckPoints` here are stubs.

    javac -d classes *.java
    java -Djava.awt.headless=true -cp classes Driver stages/1.txt models.zip s1_java.png 0 -1200 0 10 300 3
    runtime/build/nfm_view build/nfm/assets.pak data/stage/1 --full --shot s1_c.bmp 0 -1200 0 10 300
    python3 tools/nfm_diff.py s1_java.png s1_c.bmp diff.png

Primitive checks (Java writes the expectation, C replays it):

    java -cp classes LineTest 6000 > lines_java.txt;  cc -ffp-contract=off -fwrapv -o linetest linetest.c -lm && ./linetest lines_java.txt
    java -cp classes PolyTest 3000 > poly_java.txt;   cc -ffp-contract=off -fwrapv -o polytest polytest.c -lm && ./polytest poly_java.txt

Status (stage 1, camera above): lines 0/6000 differ. Polygon fill: Java2D normalises coordinates
by +0.25 and fills left-inclusive/right-exclusive; C matches that, 998/3000 random (mostly
self-intersecting sliver) polygons still differ. Stage 1 diff: 0.84% of pixels, bottom third exact;
remainder is the checkpoint ring (gr==-10 flicker, unported) and the horizon line.

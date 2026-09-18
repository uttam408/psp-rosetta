# Need for Madness? — port plan

Second Rosetta target (after Luftrauser). This is the "getting started" doc: what the
source actually is, what the pipeline already handles, and the order to build the rest.
Read [`luftrauser-port-notes.md`](luftrauser-port-notes.md) first — its checklist is
applied below.

## 1. Where things stand

Pipeline half: **done for every asset class the game has.**

```sh
git clone https://github.com/catb0t/need-for-madness-source     # decompiled tree
make setup
.venv/bin/python -m pipeline.cli build --game nfm --src <tree> --keep-going
# -> build/nfm/assets.pak   374 assets, ~10 MB
```

| class | count | becomes | notes |
|---|---|---|---|
| models (`.rad`) | 84 (16 cars + 68 track pieces) | `.pmesh` (`pipeline/nfm/pmesh.py`) | 447 KiB total. All 84 parse; wheels + collision boxes included |
| stages | 32 | `.stage.json` | 6345 `set`, 197 `chk`, 49 `fix`, 3366 `pile`, 128 wall edges; every object id resolves to a model |
| images | 168 | `.ptx` RGBA5551, swizzled | all <=128px; HUD/menu GIFs (the 3D is untextured) |
| sfx | 56 | `.pcm` 22.05 kHz | |
| music | 34 | passthrough `.mod` | ProTracker modules, 4.7 MB. ffmpeg can't decode them (see §6) |

What is **not** done: any runtime code, the stage runtime format, music, the fidelity
harness. Those are §5.

`Kind.MESH` used to be a stub that copied bytes through with `parsed: false`; it now
produces real meshes. The Java adapter now unpacks the zips NFM actually ships (it used
to look for loose `cars/*` files that don't exist).

## 2. What the source is

68k lines of decompiled (Procyon) Java 7 applet code. It is a software 3D engine: every
frame the game transforms polygons on the CPU, sorts them back-to-front (painter's
algorithm), and calls `Graphics2D.fillPolygon` with a flat colour. There are **no
textures on 3D geometry**; shading is per polygon.

| class(es) | lines | role | disposition |
|---|---|---|---|
| `Globe`, `Lobby`, `Login`, `UDP*`, `udp*`, `update` | ~26k | online multiplayer, lobby, globe UI | **cut** (the build is single-player) |
| `xtGraphics` | 10.5k | HUD, menus, car select, all UI drawing | port a subset: in-race HUD, pause, car/stage select |
| `Mad` | 2.4k | car physics + damage | **port exactly** (the game's soul) |
| `Control` | 2.4k | input flags (`left/right/up/down/handb`) and `preform()`, the CPU-driver AI that fills those flags | port; player flags come from the PSP pad/nub, AI ports as-is |
| `ContO` | 2.3k | model instance: parse `.rad`, transform, draw | parse half is now in `pipeline/nfm/rad.py`; runtime half ports |
| `Medium` | 2.2k | camera, fog, sky/ground, world sort, `Plane` list | port |
| `GameSparker` | 3.7k | main loop, stage loading, model loading | port the stage loader; loop is trivial |
| `CarDefine` | 1.6k | per-car stat tables (accel, grip, damage...) | move to `spec/` as data |
| `Plane` | 1.3k | polygon: normal, lighting, fog, draw | port; this is what maps onto the GU |
| `Record` | 0.9k | replay recording | port; doubles as the determinism oracle |
| `CheckPoints`, `Trackers`, `Wheels` | 0.7k | checkpoints, collision boxes, wheel geometry | port |
| `StageMaker`, `CarMaker`, `Smenu`, `ibxm/`, `ds/` | ~15k | editors, tracker-music playback | cut editors; see §6 for music |

Realistic port surface: **~15-18k lines of Java**, about twice Luftrauser's AS3, with far
more numerical fidelity risk (integer/float mixed physics).

## 3. Formats (all reverse-engineered from the source; cited by method)

### `.rad` model (`ContO(byte[], ...)`, ContO.java:166-420)
Line-oriented text, parsed with `startsWith`. `<p>...</p>` polygons (`c(r,g,b)` or
`glass`/`gshadow`, `light`/`lightF`/`lightB`, `noOutline`, `gr()`, `fs()`, `p(x,y,z)`),
header directives (`div`, `idiv`, `iwid`, `ScaleX/Y/Z`, `1stColor`/`2ndColor`, `w()`,
`rims()`, `gwgr()`, `disp`, `disline`, flags), and `<track>` collision boxes (only read
after `disp(`).

`p()` math is `(int)(int(v) * div * iwid * scale)` in **Java float32**. The parser
reproduces that, so vertex integers match the original. `iwid` applies to x only.

Two subtleties:
- `1stColor`/`2ndColor` mark a car's repaintable colours; polys whose colour matches are
  tagged `paint = 1|2` (glass is never repainted).
- Wheels are not in the mesh. `w(...)` makes 19 procedural planes in `Wheels.make`; we
  record the parameters and the runtime builds them.

### Stage (`GameSparker.java:2400-2670`)
`set(id,x,z,rot)`, `chk(id,x,z,rot[,y])`, `fix(id,x,z,y,rot)`, `pile(...)`,
`maxr/maxl/maxt/maxb(n,pos,offset)` walls, plus sky/fog/ground/clouds/texture/etc.

**Id -> model:** the loader does `slot = id + 46`, models load at `56 + index` in the
`PIECES` list, so `id = index + 10` (`pipeline/nfm/tables.py`). `set(47,...)` is `sroad`
(the start piece), `chk(40,...)` is `checkpoint`.

`pile()` and `mountains()` are procedural and stay as raw arguments; the runtime
expands them. `mountains(n)` is the seed for `new java.util.Random(n)`
(Medium.java:1378), so the runtime needs an exact reimplementation of Java's 48-bit LCG
(same class of job as FlashPunk's LCG in Luftrauser). Note `Medium` seeds `mgen` from
`Math.random()` when a stage doesn't set one, so only stages with `mountains()` are
reproducible.

### `.pmesh`
Documented at the top of `pipeline/nfm/pmesh.py`. Polygons stay n-gons with flat
colour (NFM shades per polygon); wheels and collision boxes are fixed-size tables.

## 4. Rendering on PSP

The original's inner loop is the PSP's home turf. Plan:

- Keep NFM's CPU-side transform + painter's sort at first (bit-faithful draw order;
  matters because NFM depends on sort for its look, and the GU has 16-bit depth and no
  per-poly sort). Submit the sorted polygons as a **triangle-list of flat-coloured
  vertices** per frame, one `sceGuDrawArray` per batch. Lighting/fog are per-poly scalars
  computed on the CPU exactly as `Plane` does.
- Only after it's correct: try GU depth buffer + hardware transform for static track
  pieces (that would change draw order, so it needs a look-diff, not a bit-diff).
- Render target 480x270 inside 480x272 (`game.toml`: viewport 800x450, uniform 0.6x).
  Fixed-point/int coordinates on the CPU side are fine at this scale.
- Outlines: `Plane` draws polygon edges unless `noOutline`; GU line strips or a second
  pass. Cheap, but easy to forget.

Luftrauser lessons that apply directly: use `float` (`real.h`) everywhere and never call
double libm; normalized UVs (only for HUD sprites here); no repeat on swizzled textures;
layer with explicit clip, not draw order; **profile before optimizing**.

## 5. Build order

Same discipline as Luftrauser: get pixels on both PPSSPP and hardware before any
gameplay, and keep that commit as the fallback.

| # | Milestone | Done when |
|---|---|---|
| M0 | **Pipeline** (this PR) | `build --game nfm` yields a pak; tests green |
| M1 | Platform skeleton: reuse `runtime/src` + `platform/{sdl,psp}`; SDL headless screenshot and bench mode *first* | **SDL half done** (`runtime/nfm/`, `Makefile.nfm`: `--shot`, `--bench`, `--list`). PSP/PPSSPP half not started |
| M2 | Model loader: `.pmesh` -> draw a car and a road piece, orbit camera | **Done**: all 84 meshes load (`.pmesh` v2 carries `disline`, `disp`, `grounded`, stonecold/newstone). Wheels are procedural, so not drawn |
| M3 | `Medium` + `Plane`: camera, sort, per-poly lighting, fog, sky/ground; stage loader (`set`/`chk`/`fix`, then `pile`/mountains/walls) | **Core done, not compared to the original yet.** `runtime/nfm/medium.c` ports `Plane.d`, `ContO.d` and `Medium.d` (sky and ground bands) in the original 800x450 integer space, scaled by 0.6 only at fill time. `stage.c` loads `.pstg`, replays the environment directives and places `set`/`chk`/`fix` pieces, `pile` (procedural rock mounds, `pile.c`, seeded by a `java.util.Random` port verified against the JDK) and the `maxr/l/t/b` walls (`thewall` every 4800 units). `nfm_view <pak> data/stage/1 --shot out.bmp` renders the intro stage (125 pieces, 845 polys, 0.27 ms/frame on the host; stage 10: 390 pieces, 0.33 ms; no directive skipped). **Still missing:** mountains, clouds, stars, shadows, wheels, damage/chip effects, road markings for `flx` polys |
| M4 | `Mad` + `Wheels` + `Trackers`: physics and collision, car drives | drive stage 1 on the SDL build |
| M5 | `Control` + PSP input, checkpoints, laps, `Record` | complete a race; replay round-trips |
| M6 | **Fidelity harness** (replay diff vs original) — before anything cosmetic | see below |
| M7 | HUD, pause, car/stage select (the useful part of `xtGraphics`), music, sfx | |
| M8 | Perf on hardware; battery; stages 2-32 sweep | 30+ fps on a real PSP on the heaviest stage |

### Fidelity harness (M6)

NFM ships a deterministic replay system (`Record`), which is the natural oracle:
feed the same recorded inputs to (a) the original Java, headless, and (b) the port, and
diff car state (pos / vel / damage) per frame.

Constraint discovered while planning: the only JDK on the dev machine is Homebrew's
JDK 26 (`/opt/homebrew/opt/openjdk/bin/java`; the `java` on `PATH` is the macOS stub),
which **no longer has `java.applet`**, and the original extends `Applet`. Either
install a Java 8-era JDK (Temurin 8) or write a ~50-line harness that stubs
`java.applet.Applet` and drives `Mad`/`Control`/`Medium` directly with no AWT window.
The second is more useful long-term (headless CI). JDK 26 does run plain non-applet Java,
which is how the trig table below was checked.

### What M3 established

- **Trig is a table.** `Medium` builds `tsin`/`tcos` as `(float)Math.sin(i * 0.017453292519943295)`
  for integer degrees and everything indexes it. `spec/gen_nfm_trig.py` emits the same values
  as hex-float literals (`runtime/nfm/gen/ntrig_table.h`); all 360 entries were compared
  bit-for-bit against a Java 26 run. No libm trig is needed in game code.
- **Build flags** `-ffp-contract=off -fwrapv` (fused multiply-add and signed-overflow
  behaviour otherwise change results Java defines). Add the same to the PSP build.
- **`Plane.av` is last frame's key.** Polygons are painter-sorted by the `av` computed while
  drawing the *previous* frame (`ContO.d` ranks by it before `Plane.d` refreshes it), and
  objects likewise by `ContO.dist`. A one-frame screenshot therefore draws in index order;
  the viewer renders warm-up frames first.
- **Procyon artifact.** `n11 *= (int)0.991` in `Medium.d` is `(int)(n11 * 0.991)` in the
  original; read literally it paints the upper sky black. Expect more of these
  (`int op= double`) in `Mad`.
- **Deviation from the original:** none deliberate yet, but the fill is centre-sampled
  scanline, not Java2D's `fillPolygon` rule, so edge pixels can differ.
- **`pile` is `Plane` glass==3**: raw colour (no snap, +0.05 saturation), `gr=-8` (full brightness), drawn `noline`, ground y=250. Its collision trackers are not built yet (needed at M5).
- **Not ported (cosmetic, `Math.random`-driven):** damage bend/shatter, chips, dust, sparks.

### Luftrauser bugs to pre-empt (from `luftrauser-port-notes.md`)

| Luftrauser bug | NFM exposure | Do this |
|---|---|---|
| #1 double/libm on the PSP's single-precision FPU (10 fps, 25-40 us per trig call) | Heavier: `Mad` has ~102 trig/sqrt/abs calls, `Plane` 28, `ContO` 43, `Control` 31, and they all run every frame per polygon | `real.h` (float, `rsin`/`rcos`/...) from the first line of C. Precompute sin/cos tables for the integer-degree angles NFM uses. Add a bench mode in M1, not M8 |
| Fidelity: float vs double | Java `Math.sin/cos` are doubles and the results are truncated to `int`. A float port can land one unit off, and physics amplifies that | Diff against the Java oracle early (M6 can start as soon as M4 drives). Expect and log a tolerance, and compare a table-based sin/cos against Java's per-degree values before trusting either |
| Determinism (LCG rand) | `Math.random()` is used in `Medium` (42x), `Plane` (10x, damage/shatter) and `CheckPoints`, and none of it is seeded | Treat as cosmetic and keep it out of the state diff. Only `mountains()` seeds `java.util.Random`, and that is exact (see §3) |
| #2/#3 normalized UVs, no repeat on swizzled data | Mostly N/A (3D is untextured), applies to HUD sprites | Keep the rule for the HUD atlas |
| #4/#5 layering by GPU order, scissor exclusive corner | NFM's sort is CPU-side so order is explicit. Scissor matters for the 480x270 viewport | Explicit scissor; bottom-right is exclusive |
| #6 one big rendering rewrite that showed nothing | Same risk when moving from CPU sort to GU depth | Keep the known-good flat-triangle path as the fallback commit |
| #8 position from logical size, not POT padding | HUD/menu GIFs are converted to POT | Use the `.ptx` logical width/height |
| #10 frame pacing | NFM's loop sleeps to a fixed rate (`GameSparker` `Thread.sleep`) and physics is per frame | Fixed-step sim, render decoupled; don't "fix" frame-dependent physics |

## 6. Open decisions

1. **Transpile vs hand-port.** Recommendation: hand-port, like Luftrauser. The Procyon
   output is full of `n3`-style names and the game's numeric quirks are the thing to
   preserve, which needs a human reading each function. A transpiler would still need a
   `java.*` shim and produce code nobody can tune for the PSP. Revisit only if `Mad` +
   `Medium` + `Plane` + `ContO` prove too slow to port by hand.
2. **Music.** 34 ProTracker modules, 4.7 MB, all "M.K." 4-channel MOD files (the game
   also carries `ibxm` for XM). Options: (a) ship the `.mod`s and port a small MOD
   replayer to the PSP runtime (tiny on disk, exact); (b) render each to Ogg with a
   tracker player offline and reuse the existing Ogg path (bigger, simpler runtime).
   Recommendation: (a). ffmpeg cannot decode them, which is why they are passthrough.
3. **Stage runtime format.** Decided and done: binary `.pstg` (`pipeline/nfm/pstg.py`,
   loader in `runtime/nfm/stage.c`). The directives are stored in the same integers
   `GameSparker` feeds `Medium`, so the runtime replays them through the same setters.
   `.stage.json` stays alongside for inspection only. Note the file has no directive
   order, so the loader applies them in the canonical stage-file order (snap, sky, fog,
   ground, texture, polys, fadefrom, density).
4. **`spec/`.** Follow Luftrauser's model: `CarDefine` and the `Mad` constants go in
   `spec/nfm.toml`, each citing its source line, generated into a header.

## 7. Legal

Same as the rest of the repo: personal use, on hardware you own. The decompiled tree's
README says the code belongs to Omar Waly and is posted for research and noncommercial
use; it also contains the game's models, sounds and music. **Nothing from that tree is
committed here** — tests use synthetic data, and `build/` is gitignored.

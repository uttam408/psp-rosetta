# Luftrauser port — what we built, what bit us

Game-specific retrospective for the first Rosetta target (Luftrauser, Flash/AS3 to CFW PSP).
Written as a starting point for the next game; the generic pipeline docs live in
[`PROJECT.md`](../PROJECT.md) and are untouched by this file.

## Where it ended up

- Faithful one-to-one port running on PPSSPP and real PSP hardware at a **60 fps** cap.
  Player, weapon, all four enemy types, sea units, FX, clouds, audio, HUD, hiscore,
  game-over, boot splash.
- Fixed 30 Hz simulation, render decoupled. Simulation is deterministic (FlashPunk LCG).
- Engine layers: `runtime/src` (FP core, entity, world, pak/ptx, audio mix) is platform-free;
  `runtime/platform/{sdl,psp}` supply gfx/input/audio; `runtime/game` is the ported game.
- Dev loop: SDL desktop build with headless screenshots, then PPSSPP, then hardware.

## Bugs and lessons (in the order they hurt)

### 1. Profile before you optimize (the big one)
Symptom: 10 fps at 100 entities, 5 fps at 200. We spent several passes on rendering
(CPU-rotated quads, batching, frustum culling). None moved the real-hardware numbers.

Cause: **the PSP has a single-precision FPU only.** Every `double` op and every libm call
(`sin`, `atan2`, `sqrt`) is emulated in software: ~1-2 us per op, 25-40 us per trig call.
Game logic written with AS3 `Number` (double) semantics was the bottleneck, not the GPU.

Fix: one `real` typedef (`runtime/src/real.h`): `float` on PSP, `double` on SDL, with
`rsin/rcos/ratan2/rsqrt/...` macros, plus `-fsingle-precision-constant`. Early-out in
`ent_update` when gravity and friction are both 0. Result: 60 brits 24.2 ms to 1.1 ms per
tick; 30 jets 8.5 ms to 1.4 ms.

Takeaways for the next game:
- **Use `real`/float from the first line of C.** Retrofitting it touched every file.
- PPSSPP JITs the same soft-double routines, so it *is* a faithful proxy for this class
  of bottleneck. It is not faithful for GPU fill/bandwidth.
- Build a bench mode early. Ours (`bench.on` file next to the EBOOT, writes `bench.txt`)
  times libm calls and per-entity-type ticks. PPSSPP cannot be screenshotted from a
  headless session but does write files, so file output is the reliable channel.
- When a render-side guess doesn't move the number, stop guessing and measure.

### 2. GU texture coordinates are normalized, not texels
`GU_TEXTURE_32BITF` UVs are in [0,1]. Passing texel units with `GU_REPEAT` tiled each
sprite ~16x and produced noise on the first PPSSPP run. Normalize by texture size and
use `GU_CLAMP`.

### 3. Swizzled textures + `GU_REPEAT` = corruption
Tiling the water/space bands as one repeated quad over a swizzled texture rendered
garbage (swizzle block layout breaks simple modulo addressing). We chased a "sprites
render in front of water" bug that was really this. Draw tiles individually with clamp
sampling, and do not rely on repeat with swizzled data.

### 4. Don't trust GPU draw order for layering
"Behind the water" originally depended on draw order and depth behaviour. Now the world
render clips everything above the water layer to rows above the waterline (scissor on
PSP, clip rect on SDL). Deterministic and backend-independent.

### 5. `sceGuScissor` args
The last two arguments are the **exclusive** bottom-right corner, not the inclusive one.

### 6. Batched 2D rewrite that "showed nothing on device"
An early `GU_TRANSFORM_2D` batching pass drew nothing. We reverted to the known-good
3D-transform path, then re-introduced batching later in small verified steps
(`GU_TRIANGLES`, vertex data from `sceGuGetMemory`, identity model matrix, quads placed
and rotated on the CPU, flush on texture change / clip change / frame end / overflow).
Lesson: change one rendering assumption at a time and keep a known-good commit to fall
back to.

### 7. GUM rotation sign
Rotation direction differs from the FlashPunk convention (`RAD = -PI/180`, degrees,
clockwise, Y-down). We self-calibrate the sign once at startup instead of hard-coding it.

### 8. POT padding is not the sprite size
`uboot`/`sea` used the power-of-two padded texture dimensions instead of the real
frame size, so the sub launched ~12 px off its tower and the battleship floated above the
waterline. Always position from the logical frame size (`frames[0]`), never from the
uploaded texture size. We had briefly "fixed" it with a bogus +4 px offset; that was
reverted.

### 9. 5551 pixel layout for runtime-built textures
`gfx_tex_from_pixels` expects R in bits 0-4, G 5-9, B 10-14, A bit 15. Easy to get
backwards when generating the font atlas or the logo header.

### 10. Frame pacing
Cap via a vblank accumulator (`TARGET_FPS` 60 gives one `sceDisplayWaitVblankStart` per
frame). 45 fps was chosen first to save battery; 60 is fine once the CPU budget is real.

## Fidelity gotchas worth carrying forward
- FlashPunk LCG RNG: `seed*16807 % 2^31`. `fp_rand(amount)` must be an exact integer
  computation, not float, or spawn sequences drift. `fp_random()` can round up to 1.0 in
  float; clamp it.
- The coupled `speed/direction/hspeed/vspeed` accessors are the flight feel. `set speed`
  re-projects onto the current direction.
- Reproduce frame-rate-dependent physics as-is at fixed 30 Hz; do not "fix" it.

## Tooling that paid off
- **SDL headless screenshots**:
  `SDL_RENDER_DRIVER=software ./build/luftrauser assets.pak --shot <ticks> out.bmp [script | '!SEA' | '!JET' | '!BENCH' | '!SPLASH']`
  Lets an agent verify visuals without a window or a PSP.
- A `-MMD -MP` header-dependency build on the PSP Makefile (editing a header rebuilt
  nothing before that).
- Pools (`POOL(...)`) for entities, a per-layer render list, a bench mode, on-screen
  FPS/entity counter.
- Boot splash generated from the project's own art into a committed header
  (`tools/make_logo_header.py`), so it needs no asset pak.
- Game art and audio are **not** committed; they come from the user's own copy via the
  pipeline. Only project-owned art (logo, icon generator) is in the repo.

## Deploy notes
- Copy `EBOOT.PBP` + `assets.pak` to `PSP/GAME/<NAME>/` on the memory stick, `cmp` both
  files, `sync`, then `diskutil eject`. Leave `hiscore.dat` alone.
- A freshly mounted card can show a stale directory listing; after an eject the stick may
  stay unmounted until re-plugged.

## Suggested checklist for the next Rosetta
1. Use `real`/float everywhere from day one; never call `double` libm on PSP.
2. Stand up SDL + headless screenshot + bench mode *before* writing game logic.
3. Get "black screen to textured quad" working on both PPSSPP and hardware before any
   perf work; keep that commit as the fallback.
4. Normalized UVs, clamp sampling, no repeat on swizzled data.
5. Position from logical frame sizes, never padded texture sizes.
6. Layering via explicit clip/scissor, not GPU order.
7. Measure, then optimize. Render-side guesses were the wrong lever here.

## Known TODOs left in this port
Firing during `justSpawned`, Boot's exact fire pattern, Brit no-player AI retune,
per-type debris caps, real "+N" score text, full game-over cinematic, fidelity harness,
`.vag` audio, and the pandoras-box backlog in [`roadmap.md`](roadmap.md).

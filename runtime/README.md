# runtime — Luftrauser, one-to-one

C reimplementation of *Luftrauser* built against the FlashPunk-shaped core in
`src/`. Constants come from `gen/spec_luftrauser.h`, generated from
`../spec/luftrauser.toml` (which cites the decompiled AS3 line-by-line).

## Build & run (desktop / SDL2)

```sh
brew install sdl2            # or sdl2-compat
make run                     # builds + runs against ../build/luftrauser/assets.pak
```

## Build for PSP (EBOOT.PBP)

```sh
export PSPDEV=~/pspdev PATH=~/pspdev/bin:$PATH
make -f Makefile.psp         # -> runtime/EBOOT.PBP  (build-verified; not yet run)
```

Deploy: put `EBOOT.PBP` + a copy of `../build/luftrauser/assets.pak` together in
`ms0:/PSP/GAME/LUFTRAUSER/` on a CFW PSP (ARK-4 / PRO), or open the EBOOT in
PPSSPP with `assets.pak` beside it.

The PSP backend (`platform/psp/`) uses the pipeline's `.ptx` blobs natively —
RGBA5551, power-of-two, unswizzled — as `GU_PSM_5551` textures with no conversion.
Sprites draw as rotated quads through the GUM matrix stack under an ortho
projection. Input is sceCtrl (analog nub + D-pad + face buttons); audio is still
the no-op backend.

Controls: Arrows / WASD steer + thrust, X / Space fire, Enter start, Esc quit.

Headless capture (used for tests / CI):

```sh
./build/luftrauser ../build/luftrauser/assets.pak --shot <ticks> out.bmp "<script>"
# script: one char per fixed frame, last char repeats — T thrust, L left, R right,
#         F fire, S start, . none.   Prefix "@" to read the script from a file.
```

## Layout

```
src/          FlashPunk-shaped core, platform-agnostic
  fp.*        RAD/DEG, LCG rand (seed*16807 % 2^31), angle/angleXY/approach, camera
  entity.*    Entity motion model — the hspeed/vspeed/speed/direction coupling
  world.*     update list / layer render list / per-type iteration
  pak.*       assets.pak (PAK1) + its "@index" AIDX binary asset table
  ptx.*       .ptx texture decode (RGBA5551/4444/8888/IDX8 -> RGBA32)
  gfx.h       backend interface (draw takes WORLD coords, camera applied here)
  input.*     logical actions (Thrust/Left/Right/Fire/...)
  audio.h     SFX + music interface  (audio_null.c = working no-op)
platform/
  sdl/        desktop backend — gfx_sdl.c, main_sdl.c (fixed 30 Hz sim, vsync render)
  psp/        TODO — pspgu + sceAudio + sceCtrl backend
game/
  game.c      Worlds/Game.as orchestration (attract -> launch)   [partial]
  player.c    Interaction/Player.as — full flight model           [weapon/FX TODO]
  uboot.c     Interaction/UBoot.as — attract launcher             [partial]
  backdrop.c  Water / Space bands
gen/          generated (spec header); gitignored
```

## Status (see ../docs/roadmap.md)

Working: pak load, ptx decode, camera, tiled bands, attract mode, launch, the
player flight model (thrust along facing, steering, speed clamp/re-projection,
camera follow with lead, gravity/friction), boost flame.

Not yet: weapon/bullets, enemies, particle FX, water-splash visuals, death spiral,
HUD/text, game-over timeline, audio, the PSP backend. All marked `TODO` inline.

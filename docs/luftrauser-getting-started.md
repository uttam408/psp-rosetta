# Luftrauser — getting started

Status as of first decompile. **The SWF is unwrapped, decompiled, and its assets
run through the pipeline.** What we learned changes the plan for the better.

## What the SWF actually is

The `luftrauser.swf` in circulation is a **MochiAds distribution build**: a small
preloader shell wrapping the real game, MochiCrypt-encrypted, in a `DefineBinaryData`
tag. MochiAds shut down in 2014 so the `patchURL` is dead — but the payload is
**self-keyed** (RC4-style keystream seeded from its own last 32 bytes, then zlib),
so no server and no external key are needed.

```sh
# 1. decompiler (this machine had no Java)
brew install openjdk
curl -sL -o /tmp/ffdec.zip \
  https://github.com/jindrapetrik/jpexs-decompiler/releases/download/version26.2.1/ffdec_26.2.1.zip
unzip /tmp/ffdec.zip -d ~/.local/ffdec

# 2. pull the encrypted payload out of the wrapper
export JAVA_HOME=/opt/homebrew/opt/openjdk
java -jar ~/.local/ffdec/ffdec.jar -export binaryData sources/luftrauser/_wrap luftrauser.swf

# 3. undo MochiCrypt  ->  real SWF
.venv/bin/python tools/mochicrypt_decrypt.py \
  sources/luftrauser/_wrap/binaryData/7_mochicrypt.Payload.bin \
  sources/luftrauser/luftrauser_real.swf          # -> FWS, 2.5 MB

# 4. export assets + AS3 from the REAL swf
java -jar ~/.local/ffdec/ffdec.jar -export image,sound,font,sprite,shape \
  sources/luftrauser/assets  sources/luftrauser/luftrauser_real.swf
java -jar ~/.local/ffdec/ffdec.jar -export script \
  sources/luftrauser/scripts sources/luftrauser/luftrauser_real.swf

# 5. pipeline
.venv/bin/python -m pipeline.cli build --game luftrauser --src sources/luftrauser/assets
#   -> build/luftrauser/assets.pak   (47 assets, ~1.8 MB: 30 sprites, 17 sounds)
```

`sources/` and `*.swf` are gitignored — do not commit game content.

## The big finding: it's built on FlashPunk

The game is written against **FlashPunk**, the open-source AS3 2D engine
(`net.flashpunk.*` — `Engine`, `World`, `Entity`, `Graphic`, `Image`, `Spritemap`,
`Anim`, `Sfx`, `Input`, `Key`, `FP`, `Tween`, `Mask`/`Hitbox`, `Draw`).

`Main.as` in full:

```as3
public class Main extends Engine {
   public function Main() {
      super(480, 320, 30, true);     // 480x320 internal, 30 fps, FIXED timestep
      FP.world = new Game();
   }
}
```

Implications:

- **480×320 @ 30fps, fixed timestep.** Fixed timestep = deterministic = a replay
  oracle is possible later. Resolution: **rebuild native at 480×272, don't crop or
  scale** — see below.
- **Port the FlashPunk subset once, and the game classes port almost mechanically**
  against it. The runtime's engine abstraction should mirror FlashPunk's API.
- FlashPunk's built-in debug `Console` and the Flex `mx.core.*` asset wrappers are
  in the SWF but aren't game content (already excluded in `games/luftrauser/game.toml`).

## Resolution: native 480×272, not a crop

The game is **not a fixed-screen game**. There's a camera that follows the player
(`FP.camera.y = player.y - FP.halfHeight`) across a large world; the screen is a
moving window into it. The world layout (space up top, sky, water below) is defined
by **entity world-coordinates** — `Player` checks `y > water.y` / `y < space.y + 248`
against the actual `Water` and `Space` entities, never against screen height.

Grep of the game code (`Interaction/`, `Worlds/`, `Main`) for resolution literals:
- `Main.as` `super(480,320,…)` — the only real one; change to `super(480,272,…)`
- `Cloud.as` `Spritemap(clCloud,320,160,…)` — sprite-sheet **cell size**, unrelated
- `Water.as` `TiledImage(clWater,800,240)` — tile buffer size, unrelated
- `UBoot.as` `FP.camera.y = 600 - FP.halfHeight` — 600 is the boss's world Y
- everything else keys off `FP.width` / `FP.height` / `FP.halfWidth` / `FP.halfHeight`

So the runtime just constructs `FP` with `width=480, height=272` (`halfHeight=136`).
**No game-logic constant depends on 320.** Effect in play: the view is 48px shorter,
so ~24px less sky and ~24px less water visible at once, player still centered.
Enemies spawn at `FP.camera.x ± FP.width` at world altitudes `100 + rand(500)` —
unchanged. Optional polish: bias the camera a touch (`- FP.halfHeight - 12`) to
reclaim a little upward visibility, since threats mostly dive from above.

Rejected alternatives: rendering 480×320 → scaling to 272 is a **15%** vertical
squish (ships look squat); pillarboxing to 408×272 shrinks all art 15% and wastes
horizontal screen. 480×272 is arguably a *better* frame for a horizontal dogfighter.

The asset pipeline is unaffected — resolution is purely an `FP`/runtime concern.

## Code size (real port surface)

| Part | LOC (AS3) | Notes |
|---|---:|---|
| Game logic (`Interaction/*`, `Worlds/Game`, `Main`, `DataHandler`) | ~3,270 | ~30 small classes; `Player.as` 397, `Worlds/Game.as` 380 |
| FlashPunk core (minus debug console) | ~6,040 | the engine subset to reimplement |
| `mochi/*` | — | **delete** — dead online scores/coins |
| `mx/core/*`, `net/flashpunk/debug/*` | — | strip — asset-wrapper / debug plumbing |

~9.3k lines of AS3 to bring over. Entities: `Player`, `Bullet`, `EBullet`,
enemies `Jet` / `Brit` / `Boot` / `Bootje` / `UBoot`, FX (parts, smoke, trails,
splashes, explosions), `Water`, `Space`, `TextBlurb`.

## Next steps

1. **Read the AS3 into a port spec** — fixed-timestep loop, entity list, the flight
   physics constants in `Player.as`, enemy AI in each `Enemies/*.as`, the spawn /
   difficulty curve and scoring in `Worlds/Game.as`, save data in `DataHandler.as`
   (Flash `SharedObject` → a file on the memstick).
2. **`runtime/` skeleton** (no blockers now):
   - pspdev CMake toolchain + EBOOT packaging (`make` → `EBOOT.PBP`)
   - `pak.c` / `ptx.c` — mount `assets.pak`, upload `.ptx` to a GU texture
   - a FlashPunk-shaped core: `Engine` loop, `Entity`/`World`, `Image`/`Spritemap`,
     `Input` (analog nub + buttons), `Sfx` (`sceAudio` PCM, `sceMp3` music)
   - target PPSSPP first, then hardware
   - toolchain: `brew install pspdev/pspdev/pspdev`, `brew install --cask ppsspp`
3. **Port FlashPunk core**, then the game classes, tuning feel against the original.

## Tools added to the repo

- `tools/mochicrypt_decrypt.py` — standalone MochiCrypt 3.1c unwrapper.
- `games/luftrauser/game.toml` `[crawl] exclude` — drops the debug-console assets.
- flash adapter now understands ffdec's `<id>_<Dotted.Class>` filenames →
  ids like `image/interaction/enemies/jet/enemybody`.

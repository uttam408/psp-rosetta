# FlashPunk API surface used by *Luftrauser*

Reference for the C/PSP port. Everything below is derived from reading the decompiled
engine under `sources/luftrauser/scripts/scripts/net/flashpunk/` and the game code
(`Main.as`, `DataHandler.as`, `Interaction/**`, `Worlds/**`). The `debug/` console is
ignored (not shipped). Line references are into `sources/luftrauser/scripts/scripts/`.

---

## 1. Summary

### How big is the reimplementation surface?

Small. FlashPunk ships ~30 classes; the game exercises a thin slice:

| Area | What the port actually needs |
|---|---|
| Core loop | `Engine` fixed 30 Hz timestep + accumulator; `World` (entity lists, layer buckets, type buckets); `Entity` (transform + velocity integrator + AABB-by-type collision). |
| Tweens | `Tweener`/`Tween`/`Alarm` only — a frame-counting countdown timer with a completion callback. No `MultiVarTween`, no easing, no `FP.tween`. |
| Graphics | A GU sprite layer: textured quad (`Image`), sub-rect frame animation (`Spritemap`/`Anim`), runtime rotate + non-uniform scale + negative-scale flip, RGB tint/modulate, horizontal flip, a horizontally-tiled texture band (`TiledImage`), and a bitmap-font text renderer (`Text`). `PreRotation` collapses to "rotated quad" (see §6). |
| Audio | One-shot sample playback with volume + stereo pan; one looping music stream; a global master volume. `Sfx`/`SfxEmbed`. |
| Input | 8 keyboard keys (`Input.check`/`Input.pressed`) + mouse click position (one hit-test, the mute button). No control-name bindings. |
| Draw (immediate mode) | `Draw.graphic` (draw a Graphic straight to the buffer with camera offset) and one `Draw.rect`. Nothing else. |
| Persistence | `DataHandler` = one integer high score in a `SharedObject` (`DataHandler.as`). Replace with a save file. |
| Assets | 55 embedded asset classes: PNG bitmaps (`mx.core.BitmapAsset`), MP3 sounds (`mx.core.SoundAsset`), one TTF (`mx.core.FontAsset`, font "uni 05_54"). |

Not needed at all: masks/pixel-collision, object pooling (`create`/`recycle`),
`Graphiclist`, camera transform on `Screen`, most of `FP`'s math helpers, most of
`World`'s query API, most of `Draw`. Full list in §7.

### Rendering model

Software double-buffer, blit-per-graphic, camera applied at blit time.

* `Engine(480, 320, 30, true)` (`Main.as:14`) → `FP.width=480`, `FP.height=320`,
  30 fps, **fixed timestep**. Native resolution is **480×320**.
* `Screen` (`Screen.as`) holds two `480×320` `BitmapData` buffers and flips between
  them each frame (`swap`/`redraw`). `FP.buffer` points at the current back buffer.
* Each frame `Engine.render` (`Engine.as:96`): `screen.swap()` →
  `screen.refresh()` fills the whole buffer with `Screen._color` (the background;
  `Game` sets it to `0xE5EFEC` / `15064492`, `Game.as:96`) → `world.render()` →
  `screen.redraw()` shows the buffer.
* `World.render` (`World.as:154`) walks layers. `_layerList` is kept sorted ascending
  and iterated **from the end**, so **higher layer number = drawn first = further
  back**. Within a layer, entities draw oldest-first. Lower layer number is frontmost;
  default layer is `0`.
* `Entity.render` (`Entity.as:154`) calls `graphic.render(FP.buffer, point, camera)`
  where `point = (entity.x, entity.y)` (graphics are `relative` by default) and
  `camera = FP.camera`. `Image.render` (`Image.as:114`) computes
  `screenPos = entityPos + graphic.offset - camera*scroll` and then:
  * if `angle == 0 && scale == 1 && no blend` → `copyPixels` (straight alpha blit),
  * else → `draw` with an affine matrix (rotate about `originX/originY`, then scale).
* **Many game entities bypass `entity.graphic`** and draw manually in an overridden
  `render()` via `Draw.graphic(spr, x, y)`: `Player` (body + wings + boost),
  `Jet`, `Brit` (body + wings), `UBoot` (floating logo), and all of `Game`'s HUD.
  `Draw.graphic` (`Draw.as:278`) renders a `Graphic` to the current target
  (`FP.buffer`) with `FP.camera` offset. This is the immediate-mode path and it
  runs every frame.
* Layers in use: `0` (player, airborne enemies, particles), `1000` (`Water`,
  `UBoot`), `2000` (`Space`, bullets, explosions, splashes, ships), `4000`
  (`Cloud`, backmost).

Camera: `FP.camera` is a plain `Point` that the game writes directly every frame
(`Player.as:153` / `:310-313`, `UBoot.as:59`). It does smoothed follow
(`cam += (target-cam)*0.05`) plus a decaying random shake. There is no `Screen`
matrix/zoom/rotation in play.

### Update / collision model

* Fixed timestep (`Engine.as`, `FP.fixed == true`): a `flash.utils.Timer` at
  `tickRate = 4 ms` drives an accumulator; each accumulated `rate = 1000/30 ≈ 33.3 ms`
  runs one `update()`; `maxFrameSkip = 5`. **In fixed mode `FP.elapsed` is
  effectively ignored for game logic**: `Tween` and `Spritemap` advance by frame
  count, not seconds (`Tween.as:45`, `Spritemap.as:75`). So `Alarm(60)` = 60 frames
  = 2 s; anim rate `0.5` = advance half a frame per tick = 2 ticks per animation
  frame. The port must reproduce the 30 Hz accumulator with catch-up or all timings
  drift.
* `World.update` (`World.as:133`) walks the update linked list: per entity, update
  its tweens, `entity.update()`, then `graphic.update()` (animation).
* `Entity.update` (`Entity.as:147`) integrates motion:
  `moveBy(Hspeed, Vspeed)` (no collision — the swept/solid path of `moveBy` is
  **never** used); `vspeed += gravity`; `speed = approach(speed, 0, friction)`.
  `moveBy` (`Entity.as:645`) accumulates sub-pixel remainder and rounds to integer
  positions.
* Collision is **AABB-by-type only**. See §5.

---

## 2. `Engine` / `Main` / core loop

`Main` (`Main.as`) extends `Engine`, ctor `super(480,320,30,true)` then
`FP.world = new Game()`.

| Member | Used by | Port note |
|---|---|---|
| `Engine(w,h,frameRate,fixed)` ctor | `Main.as:14` | Set globals; init screen buffers; create the first `World`. |
| fixed-timestep loop (`onTimer`, `tickRate=4`, `rate=1000/30`, `maxFrameSkip=5`, `maxElapsed=0.0333`) | implicit | Accumulator at 30 Hz, up to 5 catch-up steps, then one render. Use `sceKernelGetSystemTimeWide`. |
| `FP.elapsed` | ignored in fixed mode | Game logic is frame-counted; you can hardcode dt = 1 frame. |
| `Engine.update` → `world.update()` + `world.updateLists()` + world switch check | loop | `updateLists` applies deferred add/remove (see §4). |
| `Engine.render` → buffer swap + clear + `world.render()` | loop | On GU: clear to bg color, draw, swap. |
| `FP.console.log(...)` | `Game.as:182` (every frame) | Leftover debug (`Input.mouseX + " : " + Input.mouseY`). **Drop it.** |

`MochiServices` / `MochiEvents` / `MochiScores` (`Game.as`, `Player.as`, `UBoot.as`,
`Boot.as`, `Bootje.as`) are the Mochi web-portal SDK (ads, achievements,
leaderboards). **Out of scope for PSP** — stub to no-ops.

---

## 3. `FP` (global utility singleton)

Only these are touched (counts are call sites across game code):

| Member | Sig / semantics | Sites | Port note |
|---|---|---|---|
| `FP.world` (get) | current `World` | ~183 | The active world pointer. |
| `FP.world = x` (set) | `Engine.as` defers to `_goto`, switch happens at end of frame (`checkWorld`, `Engine.as:237`): `end()` old, swap, `begin()` new, camera = new world's camera | `Main.as:15`, `Game.as:293`, `UBoot`→`Player` add | Deferred world swap. |
| `FP.rand(n)` | `uint`, LCG: `_seed = _seed*16807 % 2147483647; return _seed/2147483647 * n` | ~87 | **Port this LCG exactly** — enemy aim, particle spread, camera shake, spawn tables all depend on it. Seed is randomized at startup (`FP.randomizeSeed`). |
| `FP.random` | `Number` 0..1, same LCG step | ~9 | Same generator, same stream. |
| `FP.choose(...args)` | returns `args[rand(args.length)]` | ~9 | e.g. `FP.choose(0,0,1,1,2,2,3)` weighted spawn pick. |
| `FP.angle(x1,y1,x2,y2)` | `atan2(y2-y1, x2-x1) * DEG`, normalized to `[0,360)` | ~11 | `DEG = -180/π`. **Angles are degrees, clockwise, Y-down.** |
| `FP.angleXY(obj,angle,dist,ax=0,ay=0)` | writes `obj.x = cos(angle*RAD)*dist+ax`, `obj.y = sin(angle*RAD)*dist+ay` | `Player.as:309` | `RAD = π/-180` (negative). |
| `FP.distance(x1,y1,x2,y2)` | euclidean | 2 | |
| `FP.scaleClamp(v, lo, hi, lo2, hi2)` | remap `v∈[lo,hi]` to `[lo2,hi2]`, clamped | ~13 | Used for stereo pan (`dx∈[-300,300] → pan∈[-0.25,0.25]`) and music volume vs. altitude. |
| `FP.camera` | `Point`, written directly by game | ~38 | Plain mutable `{x,y}`. |
| `FP.width` / `FP.height` / `FP.halfWidth` / `FP.halfHeight` | `480 / 320 / 240 / 160` | many | Constants. |
| `FP.volume` (get/set) | sets `SoundMixer.soundTransform.volume` (global master) | ~18 | Global audio gain 0..1. Mute toggle sets it to 0/1. |
| `FP.RAD` | `π/-180` | ~3 | Used as `Math.sin(FP.RAD * angle)` for wing banking. |
| `FP.stage` | `Game.as:101` (Mochi only) | 1 | N/A. |
| `FP.screen` | `Game.as:96` sets `FP.screen.color` | 1 | Background color. |

`FP.randomSeed` / `FP.randomizeSeed` are called once internally by `Engine`.

Everything else in `FP.as` (`lerp`, `colorLerp`, `approach` [used *inside* Entity],
`clamp`, `getColor*`, `stepTowards`, `anchorTo`, `rotateAround`, `distanceRects`,
`frames`, `shuffle`, `sort`/`sortBy` [used *inside* World], `next`/`prev`, `swap`,
`getBitmap`, `getXML`, `tween`, `log`/`watch`) is **not called by game code**.
`FP.approach` and `FP.sort` are used internally by `Entity`/`World` and must exist as
private helpers.

---

## 4. `World`

`Game` (`Worlds/Game.as`, 380 lines) is the only `World`. It overrides
`begin`, `update`, `render`.

| Member | Where | Port note |
|---|---|---|
| `add(e)` / `remove(e)` | ~183 sites | Deferred: pushed to `_add`/`_remove`, applied in `updateLists()` at end of frame. Sets `e._world`. |
| `updateLists()` | `Engine` each frame | Process `_remove` (call `removed()`, unlink from update/render/type lists, clear tweens if `autoClear`), then `_add` (link lists, add to type bucket, call `added()`), then re-sort `_layerList` if dirty. |
| linked lists: update list, per-layer render list (`_renderFirst/_renderLast` by `_layer`), per-type buckets (`_typeFirst` by `_type` string) | internal | Three intrusive doubly-linked lists on `Entity`. Type buckets are the collision index. |
| `camera` (`Point`) | `Game` has one; `Engine` copies `FP.camera = world.camera` on switch | Per-world camera. |
| `classCount(Class)` | ~20 sites (`classCount(Player)`, `classCount(UBoot)`, `classCount(PlayerPart)`, `classCount(JetPart)`, `classCount(BritPart)`) | Count of live entities of an exact class. Needs a per-class counter (`_classCount` dict, keyed by `getDefinitionByName(getQualifiedClassName(this))` — see `Entity` ctor). |
| `classFirst(Class)` | `Game.as:158,166` (`classFirst(UBoot)`) | First live entity `is Class` (linear scan). |
| `typeCount(String)` | `Game.as:320` (`typeCount("Enemy")`) | Count in a type bucket. |
| `getClass(Class, arr)` | `Game.as:129,270`, plus | Fill array with all entities `is Class`. |
| `furthestFromEntity(type, ent, false)` | `Jet.as:93`, `Brit.as:98,195` | Returns an entity of `type` far from `ent` (note engine impl is quirky: squared-dist threshold starting at 50, returns last that exceeds running max — replicate as-is or the "no player" wander AI changes). |
| `begin()` / `end()` | `Game.begin` (Mochi connect) | Lifecycle hooks; `end()` unused by game. |
| `addTween(tween, start)` | `Game`, most entities | Inherited from `Tweener`. See §8. |

Not used by game: `removeAll`, `addList`/`removeList`, `addGraphic`, `addMask`,
`create`/`recycle`/`clearRecycled*` (**no pooling**), `bringToFront`/`sendToBack`/
`bringForward`/`sendBackward`/`isAtFront`/`isAtBack`, `collideRect`/`collidePoint`/
`collideLine`/`collideRectInto`/`collidePointInto`, `nearestToRect`/`nearestToEntity`/
`nearestToPoint`, `getType`/`getLayer`/`getAll`, `mouseX`/`mouseY`, `count`,
`typeFirst`/`layerFirst`/`layerLast`/`farthest`/`nearest`/`layers`/`uniqueTypes`/
`layerCount`.

---

## 5. `Entity` & collision

20 game classes extend `Entity` (4 of those via `Enemy`, `Interaction/Enemy.as`).

### Entity members the game uses

| Member | Notes |
|---|---|
| ctor `Entity()` (no args form) | Game always calls `super()` then sets `x`/`y`. |
| `x`, `y` (`Number`) | Position. Written/read everywhere. |
| `hspeed`, `vspeed` (get/set) | Setter also recomputes `direction = FP.angle(0,0,hspeed,vspeed)`. |
| `speed` (get/set) | Magnitude of `(hspeed,vspeed)`; setter uses `direction`. |
| `direction` (`Number`, degrees) | Heading. Set directly and via speed/hspeed/vspeed. |
| `gravity`, `friction` (`Number`) | Applied in `Entity.update`: `vspeed += gravity`, `speed = approach(speed, 0, friction)`. |
| `update()` | Overridden by ~all; they call `super.update()` for the integrator. |
| `render()` | Overridden by several to draw manually (see §1). |
| `added()` | Not overridden by game. |
| `removed()` | Overridden in `Enemy` and each enemy subclass (scoring / difficulty bump). Called from `updateLists`. |
| `setHitbox(w, h, ox, oy)` | Sets `width/height/originX/originY`. The **only** hitbox mechanism used. |
| `type` (get/set `String`) | Adds/removes from world type bucket when `_added`. Values: `"Player"`, `"Enemy"`, `"Bullet"`, `"EnemyBullet"`, and `""` to detach a dead ship. |
| `layer` (get/set `int`) | Re-buckets render list. Values: `1000`, `2000`, `4000` (default `0`). |
| `graphic` (set) | Assigns the auto-render graphic (`Image`/`Spritemap`/`TiledImage`). |
| `collidable` (`Boolean`) | Toggled `false` on dead ships so bullets pass through. |
| `collide(type, x, y)` → `Entity` | See below. |
| `collideInto(type, x, y, array)` | See below. |
| `motionAdd(angleDeg, magnitude)` | `hspeed += cos·mag; vspeed += sin·mag` (via `FP.angleXY`). ~12 sites. |
| `moveBy(dx, dy)` | Called only by `Entity.update`, **no type arg** → no collision, just `x/y += round(accum)`. |
| `distanceFrom(entity)` | Center-to-center euclidean (`param2=false`). `Jet`, `Brit`, `Bootje`. |
| `width`, `height`, `originX`, `originY` | Read for wrap-around / hitbox math. |
| `world` (get) | `== FP.world`. |

`Entity` extends `Tweener` (tween list) — see §8.

### Collision — exactly how simple it is

**Pure axis-aligned bounding-box overlap, bucketed by type string. No masks, no
pixel test, no sweeping, no rotation.**

* `Entity._mask` is **never assigned** anywhere in the game. So `collide()` and
  `collideInto()` always take the fast path (`Entity.as:190-206`, `:351-367`):
  rectangle overlap between `this` (moved to the test `x,y`) and each entity in
  `world._typeFirst[type]`, using `x - originX .. x - originX + width` on both
  sides. `collidable` must be true on both.
* Two call shapes only:
  * `collide(type, x, y)` → first overlapping entity or `null`:
    * `Bullet.as:48` — `collide("Enemy", x, y)`
    * `EBullet.as:45` — `collide("Player", x, y)`
    * `Jet.as:115`, `Brit.as:120`, `Boot.as:76`, `Bootje.as:70` — `collide("Player", x, y)` (ram damage)
  * `collideInto(type, x, y, array)` → fills array with all overlaps:
    * `Explosion.as:47`, `LargeExplosion.as:47` — `collideInto("Enemy", x, y, arr)` (splash damage)
* Always called with `x, y` = the entity's own current position (no look-ahead).
* Hitboxes (`setHitbox` w,h,ox,oy): Player `4,4,2,2`; Bullet `16,16,8,8`;
  EBullet `8,8,4,4`; Explosion `32,32,16,16`; LargeExplosion `64,64,32,32`;
  SmallExplosion default (uninitialized → `0,0`); Jet/Brit `16,16,8,8`;
  Boot `204,32,0,-16`; Bootje `48,20,0,-12`. **Hitboxes are unrotated even though
  the sprite rotates** — the plane's collision box is a fixed 4×4 (Player) / 16×16
  (enemies) square.

Port: one function `entity* overlap_type(entity* self, const char* type)` doing
rect-vs-rect against a linked list, plus an `_into` variant. That is the entire
collision system.

`Entity` methods **not used**: `collideTypes`, `collideWith`, `collideRect`,
`collidePoint`, `collideTypesInto`, `onCamera`, `moveTo`, `moveTowards`,
`moveBy`-with-type/solid, `moveCollideX/Y`, `clampHorizontal`, `clampVertical`,
`distanceToPoint`, `distanceToRect`, `addGraphic`, `setHitboxTo`, `setOrigin`,
`centerOrigin`, `renderTarget`, `visible`/`active` (left at defaults), `mask`.

---

## 6. Graphics — Spritemap, rotation, PreRotation

### `Image` (`graphics/Image.as`)

| Constructor / member | Sites | Port note |
|---|---|---|
| `new Image(EmbedClass)` | `Bullet`, `Boot`, `Bootje`, `UBoot` (×2) | Textured quad from a PNG. |
| `centerOO()` | ~27 (all graphics) | Shift x/y by origin and set origin to center — i.e. "draw centered on entity pos". |
| `scale` (`Number`) | `Bullet.as:32,44` (1.5 → 1 pop-in) | Uniform scale multiplier. |
| `scaleX` / `scaleY` | `Player.as:303` (`scaleX = ±1` splash flip), wings `scaleY = sin(...)` | Non-uniform scale; **negative allowed** (mirror). |
| `angle` (`Number`, degrees) | planes/wings/boost | Runtime rotation about origin. `Image.render` uses matrix path when `angle != 0`. |
| `color` (`uint` RGB) | ~8 (`Text.color`, HUD) | Multiply tint (`ColorTransform` on the buffer). For PSP: vertex color / `sceGuColor` modulate. |
| `flipped` (`Boolean`) | `Boot.as:44`, `Bootje.as:40` | Horizontal mirror of the source (engine caches a flipped bitmap; on GU just negate U or `scaleX`). |
| `width` / `height` (get) | many | Buffer dimensions. |
| `.alpha` | not used by game | skip. |

`Image.render` fast path = alpha blit; slow path = affine (`rotate` about
`originX/originY`, then `scale`, translate to `screenPos`). Blend modes: `blend` is
never set by game code → always normal alpha.

`Image.createRect` / `createCircle` — not used.

### `Spritemap` (`graphics/Spritemap.as`) + `Anim` (`graphics/Anim.as`)

The workhorse animated graphic. 17 `new Spritemap(...)` sites.

| Member | Sites | Port note |
|---|---|---|
| `new Spritemap(EmbedClass, frameW, frameH, callback=null)` | 17 | Grid atlas; `callback` fires when a non-looping anim completes. |
| `add(name, framesArray, frameRate, loop=true)` | ~20 | Define an animation. `framesArray` is a list of frame indices, e.g. `[0,1,2,3,4,5,6,7,8,9]`. `frameRate` is **frames-advanced-per-tick** in fixed mode (e.g. `0.5` → 2 ticks/frame, `0.2`, `0.3`). |
| `play(name, reset=false)` | ~20 | Start an animation. `Player.as:223` uses `play("Idle", true)`. |
| `frame` (get/set `int`) | ~10 | Direct frame index (kills any running anim). `Game_clSkull` frame 0/1; mute icon frame 0/1; particles set `frame = FP.rand(frameCount)`. |
| `randFrame()` | `Cloud.as:22` | `frame = FP.rand(frameCount)`. |
| `frameCount` (get) | 3 (particles) | `columns*rows`. |
| `complete` (`Boolean`, get) | `Player.as:112` | True when a non-looping anim finished. |
| `callback` (`Function`) | ctor arg | Almost always `removeThis` → despawn entity when the one-shot animation ends (explosions, splashes, hits, smoke, trails). |
| inherited `angle` / `scaleX` / `scaleY` / `centerOO` / `color` | planes' boost sprites, splash | Same as `Image`. |
| `update()` | called by `World` (and manually in `Player.as:375-376`) | Advance `_timer += frameRate * rate` (fixed mode), step index, wrap or clamp+fire callback. |

Not used: `rate` (left 1), `index`, `setFrame`, `getFrame`, `setAnimFrame`,
`randFrame` aside, `columns`/`rows`/`currentAnim`. `Anim.play()` (via the returned
`Anim`) not used directly.

Animation catalogue (name, frames, rate, loop):
`Player` boostStart `Idle [0..4] 0.3 false`, boost `Idle [0,1] 0.3 true`;
`EBullet` `Idle [0,1] 0.5 true`; `Explosion` `Explode [0..9] 0.5 (once)`;
`LargeExplosion` `Explode [0..13] 0.5`; `SmallExplosion` `Explode [0..6] 0.5`;
`Smoke` `Smoke [0,1,2] 0.2`; `JetTrail` `Trail [0,1,2] 0.2`;
`BulletHit` `Hit [0,1,2,3] 0.5`; `WaterSplash` `Splash [0,1,2]/[1,2]/[2] 0.3`;
`BigWaterSplash` `Splash [0,1,2] 0.3`.

### Rotation: **runtime, not pre-baked** — important

* Planes (`Player`, `Jet`, `Brit`) create `PreRotation` graphics
  (`new PreRotation(clBody, 360, false)` for Player, `..., 90, false` for enemies)
  — `Player.as:26,30`, `Jet.as:31,35`, `Brit.as:32,36`. 6 sites total.
* **But the game only ever sets `.angle` (the inherited `Image.angle`), never
  `.frameAngle`.** `PreRotation.render` (`PreRotation.as:92`) selects its pre-baked
  frame from `frameAngle`, which stays `0`, so it always shows frame 0 — and then
  calls `super.render()` (`Image.render`), which applies `this.angle` as a **runtime
  affine rotation**. Net effect: the pre-rotation atlas is dead weight; the sprite is
  a single frame rotated live by the GU.
* So for the port: **drop `PreRotation` entirely**. Treat plane body/wings as normal
  `Image`s rotated by a quad transform. The expensive `PreRotation` constructor
  (which bakes a `frameW*90`-wide atlas, padding each frame to the sprite's
  *diagonal* so rotation doesn't clip) can be skipped.
* Wing detail: every frame the game sets
  `wings.angle = body.angle` and `wings.scaleY = Math.sin(FP.RAD * body.angle)`
  (`Player.as:371-372`, `Jet.as:97-98`, `Brit.as:102-103`) — a vertical squash that
  passes through 0 and goes negative, giving a fake-3D "banking / rolling" look. The
  GU quad transform must honor negative `scaleY`.
* Pivot: `centerOO()` is called on all of them, so rotation is about the sprite
  center, positioned at the entity's `(x,y)`.
* `FP.rand(this.shake)` etc. jitter the camera, not the sprite.

`FP.DEG`/`FP.RAD` sign: `RAD = π / -180`. Rotation is **clockwise for positive
angle** in screen space (Y-down). Match this or planes fly backwards.

### `TiledImage` (`graphics/TiledImage.as`)

| Site | Usage |
|---|---|
| `Space.as:13` | `new TiledImage(clSpace, 800, 240)` — the sky band. |
| `Water.as:13` | `new TiledImage(clWater, 800, 240)` — the sea band. |

Fills an `800×240` rect by tiling the source texture. `offsetX/offsetY/setOffset`
are **never called** → no texture scrolling. Each frame the owning entity sets
`x = player.x - width/2` (`Space.as:32`, `Water.as:32`), so the band just follows the
player. Port: a single quad with a repeating texture (or 800/tileW quads), snapped to
player X. `Water.y = 640`, `Space.y = -space.height` initially (`≈ -240`); these Y
values are the world's "sea level" and "space ceiling" and are read all over gameplay
(`if (y > water.y)` = "hit the sea", `if (y < space.y + 248/200)` = "hit space").

### `Graphiclist` — imported by `Entity.addGraphic` internally but **never used by game**.

---

## 7. Text / font

`Text` (`graphics/Text.as`) extends `Image`; renders a `flash.text.TextField` with an
**embedded font** into a bitmap, rebuilt on every property change.

* Embedded font (`Text__FONT_DEFAULT.as`): TTF `default` / family **"uni 05_54"** —
  a well-known 5-px freeware pixel font. `Text.font = "default"`, `Text.size = 16`
  (statics, never changed by game).
* Construction sites (**all in 2 files**):
  * `TextBlurb.as:21` — `new Text("+10" | "+20" | "+40" | "+100", 0, 0)`: the floating
    score popup on a kill. `centerOO()`, `color = 0x610F1D`, rises 1 px/frame,
    despawns after 60 frames.
  * `Game.as` HUD, all in the overridden `render()` and **re-created every frame**
    (immediate mode):
    * `:121` `"SCORE: n"` (top-left, `color 0x8D4E0D`)
    * `:137` game-over text block: multi-line (`"KILLS: ...\nPLANES: ...\n..."`),
      width `FP.width-20`, height `FP.height-90`, `alignment = CENTER`
      (`TextFormatAlign.CENTER`, the only `Text.alignment` use, `:141`),
      `updateBuffer(true)` (`:143`, the only explicit `updateBuffer` call),
      manual centering via `originX = width/2`.
    * `:149` the long credits string, scrolled horizontally by `sliderX`.
    * `:160,163,166` `"PRESS UP TO LAUNCH"`, `"ARROW KEYS + X ~ RELEASE X TO REPAIR"`,
      `"BEST: n"` — start-screen hints, positioned by `300 - text.width/2`.
* Character set: uppercase A–Z, digits, space, and punctuation `: + - ~ | < > \n`.
  The only non-ASCII is `<3` (literal) and `~`. A **96-glyph ASCII bitmap font atlas
  is more than enough.**
* `Text` properties used: ctor `(string, x, y[, w, h])`, `.color`, `.centerOO()`,
  `.width`, `.originX`, `.x`, `.alignment` (once), `.updateBuffer(true)` (once),
  `.text` (implicitly, via `substr` slicing for the game-over typewriter effect —
  actually the string is sliced *before* construction, `Game.as:137`).

**Recommendation:** replace `Text` wholesale with a bitmap-font blitter:
one PNG glyph atlas ("uni 05_54" rasterized at 16 px, or any readable pixel font),
a `draw_text(str, x, y, rgb)` with left/center alignment and `\n` line breaks, and a
measure function for centering. No `TextField`, no TTF rasterizer needed. This
removes the single biggest chunk of Flash-runtime dependency.

`Text` setters `font`/`size`, and `Text` as a general rich-text widget — not needed.

---

## 8. Tweens — `Tweener` / `Tween` / `Alarm`

**Only `Alarm` is used.** `tweens/misc/Alarm.as`. ~26 `new Alarm(...)` sites.

* Pattern is always: `addTween(new Alarm(frames, callbackFn), true)` — the `true`
  starts it immediately (`Tweener.addTween`, `Tweener.as:27`).
* `Alarm(duration, complete, type=Tween.PERSIST)` — the game **never passes a
  type**, so it is always `PERSIST (0)`: on completion `_time = _target`,
  `active = false`, `complete()` is called, and **the tween stays attached but
  inactive** (it is *not* auto-removed — that would be `ONESHOT`). Minor: finished
  alarms accumulate on long-lived entities; negligible.
* `duration` is in **frames** (fixed mode: `Tween.update` does `_time += 1` per tick,
  `Tween.as:45`).
* Re-arming: callbacks routinely `addTween(new Alarm(...), true)` again to make a
  repeating timer (`Jet.shootBullet`, `Boot.fireCannons`, `Game.spawnMoreEnemies`,
  etc.).
* `Alarm.reset()` / `elapsed` / `duration` / `remaining` — **not used**.
* `Tweener` API used: `addTween(tween, start)`; internally `updateTweens()` (called
  by `World.update` / `Engine.update`), `removeTween`, `clearTweens`.
  `autoClear` left `false`.

**Not used:** `MultiVarTween` (imported by `FP` only), `FP.tween`, easing functions,
`Tween.LOOPING`, `Tween.ONESHOT` as an explicit arg, `Tween.percent`/`scale`/
`start`/`finish` from game code, `Tweener` on `Entity` beyond alarms.

Port: an `Alarm` = `{ int frames_left; void(*cb)(void*); void* ctx; bool active; }`
list per entity/world, decremented once per update, fires `cb` at 0.

Callback list (representative): `Game.unaware`, `Game.spawnMoreEnemies`,
`Game.spawnEnemies`→`spawnMoreEnemies`, `Bullet.removeThis`, `EBullet.removeThis`,
`UBoot.fireAlarm0/fireAlarm1/removeThis`, `Jet.shootBullet`, `Brit.shootBullet`/
`performIntelligence`, `Boot.fireCannons/fireSecondary`, `Bootje.fireCannons`,
`JetPart/BritPart/PlayerPart.removeThis`, `TextBlurb.removeThis`.

---

## 9. Audio — `Sfx` / `SfxEmbed`

`SfxEmbed.as` (`_sound = new EmbedClass()`), `Sfx.as`.

| Member | Sites | Port note |
|---|---|---|
| `new SfxEmbed(EmbedClass)` | ~17 sound classes | One `Sound` per class, cached in a static dict. |
| `play(volume=1, pan=0)` | most SFX | One-shot. `pan ∈ [-1,1]` computed via `FP.scaleClamp(x - player.x, -300, 300, -0.25..±1, ...)` for stereo positioning. `volume` is `FP.volume` (master, 0 or 1). |
| `loop(volume=1, pan=0)` | `Game.startMusic` → `bgMusic.loop(FP.volume/2)` | Looping music (`Game_clMusic.mp3`). On `SOUND_COMPLETE` it re-`loop`s. |
| `stop()` | `Player` boost sound, `Game.as:292` (music on restart) | Stop channel. |
| `.playing` (get) | `Player.as:230,329` | Is a channel active. |
| `.volume` (set) | `Game.as:303` `bgMusic.volume = FP.scaleClamp(player.y, 100, 600, 0.5, 0.75)` | Live music-volume ducking by altitude. |
| `complete` (`Function`) | ctor 2nd arg — **never passed by game** | skip. |
| `FP.volume` (global) | mute toggle, all `play` calls | Master gain → `sceAudio` output volume or per-voice scale. |

Not used: `Sfx.resume`, `.pan` as a live property, `.position`, `.length`.

Port: `sceAudio` (or the Media Engine for the MP3 music). ~17 short SFX
(decode to PCM at load), 1 streaming/looping music track. Volume + a simple L/R pan
(scale the two channels). Master mute = 0/1.

MP3s: the SFX and music are `.mp3` embeds. Either pre-convert to `.wav`/`.at3` in the
asset pipeline or decode with the ME.

---

## 10. Input — `Input` / `Key`

| Member | Sites | Port note |
|---|---|---|
| `Input.check(keyCode)` | ~21 | Key held this frame. |
| `Input.pressed(keyCode)` | 4 (`Key.UP`, `Key.X`) | Key went down this frame. |
| `Input.mousePressed` (`Boolean`) | `Game.as:183` | Mouse click this frame (mute button). |
| `Input.mouseX` / `Input.mouseY` | `Game.as:182,185,187` | Cursor pos in screen space; hit-tests the mute rect `x∈(450,480), y∈(10,30)`. |

Keys referenced (`Key.as` constants → JS keycodes): `UP(38)`, `W(87)`, `LEFT(37)`,
`A(65)`, `RIGHT(39)`, `D(68)`, `X(88)`, `SPACE(32)`. That's the **entire** control
set. `Key.UP`/`Key.W` = thrust/launch, `Key.LEFT/A` + `Key.RIGHT/D` = turn,
`Key.X`/`Key.SPACE` = shoot (and `X` = restart on the game-over screen, hold-to-repair
mechanic keys off `X`/`SPACE` *not* held).

**No `Input.define` / control-name strings, no released(), no mouse wheel, no
keyString.** 

Port mapping suggestion: D-pad L/R = turn, Cross/R-trigger = thrust, Square/L-trigger
= shoot, Start = restart. The mouse mute toggle → map to Triangle or Select, or drop
the on-screen mute button and use a system-menu volume.

---

## 11. `Draw`

| Function | Sites | Port note |
|---|---|---|
| `Draw.graphic(g, x=0, y=0)` | 17 (`Player`, `Jet`, `Brit`, `UBoot`, `Game` HUD) | Render a `Graphic` immediately to `FP.buffer` at `(x,y)` with `FP.camera` offset. This is just "blit this sprite now". |
| `Draw.rect(x, y, w, h, color, alpha=1)` | 1 (`Game.as:127`) | The expanding black bar behind the game-over screen. Solid `fillRect` when `alpha>=1`. |

Everything else in `Draw.as` (`line`, `linePlus`, `circle`, `circlePlus`, `hitbox`,
`curve`, `entity`, `setTarget`, `blend`) is **not used by game code**.
`Draw.resetTarget` is called once by `Engine.render`.

---

## 12. Explicitly NOT used (do not build)

* **Masks entirely:** `Mask`, `masks/Hitbox`, `masks/Masklist`, pixelmask, any
  `Mask.collide`. `Entity.mask` is never set. Collision is rect-only (§5).
* **Object pooling:** `World.create` / `recycle` / `clearRecycled*` /
  `Entity._recycleNext`. The game `new`s freely (a `JetTrail` per jet per frame, a
  `Text` per HUD element per frame).
* **`Graphiclist`** (multi-graphic container).
* **`PreRotation`'s pre-baked rotation** — imported and constructed but its frame
  atlas is never selected (see §6). Collapse to runtime-rotated quad.
* **`TiledImage` scrolling** (`offsetX/offsetY/setOffset`) — never called.
* **`MultiVarTween`, `FP.tween`, easing, `Tween.LOOPING`.**
* **`Screen` transform:** `x/y/scale/scaleX/scaleY/angle/originX/originY/smoothing/
  capture/mouseX/mouseY`. Only `Screen.color` (bg) and the double-buffer swap matter.
* **Most of `FP`:** `lerp`, `colorLerp`, `clamp`, `clampInRect`, `getColorRGB/HSV`,
  `getRed/Green/Blue`, `stepTowards`, `anchorTo`, `rotateAround`, `distanceRects`,
  `distanceRectPoint`, `scale` (unclamped), `frames`, `shuffle`, `sortBy`, `next`,
  `prev`, `swap`, `getBitmap`, `getXML`, `timeFlag`, `watch`, `sign`. (`approach`
  and `sort` are needed only as `Entity`/`World` internals.)
* **Most of `World`:** all `collide*` on World, all `nearestTo*` (but keep
  `furthestFromEntity`), `getType/getLayer/getAll`, z-order shuffles
  (`bringToFront` etc.), `removeAll`, `addList/removeList`, `addGraphic/addMask`,
  `mouseX/mouseY`, and the dozen count/first accessors except
  `classCount`/`classFirst`/`typeCount`/`getClass`.
* **Most of `Entity`:** see end of §5.
* **Most of `Draw`:** see §11.
* **`Sfx.resume` / `.position` / `.length` / live `.pan`.**
* **`Input`:** `define`, `released`, control-name strings, `keys`, mouse wheel,
  `keyString`, `lastKey`, `mouseFlash*`.
* **`Console` / `FP.log` / `FP.console`** (debug console; one stray `FP.console.log`
  in `Game.update` — delete it).
* **Mochi SDK** (`mochi.as3.*`): ads, achievements, leaderboards, `startPlay`.
  Stub to no-ops.
* **`flash.net.SharedObject`** (`DataHandler.as`) → replace with a PSP save-data
  file holding one int.

---

## 13. Trickiest parts for the PSP port

1. **Bitmap-font text system.** `Text` is the deepest Flash-runtime tie (TextField +
   embedded TTF, rebuilt every frame). Must be rebuilt as a glyph-atlas blitter with
   center alignment, `\n` handling, and text measurement for the game-over screen and
   the centered start-screen hints. ~7 construction patterns, all in `Game.render` /
   `TextBlurb`.
2. **Deterministic RNG + angle convention.** `FP.rand` is a specific LCG
   (`seed*16807 mod 2^31`); `FP.random` shares the stream; angles are **degrees,
   clockwise, Y-down**, with `RAD = π/-180`. Enemy aiming (`FP.angle`+`motionAdd`),
   particle spray, camera shake, and the weighted spawn tables (`FP.choose`) all
   depend on exact reproduction, or the game *feels* different.
3. **Fixed 30 Hz accumulator with catch-up.** All timing is frame-counted:
   `Alarm` durations, animation rates (`0.2`–`0.5` frames/tick), camera smoothing
   (`*0.05`), the typewriter game-over text. The `Timer(4ms)` + `rate=33.3ms` +
   `maxFrameSkip=5` loop must be reproduced; you cannot just run "as fast as vsync".
4. **Rotated sprites vs. unrotated hitboxes on GU.** Planes are runtime-rotated quads
   about a centered origin, with the wings doing `scaleY = sin(angle)` (passes
   through zero and negative) for a banking effect, and the source frame is padded to
   the sprite diagonal. The collision box, meanwhile, is a fixed tiny AABB (Player
   4×4) that does *not* rotate. Getting the visual pivot right while keeping the
   (separate, axis-aligned) hitbox correct needs care.
5. **Immediate-mode overdraw / allocation churn.** No pooling: a `JetTrail` entity
   per jet per frame, `new Text` per HUD element per frame, `new Explosion`/particles
   in bursts. On PSP you'll want a frame arena or pools even though the original
   didn't — but behavior (spawn counts, `classCount(...) < 15` caps) must match.

### Surprises

* **`PreRotation` is dead code here.** The game imports and constructs it (expensive
  atlas bake) but never drives `frameAngle`; rotation is actually the inherited
  runtime `Image.angle`. The port can ignore the class entirely.
* **Zero pixel-perfect collision.** 4 type buckets, two call sites shapes
  (`collide` / `collideInto`), pure rect overlap, no look-ahead, no sweeping. The
  whole collision engine is ~30 lines.
* **`TiledImage` never scrolls** — `Water`/`Space` are just textured bands snapped to
  the player's X; the "sea level" (`water.y = 640`) and "ceiling" (`space.y ≈ -240`)
  are gameplay constants read everywhere.
* **A debug `FP.console.log` runs every frame** in `Game.update` (`Game.as:182`).
* **`Alarm`s are `PERSIST`, not `ONESHOT`** — finished timers stay attached inactive
  rather than being freed; harmless but note it if you port the tween list literally.
* **World runs in a large coordinate space** (thousands of units tall) with a
  following camera; the framebuffer is only 480×320.
* **Master audio is binary** — the mute button and all `play()` calls pass
  `FP.volume` which is only ever `0` or `1`; only the music does continuous volume
  (altitude ducking, `0.5`–`0.75`).

# Luftrauser — Gameplay / Behavior Spec (from decompiled AS3)

Source: `sources/luftrauser/scripts/scripts/`. Engine is FlashPunk (`net/flashpunk/`).
All `file:line` refs are into that tree. "Inferring" = decompiler obscured intent.

Coordinate / angle conventions (critical — replicate exactly):

- FlashPunk `FP.RAD = Math.PI / -180`, `FP.DEG = -180 / Math.PI` (`net/flashpunk/FP.as:77,79`). Angles are **degrees, clockwise-from-+X but with Y negated in the trig**.
- `FP.angleXY(o, deg, mag)` → `o.x = cos(deg*RAD)*mag`, `o.y = sin(deg*RAD)*mag` (`FP.as:254`). With RAD negative: `o.x = cos(deg°)*mag`, `o.y = -sin(deg°)*mag`. So angle 0 = +X (right), angle 90 = **up** (−Y), angle 270 = down.
- `FP.angle(x1,y1,x2,y2) = atan2(y2-y1, x2-x1) * DEG`, then `+360` if negative → range [0,360) (`FP.as:248`).
- `FP.distance` = Euclidean (`FP.as:270`).
- World Y increases downward. Water is at large +Y (640), space ceiling at small/negative Y.

### FlashPunk Entity motion model (`net/flashpunk/Entity.as`)

- Fields: `x,y` (Number), `hspeed`/`vspeed` (backing `Hspeed`/`Vspeed`), `direction` (deg), `friction`, `gravity` (`Entity.as:22-30`).
- `set hspeed`/`set vspeed` also recompute `direction = FP.angle(0,0,hspeed,vspeed)` (`Entity.as:106-121`).
- `get speed = FP.distance(0,0,hspeed,vspeed)`; `set speed` re-projects: `angleXY(tmp, direction, value); hspeed=tmp.x; vspeed=tmp.y` (`Entity.as:123-137`). **Setting `speed` snaps the velocity vector onto `direction`.**
- `motionAdd(deg,mag)`: `angleXY(tmp,deg,mag); hspeed+=tmp.x; vspeed+=tmp.y` (`Entity.as:709`).
- `Entity.update()` (`Entity.as:147-152`), each step, in this order:
  1. `moveBy(Hspeed, Vspeed)` (integer-stepped sweep with collision solving; for our entities effectively `x+=hspeed; y+=vspeed`).
  2. `vspeed += gravity`
  3. `speed = FP.approach(speed, 0, friction)` — i.e. shrink speed magnitude toward 0 by `friction`, keeping current `direction`. `FP.approach` (`FP.as:186`): move toward target by at most step.
- Subclass `update()` overrides do their logic **then call `super.update()`** last, so gravity/friction/move happen after the AI each frame.

### Timestep

`Main` calls `Engine(480,320,30,true)` (`Main.as:14`): width 480, height 320, **assignedFrameRate 30**, `FP.fixed = true` (`Engine.as:55-61`). Fixed timestep → one `update()` per frame, `FP.elapsed` unused, **all Alarm/Tween durations are in frames** (`Tween.update`: `_time += FP.fixed ? 1 : FP.elapsed`, `Tween.as`). Stage frameRate set to 30 (`Engine.as:126`).
`FP.rand(n)` / `FP.random`: LCG `_seed = _seed*16807 % 2147483647`; `rand(n) = _seed/2147483647 * n` (returned as uint → floor), `random = _seed/2147483647` (`FP.as:399-409`). Seed randomized at startup (`Engine.as:64`). **`FP.choose(...)` picks `args[rand(args.length)]`** (`FP.as:175`).

---

## 1. Engine config

| Item | Value | Source |
|---|---|---|
| Logical resolution | 480 × 320 | `Main.as:14` |
| `FP.halfWidth / halfHeight` | 240 / 160 | `FP.as:98-105` |
| Frame rate | 30 fps | `Main.as:14` |
| Timestep | fixed (`FP.fixed=true`), 1 update/frame; tween/alarm units = frames | `Engine.as:59`, `Tween.as` |
| Initial world | `new Game()` | `Main.as:15` |
| Screen clear color | `0xE5D6EC` (15064492) | `Game.as:96` |
| PSP note | build manifest targets 480×272 from 640×480 native; logical game space stays 480×320 | `build/luftrauser/assets.manifest.json` |

---

## 2. Game flow / states

`Game` is a FlashPunk `World`. There is no explicit state enum; state is derived from `classCount(UBoot)` and `classCount(Player)`.

### 2a. Construction (`Game.as:68-97`)
- `Game(aware:Boolean=false, volume:Number=1)`. `FP.volume = volume`. Mute sprite frame 0 if volume==1 else 1.
- If `aware==true`: add `Alarm(300, unaware)` → clears `aware` after 300 frames (10 s). `aware` suppresses the tutorial text + the floating logo. (First run passes `aware=false`; restart passes `new Game(false, FP.volume)` — `Game.as:293` — so the "PRESS UP TO LAUNCH" help always shows.) *Inferring: `aware` was meant for "came from a menu that already explained controls"; in this build it is effectively always false-with-help.*
- Adds: `UBoot`, `Water` (`this.water`), `Space` (`this.space`), then 6 `Cloud`s.
- Skull sprite `Game_clSkull` is 326×80, centered; mute sprite 16×16.

### 2b. Intro / "attract" (UBoot on screen, no Player) — `UBoot.as`
- UBoot spawns at `x=300`, `y = 640 + sprUBoot.height/2` (just below water), `vspeed = -0.5` (rising) (`UBoot.as:34-37`).
- Camera hard-locked to `x = UBoot.x - halfWidth`, `y = 600 - halfHeight` while `player==null` (`UBoot.as:57-61`).
- `Alarm(96, fireAlarm0)`: after 96 frames sets `vspeed=0`, `ready=true` (`UBoot.as:38,84-88`). Sub stops rising.
- While `ready && !launch`: render shows "PRESS UP TO LAUNCH", "ARROW KEYS + X ~ RELEASE X TO REPAIR", "BEST: <highscore>" (`Game.as:154-170`).
- Floating logo: while `!aware`, drawn at `x`, `logoy(500) + sin(time)*5`, `time += 0.05/frame` (`UBoot.as:45-53`).
- Music slider credits text scrolls along the bottom while UBoot present & no player: `sliderX += 2` per frame, wraps at 3000 (`Game.as:202-208`), text drawn at `600 - sliderX` (`Game.as:149`).
- Mute toggle: click at screen (450<mx<480, 10<my<30) toggles `FP.volume` 0/1 (`Game.as:183-200`); a mute icon also drawn at (520,460).

### 2c. Launch → Playing (`UBoot.as:62-80`)
On `UP`/`W` while `ready && !launch`:
- `Game.startMusic()` → `bgMusic.loop(FP.volume/2)` (`Game.as:367`).
- Create `player = new Player(UBoot.x, UBoot.y)`, add to world; add `Explosion` at that point.
- `Alarm(48, fireAlarm1)`, `launch=true`. After launch, `logoy -= 8`/frame (logo flies up).
- `fireAlarm1` (48 f later): `vspeed = 1` (sub dives), `Alarm(48, removeThis)`.
- `removeThis` (another 48 f): `Game.spawnEnemies()` then remove UBoot.
- `spawnEnemies` (`Game.as:372-377`): add 2 `Brit` at `FP.choose(camera.x-500, camera.x+width+500)`, `y = 100 + rand(500)`; then `Alarm(30 + rand(60), spawnMoreEnemies)` starts the difficulty loop.

### 2d. Playing
- Player alive. `Game.update` sets `bgMusic.volume = FP.scaleClamp(player.y, 100, 600, 0.5, 0.75)` (`Game.as:301-304`) — music louder lower down.
- HUD: if `classCount(Player)>0 && gameScore>0`, draw `"SCORE: n"` at camera top-left, color `0x8D420D` (`Game.as:119-124`).
- Enemy spawning: see §6.

### 2e. Player death → Game-over sequence
Player removed from world (only when it dies in/over water, see §3 death). Now `classCount(UBoot)==0 && classCount(Player)==0`, so `Game.update` runs the game-over timeline driven by `overdelay` (increments +1/frame from 0) (`Game.as:210-300`):

| `overdelay` | Behavior |
|---|---|
| == 31 | Remove any `Enemy` with `health<=0` from world (`Game.as:265-278`) |
| > 30 | Grow the black letterbox bars: `overx += 40` until ≥400; once `overx > halfWidth+20`, `overy *= 1.2` per frame until ≥400. `overx` starts 0, `overy` starts 4 (`Game.as:52-54,279-286`). Bar drawn as filled rect centered on screen, color `0x8D420D` (`Game.as:127`). |
| > 90 (once, at frame it first exceeds) | `sprSkull.frame 0→1`; remove `space`, `water`, all `Cloud`s; build `gameOverText` (see below) (`Game.as:240-263`). |
| > 130 | `skully *= 0.9` per frame (skull image slides up toward final position; `skully` init `halfHeight-40 = 120`) (`Game.as:236-239`). Also `X` now fast-forwards the text: `textIndex = gameOverText.length` (`Game.as:295-298`). |
| > 200 | `textIndex += 1` per frame → typewriter reveal of `gameOverText` via `substr(0,textIndex)` (`Game.as:137,234`). Pressing `UP` opens the Mochi leaderboard (dead online — skip in port) (`Game.as:215-233`). |

Skull drawn at `(camera.x+halfWidth, camera.y + skully + 40)` (`Game.as:145`). Game-over text drawn centered, color `0xFFD8AC` (16768428), box `(camera.x+halfWidth, camera.y+90, width-20, height-90)` (`Game.as:137-144`). During game-over, any `Enemy` with `health>0` still renders (frozen — Game stops calling world logic? No: `super.update()` still runs so enemies still `update()`; but their AI targets `player` which is null → they fly to furthest enemy). *Inferring: intended as frozen tableau; in practice they drift.*

The camera during game-over is **not** actively reppositioned (Player gone, UBoot gone) — it stays where the Player's follow left it, so the letterbox/skull/credits are drawn relative to that last camera position. There is no explicit "zoom"; the black bars closing in over ~10 frames read as the cinematic pinch.

`gameOverText` content (`Game.as:253-262`):
```
KILLS: <sum of gameKills>
PLANES: <gameKills[0]>x10
JETS: <gameKills[1]>x20
BOAT: <gameKills[2]>x40
SHIP: <gameKills[3]>x100

<if gameScore > highscore:>  NEW HIGH SCORE: <score>\n\nX TO RESTART | UP TO SUBMIT
<else:>                      SCORE: <score>\nHIGH SCORE: <hi>\n\nX TO RESTART | UP TO SUBMIT
```
High score saved here if beaten: `DataHandler.setHighScore(gameScore)` (`Game.as:254-256`).

### 2f. Restart (`Game.as:288-299`)
Press `X` when `textIndex >= gameOverText.length` (text fully shown) and `gameOverText.length>0`:
`bgMusic.stop(); FP.world = new Game(false, FP.volume)` — full world reset, back to attract mode.

---

## 3. Player (`Interaction/Player.as`)

### 3a. Fields / init
| Field | Init | Source |
|---|---|---|
| `health` | 10 (Number) | `Player.as:56` |
| `turn` | 10 (deg/frame) | `Player.as:58` |
| `can_shoot` | 0 (cooldown counter, frames) | `Player.as:60` |
| `shake` | 0 | `Player.as:62` |
| `isBoosting` | false | `Player.as:64` |
| `justSpawned` | true | `Player.as:66` |
| `stubbornCounter` / `daredevilCounter` | static, 0 | `Player.as:20-22` |

Constructor `Player(px,py)` (`Player.as:76-102`):
- `x=px, y=py`; play spawn sound; `vspeed = -6` (launches upward).
- `sprPlayerBody.angle = sprPlayerWings.angle = 90` (facing up).
- Boost sprites: `sprBoostStart` anim "Idle" frames [0,1,2,3,4] @ 0.3, non-loop; `sprBoost` "Idle" [0,1] @ 0.3 loop. Boost graphics offset: after `centerOO`, `x -= 12`, `originX = 24`, `x -= 4`, `originY = 8` (i.e. drawn behind the ship) (`Player.as:85-97`).
- `setHitbox(4,4,2,2)` → 4×4 hitbox, origin (2,2) (centered) (`Player.as:100`).
- `type = "Player"`.
- Body/wings PreRotation: 360 steps (1° visual resolution). Physics uses `sprPlayerBody.angle` directly (unquantized).

### 3b. "Just spawned" state (`Player.as:126-183`) — runs instead of normal update, returns early
Physics constants **not yet set** (`gravity`,`friction`,`speed` still 0). Ship coasts on `vspeed`.
- On `UP`/`W` **pressed**: `turn=10; gravity=0.2; friction=0.1; speed=6; justSpawned=false` → enter normal flight (`Player.as:128-135`).
- While `UP`/`W` **not held**: `turn=10; vspeed=-2` (holds a slow rise) (`Player.as:136-140`).
- `LEFT`/`A` held: body & wings `angle += 5`/frame; `RIGHT`/`D`: `angle -= 5`/frame (`Player.as:141-150`). (Note: 5°/frame here, not `turn`.)
- Camera: `camera.x = x - halfWidth + hspeed*10`, `camera.y = y - halfHeight` (`Player.as:153-154`).
- If `y < space.y + 248` (near ceiling; space.y = −240 → y < 8): `turn=10; gravity=0.2; friction=0.1; body/wings angle = 270` (point straight down); `justSpawned=false` (`Player.as:155-163`).
- Shooting allowed during spawn (same as normal, `X`/`SPACE`, cooldown 3, speed 16). See §4.
- `super.update()` then `return`.

### 3c. Normal flight update (`Player.as:184-378`), per frame, in order:

1. Achievements (Mochi — skip in port): score>250 "pilot", score>3500 "luftrauser", `stubbornCounter>=5`, `daredevilCounter>=25`. `stubbornCounter` reset to 0 while `X` held; `daredevilCounter` reset to 0 while `X` NOT held (`Player.as:184-217`). (These counters increment on every enemy kill, §5. "Daredevil" = 25 kills without releasing fire; "stubborn" = 5 kills without firing.)
2. **Boost / thrust** (`Player.as:218-239`): if `health>0 && (UP or W)`:
   - on rising edge: play boost sound, play `sprBoostStart` "Idle", `isBoosting=true`.
   - every frame held: `motionAdd(sprPlayerBody.angle, 1.5)` — add 1.5 units of velocity along facing.
   - else: stop boost sound; if was boosting, play `sndBoostLeave`; `isBoosting=false`.
3. **Steering** (`Player.as:240-247`): `LEFT`/`A` → `sprPlayerBody.angle += turn`; `RIGHT`/`D` → `-= turn`. (`turn` = 10 normally, 5 while boosting — set at step 6.)
4. **Shooting** (`Player.as:248-264`): if `can_shoot<=0 && X` held: play shoot sound; spawn `Bullet(x,y)`; `bullet.motionAdd(sprPlayerBody.angle, 16)`; then advance bullet one step (`x += hspeed; y += vspeed`); `can_shoot = 3`. Else `--can_shoot`. → fire every 4th frame while held (30fps → 7.5 shots/s). Note: normal state checks only `X` (not SPACE); spawn state checks `X` or `SPACE`.
5. **Speed clamp** (`Player.as:265-272`): `if(speed<2) speed=2; if(speed>6) speed=6`. Because `set speed` re-projects onto `direction`, and `direction` is set to facing at step 7, this effectively **forces the velocity to always point where the ship faces, magnitude clamped to [2,6]**. This is the signature "the plane flies where its nose points" feel.
6. `turn = isBoosting ? 5 : 10` (`Player.as:273-280`).
7. `direction = sprPlayerBody.angle` (`Player.as:281`). (Combined with step 5's clamp next frame and `super.update()` friction this frame.)
8. **Underwater** `y > water.y` (640): `health -= 0.2`/frame; `vspeed -= 2`/frame (strong buoyancy push up) (`Player.as:282-286`).
9. **Ceiling** `y < space.y + 248` (y < 8): `vspeed += 2`/frame (pushed down) (`Player.as:287-290`).
10. **Water proximity splash** (`Player.as:291-304`): if vertical distance to water `< 60`: spawn `WaterSplash(x, water.y, dist/20)` where the 3rd arg (0/1/2) selects splash sub-animation by how close; `scaleX = +1` if `hspeed>0` else `-1` (mirror). *Note `_loc5_` logic: `if hspeed>0: _loc5_=1 else _loc5_--` starting from 0 → -1; so scaleX is 1 or -1.*
11. **Camera follow** (`Player.as:305-313`):
    - `angleXY(lead, sprPlayerBody.angle, speed*6)` — look-ahead point `speed*6` px along facing.
    - `tx = x - halfWidth + lead.x`, `ty = y - halfHeight + lead.y`.
    - `camera.x = tx + (tx - camera.x)*0.05 + FP.rand(shake) - shake/2`
    - `camera.y = ty + (ty - camera.y)*0.05 + FP.rand(shake) - shake/2`
    - *Inferring:* this is `camera = 1.05*target - 0.05*oldCamera + noise` — a slight lead/overshoot, essentially a stiff follow with tiny anticipation, plus uniform random shake in `[-shake/2, +shake/2]` on each axis.
12. **Damage smoke / repair** (`Player.as:314-326`): if `0 < health < 10`:
    - if `FP.rand(10) > health`: spawn `Smoke(x,y)` with `motionAdd(rand(360), random*((10-health)/4))` — more smoke at lower health.
    - if NOT firing (`!X && !SPACE`): `health += 0.05`/frame (regen while not shooting).
13. **Death spiral** `health <= 0` (`Player.as:327-361`): stop boost sound; every frame spawn a `Smoke` (`motionAdd(rand(360), random*2.5)`); `rand(30)<1` → `Explosion`; `rand(25)<1` → up to 5 `PlayerPart` (cap 15 in world); `rand(10)<1` → `SmallExplosion`. **If `y > water.y`: spawn `LargeExplosion`, `FP.world.remove(this)`** — this is the only place the Player entity is removed, which triggers the game-over sequence. So a dead player keeps flying/smoking until it reaches the water.
14. **Shake decay** (`Player.as:362-370`): `shake *= 0.9`; then `if(shake>0) shake -= 0.1 else shake = 0`.
15. Wings: `sprPlayerWings.angle = body.angle`; `sprPlayerWings.scaleY = sin(RAD * body.angle)` (wings squash as ship rotates — pseudo-3D roll). Boost sprites' angle = body angle; `sprBoost.update()`, `sprBoostStart.update()`.
16. `super.update()` → moveBy, `vspeed += gravity (0.2)`, `speed = approach(speed, 0, friction 0.1)`.

### 3d. `getDamage(amount)` (`Player.as:380-394`)
`health -= amount; shake += amount*4;` if `health < 0`: spawn 5 `PlayerPart` (no cap check here).
Callers: `EBullet` hit → `getDamage(4)`; `Jet` body collision → `getDamage(4)`; `Brit` body collision → `getDamage(3)`.
Direct `health -=` (not via getDamage, no shake): underwater 0.2/frame; `Boot`/`Bootje` body contact → `player.health -= 2` + knockback `motionAdd(angleAwayFromBoat, 2)`.

### 3e. Death conditions summary
- health reaches ≤0 from: enemy bullets (4 each), Jet/Brit ramming (4/3), Boot/Bootje ramming (2/contact), sustained underwater (0.2/frame).
- Player entity only removed (→ game over) when a **dead** player's `y > water.y` (crashes into sea). A dead player over land keeps smoking indefinitely (edge case — flag: if a dead player never descends to water, game never ends. In practice gravity 0.2 pulls it down.)
- Ramming an enemy also damages/kills the enemy (Jet/Brit set own health 0; Boot/Bootje take 2).

---

## 4. Weapon (player Bullet — `Interaction/Bullet.as`)

- Fire input: `X` (normal), `X` or `SPACE` (spawn state). Cooldown `can_shoot`: set to **3** after firing, decremented 1/frame when >0, can fire when `<=0` → **one shot per 4 frames** (7.5/s at 30fps).
- Spawn: `new Bullet(player.x, player.y)`, `motionAdd(body.angle, 16)` → speed 16 along facing; then immediately advanced one step (`x += hspeed; y += vspeed`) so it starts 16px ahead (`Player.as:169-173, 253-257`).
- **No spread, no recoil, no player knockback.** Purely deterministic straight shot along nose.
- Bullet (`Bullet.as`): hitbox `setHitbox(16,16,8,8)` (16×16 centered), `type="Bullet"`, `layer=2000`. Sprite `Image`, `scale` starts 1.5 then forced to 1 on first update (one-frame "pop"). `Alarm(60, removeThis)` → lives 60 frames (2 s), then spawns `BulletHit` and removes.
- No gravity/friction set on Bullet → travels straight forever until timeout / hit / water.
- Per update (`Bullet.as:42-62`): `collide("Enemy", x, y)` → if hit: play `Bullet_clSnd` (pan by `scaleClamp(x - player.x, -300,300, -0.25,0.25)`), spawn `BulletHit(x,y)`, remove self, `enemy.health -= 2`.
- If `y > water.y`: spawn `WaterSplash(x + rand(16)-8, water.y)`, remove self.
- One bullet hits one enemy (first in type list) per frame.

---

## 5. Enemies

Shared: extend `Enemy` (`Enemy.as`): `health:int`. `Enemy.removed()` → if a Player exists, `gameHard += 0.3` (every enemy leaving the world ramps difficulty) (`Enemy.as:17-24`). Score/kills are added in each subclass's `removed()` override, guarded by `classCount(Player)>0`.
Body-rotation sprites are `PreRotation(...,90,...)` → **4° visual quantization** for air units (Jet, Brit). Physics angle exact.
All bullets: `EBullet` (`EBullet.as`) — Spritemap 12×12 anim [0,1]@0.5 loop, hitbox `setHitbox(8,8,4,4)`, `type="EnemyBullet"`, `layer=2000`, `Alarm(120, removeThis)` (lives 120 f = 4 s). On `collide("Player")`: spawn BulletHit, remove, play `EBullet_clSnd` (pan), `player.getDamage(4)`. On `y > water.y`: WaterSplash + remove. No gravity/friction → straight line.

### 5a. Jet (`Interaction/Enemies/Jet.as`) — AIR, fast interceptor
| Prop | Value | Source |
|---|---|---|
| health | 3 | `Jet.as:48` |
| `myspeed` | `14 + FP.rand(4)` (14–17) | `Jet.as:49,79,92` |
| `speed` | set to `myspeed` every frame | `Jet.as:79` |
| gravity | 0.02 | `Jet.as:51` |
| friction | 0.1 | `Jet.as:52` |
| hitbox | 16×16 centered (`setHitbox(16,16,8,8)`) | `Jet.as:55` |
| score | +20, `gameKills[1]++` | `Jet.as:160-166` |
| shoot alarm | `Alarm(30 + rand(300), shootBullet)`, re-armed same each shot | `Jet.as:57,152` |

Movement (`Jet.as:67-121`), per frame:
- Cache `player`. If `health<=0` → `Die()`.
- Spawn a `JetTrail(x,y)` every frame.
- `speed = myspeed` (constant cruise, re-projected onto `direction`).
- `sprEnemyBody.angle = direction`.
- If player alive (`player.health>0`): `motionAdd( FP.angle(x,y, player.x + rand(10)-5, player.y + rand(20)-10), 0.2 )` — gentle 0.2 steering toward jittered player position. If `distanceFrom(player) > 900`: reroll `myspeed`, and hard-set `direction = FP.angle(x,y,player.x,player.y)` (snap toward player when far).
- If player dead/absent: reroll `myspeed`; target = `FP.world.furthestFromEntity("Enemy", this, false)`; hard-set `direction` toward it; `motionAdd` toward jittered target by 0.2.
- Wings: `angle = body.angle`, `scaleY = sin(RAD*body.angle)`.
- Altitude guards: `y > water.y-100` → `vspeed -= 0.3`; `y > water.y-40` → `vspeed -= 0.7` (additional); `y > water.y` → `Die()` (dies on touching sea); `y < space.y+200` (y < -40) → `vspeed += 2`.
- `collide("Player")` → `health = 0; player.getDamage(4)`.
- `super.update()`.

`shootBullet` (`Jet.as:150-156`): re-arm `Alarm(30+rand(300))`; spawn `EBullet(x,y)`, `motionAdd(sprEnemyBody.angle, 8)` — **fires straight ahead along its own facing, speed 8** (not aimed — relies on the jet pointing at you). No sound played on Jet fire (only on death, `Jet_clSnd`).

`Die()` (`Jet.as:123-148`): if Player exists play `Jet_clSnd` (pan). Remove self; spawn `SmallExplosion`; `Player.stubbornCounter++`, `daredevilCounter++`; if `player.health>=0` spawn `TextBlurb(x,y,"+20")`; spawn up to 5 `JetPart` (world cap 15), each `motionAdd(direction, speed/2 + 1)`.
`removed()` adds +20 score & kill (only if Player alive).

### 5b. Brit (`Interaction/Enemies/Brit.as`) — AIR, basic "plane", the staple enemy
| Prop | Value | Source |
|---|---|---|
| health | 4 | `Brit.as:52` |
| `maxspeed` | 6 init; retuned by AI to `4 + rand(2)` (+2 if far, +2 more from perform) | `Brit.as:22,176-180` |
| `minspeed` | 2 init; `2 + rand(2)` when far | `Brit.as:23,180` |
| `reaction` (turn rate toward player) | `0.3 + random*0.3` init; retuned `0.2 + random*0.3` | `Brit.as:54,94,191` |
| `turn` (extra spin) | 0; toggled to `rand(6)-3` / 0 by AI when far | `Brit.as:101,182-189` |
| gravity | 0.02 | `Brit.as:56` |
| friction | 0.1 | `Brit.as:57` |
| hitbox | 16×16 centered | `Brit.as:59` |
| score | +10, `gameKills[0]++` | `Brit.as:200-207` |
| shoot alarm | `Alarm(30 + rand(300), shootBullet)` | `Brit.as:61,163` |
| AI alarm | `Alarm(rand(60)+1, performIntelligence)` | `Brit.as:62` |

Movement (`Brit.as:72-126`) per frame:
- Cache player; `health<=0` → `Die()`.
- Clamp `speed` to `[minspeed, maxspeed]` (re-projected onto `direction`).
- `sprEnemyBody.angle = direction`.
- If player alive: `motionAdd( FP.angle(x,y,player.x,player.y), reaction )` — steer toward player (exact aim, no jitter). Else target furthest enemy: hard-set `direction` toward it.
- `direction += turn` (constant spin component when AI set it).
- Wings angle/scaleY like Jet.
- Altitude: `y > water.y-100` → `vspeed -= 0.1`; `y > water.y-40` → `vspeed -= 0.2`; `y > water.y` → `Die()`; `y < space.y+200` → `vspeed += 2`.
- `collide("Player")` → `health=0; player.getDamage(3)`.

`performIntelligence` (`Brit.as:171-198`): if player exists: `maxspeed = 4 + rand(2)`; if `distanceFrom(player) > 200`: `maxspeed += 2`, `minspeed = 2 + rand(2)`, re-arm `Alarm(60 + rand(120), performIntelligence)`, toggle `turn` between `rand(6)-3` and 0. `reaction = 0.2 + random*0.3`. (If within 200 px, the alarm is NOT re-armed — *inferring bug/feature*: once a Brit closes to <200 it stops re-scheduling its AI, locking its current speed band and flying straight at you with only `reaction` steering.) If no player: aim at furthest enemy.

`shootBullet` (`Brit.as:155-169`): only if `player && player.health>0`: play `Brit_clSnd2` (pan −1..1), re-arm `Alarm(30+rand(300))`, spawn `EBullet(x,y)` `motionAdd(sprEnemyBody.angle, 8)` — straight ahead, speed 8.

`Die()` (`Brit.as:128-153`): play `Brit_clSnd` (pan) if Player exists; remove; `SmallExplosion`; counters++; `TextBlurb "+10"` if `player.health>=0`; up to 5 `BritPart` (cap 15), `motionAdd(direction, speed/2+1)`.

### 5c. Boot (`Interaction/Enemies/Boot.as`) — SEA, the battleship ("SHIP")
| Prop | Value | Source |
|---|---|---|
| health | 60 | `Boot.as:53` |
| hspeed | `±(random*0.1) + 0.2` (dir by random `flipped`) | `Boot.as:44-52` |
| hitbox | `setHitbox(204,32,0,-16)` → 204 wide × 32 tall, originX 0, originY −16 (hitbox extends above the anchor) | `Boot.as:57` |
| layer | 2000 | `Boot.as:58` |
| score | +100, `gameKills[3]++` | `Boot.as:181-188` |
| fire alarm | `Alarm(90, fireCannons)` initial | `Boot.as:56` |

Behavior (`Boot.as:67-137`):
- Sits on the water: while `health>0`, forced `y = water.y - sprShip.height` every frame (`Boot.as:75`). (`Boot_clShip` image; height from asset.)
- Body contact with Player (`collide("Player")` while alive): `player.health -= 2`; `player.motionAdd(angle away from ship, 2)` knockback; `health -= 2`; if that kills it, unlock Mochi achievement.
- On death (`health<=0`, first frame `collidable` still true): play `Boot_clSnd` (pan); `type=""` (stops colliding as enemy); spawn 3 `LargeExplosion` at `x+rand(60), y+rand(20)`; `TextBlurb "+100"` if `player.health>=0`; counters++; `collidable=false`.
- Dead sink: `hspeed=0`; when `y > water.y - shipHeight + 60` → `FP.world.remove(this)` (sinks 60px then gone); random ongoing explosions: `rand(20)<1` → `switch(rand(9))` case 8 `LargeExplosion` else `Explosion` at `x+50+rand(100), y+rand(40)`; `if(vspeed < 0.2) vspeed += 0.01` (slow sink accel).
- **Screen wrap** (always): `if x < camera.x-500 → x = camera.x + width + 400`; `if x > camera.x + width + 500 → x = camera.x - 400` (`Boot.as:128-135`).

`fireCannons` (`Boot.as:139-157`): re-arm `Alarm(120 + rand(120), fireCannons)`. If `!fire` and player exists: also `Alarm(90, fireCannons)`, `fire=true`, snapshot `firex/firey = player.x/y`, start `Alarm(2, fireSecondary)`. If `fire` already: `fire=false` (end of burst).
`fireSecondary` (`Boot.as:159-179`): while `fire && player.health>0 && health>0`: play `Boot_clSnd2` (pan); re-arm `Alarm(5, fireSecondary)`; spawn `EBullet(x + shipWidth*0.75, y + 24)` aimed `FP.angle(x + shipWidth/2, y, firex, firey)` speed **8** — a burst of bullets every 5 frames toward the snapshotted player position (leads slightly stale). Burst length bounded by the `fire` toggle in `fireCannons`.

### 5d. Bootje (`Interaction/Enemies/Bootje.as`) — SEA, the small boat ("BOAT")
| Prop | Value | Source |
|---|---|---|
| health | 20 | `Bootje.as:49` |
| hspeed | `±(random*0.1) + 0.2` | `Bootje.as:40-48` |
| `ammo` | 3 (burst size) | `Bootje.as:23,140,157` |
| hitbox | `setHitbox(48,20,0,-12)` → 48×20, originX 0, originY −12 | `Bootje.as:52` |
| layer | 2000 | `Bootje.as:53` |
| score | +40, `gameKills[2]++` | `Bootje.as:164-171` |
| fire alarm | `Alarm(40 + rand(40), fireCannons)` initial | `Bootje.as:50` |

Behavior mirrors Boot (`Bootje.as:61-131`): forced `y = water.y - shipHeight` while alive; body contact → `player.health -= 2` + knockback `motionAdd(away, 2)` + `health -= 2`; on death spawn 3 `Explosion` (not Large), `TextBlurb "+40"`, counters++, `type=""`, `collidable=false`; dead sink identical (`> water.y - h + 60` remove; `rand(20)<1` → `rand(9)==8 ? Explosion : SmallExplosion` at `x+10+rand(20), y+rand(32)`; `vspeed += 0.01` until 0.2). Screen-wrap identical to Boot.

`fireCannons` (`Bootje.as:133-162`): if `health>0 && player`: if `ammo>0`: `Alarm(10, fireCannons)`, `--ammo`; if `player.health>0 && distanceFrom(player) < 600`: play `Bootje_clSnd2` (pan); spawn `EBullet(x, y+16)` aimed `FP.angle(x,y, player.x, player.y)` speed **6** (aimed, direct). Else (ammo depleted): `ammo = 3`, `Alarm(100 + rand(100), fireCannons)`. → bursts of 3 aimed shots 10 frames apart, then a 100–200 frame reload.

### 5e. Air vs Sea
- **Air:** Jet, Brit — free-flying, killed on touching water, pushed away from ceiling.
- **Sea:** Boot, Bootje — locked to water surface, drift horizontally, screen-wrap around the camera, sink on death.
- `UBoot` (submarine) is the intro launcher, not a combat enemy (no `type="Enemy"`; `Interaction/UBoot.as`).

---

## 6. Spawning & difficulty (`Game.as`)

### 6a. Kickoff
`UBoot.removeThis` → `Game.spawnEnemies()` (`Game.as:372-377`): add 2 `Brit` (x = `FP.choose(camera.x-500, camera.x+width+500)`, y = `100 + rand(500)`), then `Alarm(30 + rand(60), spawnMoreEnemies)`.

### 6b. `spawnMoreEnemies()` — the loop (`Game.as:308-365`)
Runs only if `player != null || classCount(Player) > 0`. Each invocation:
1. Re-arm: `Alarm(30 + FP.rand(60), spawnMoreEnemies)` → next spawn tick in **30–89 frames** (~1–3 s).
2. `spawnX = FP.choose(camera.x - 500, camera.x + width + 500)` (off left or right edge). `spawnY = 100 + rand(500)` (used only by air units).
3. `gameHard += 0.1` every tick.
4. Spawn gate: only if `typeCount("Enemy") < gameHard  &&  typeCount("Enemy") < 100`. (So the live enemy count is capped by `gameHard` itself, and hard-capped at 100.)
5. Choose enemy class `_loc3_` by `gameHard`:
   - `gameHard < 3`: `0` (Brit only).
   - `gameHard < 7`: `FP.choose(0,0,1)` → 2/3 Brit, 1/3 Bootje.
   - `gameHard < 15`: `FP.choose(0,1,2)` → 1/3 each Brit / Bootje / Jet.
   - else: `FP.choose(0,0,1,1,2,2,3)` → Brit 2/7, Bootje 2/7, Jet 2/7, Boot 1/7.
6. Spawn:
   - case 0 (**Brit**): loop `_loc4_` from 0 while `_loc4_ <= gameHard/5 + 1` → add `Brit(spawnX, spawnY)`. Count = `floor(gameHard/5) + 2` Brits per tick (integer loop; `_loc4_` int). *Inferring: at gameHard 10 → 4 Brits, at 30 → 8.*
   - case 1 (**Bootje**): add one `Bootje(spawnX)`.
   - case 2 (**Jet**): loop while `_loc4_ <= gameHard/15 + 1` → add `Jet(spawnX + rand(100)-50, spawnY + rand(100)-50)`. Count = `floor(gameHard/15) + 2` jets.
   - case 3 (**Boot**): add one `Boot(spawnX)`.

### 6c. `gameHard` (difficulty scalar), `Game.as:38`
- `+0.1` per spawn tick (`Game.as:319`).
- `+0.3` per enemy removed while player alive (`Enemy.removed`, `Enemy.as:22`).
- `+0.3` again per Jet/Brit/... — no: that `+0.3` in `Enemy.removed` is the only per-death bump; subclass `removed()` calls `super.removed()`.
- Never decreases. Drives both the max concurrent enemy count (`typeCount < gameHard`, capped 100) and the per-tick swarm sizes and the class mix. **Killing enemies accelerates difficulty faster than time does** (0.3 vs 0.1).
- No waves, no explicit caps other than `typeCount("Enemy") < 100` and `PlayerPart`/`JetPart`/`BritPart` world caps of 15.

### 6d. Alarm/timer constants inventory
| Timer | Frames | Where |
|---|---|---|
| UBoot rise → ready | 96 | `UBoot.as:38` |
| launch → sub dive | 48 | `UBoot.as:72` |
| dive → sub remove (+spawnEnemies) | 48 | `UBoot.as:93` |
| `aware` clear | 300 | `Game.as:83` |
| first spawnMoreEnemies | 30 + rand(60) | `Game.as:376` |
| spawnMoreEnemies re-arm | 30 + rand(60) | `Game.as:316` |
| Jet/Brit shoot | 30 + rand(300) | `Jet.as:57,152` / `Brit.as:61,163` |
| Brit performIntelligence | rand(60)+1, then 60 + rand(120) | `Brit.as:62,181` |
| Boot fireCannons | 90 init; 120 + rand(120); +90; +2 (→fireSecondary); 5 (secondary loop) | `Boot.as:56,141,146,150,171` |
| Bootje fireCannons | 40 + rand(40) init; 10 (per shot); 100 + rand(100) reload | `Bootje.as:50,142,158` |
| Bullet lifetime | 60 | `Bullet.as:34` |
| EBullet lifetime | 120 | `EBullet.as:35` |
| TextBlurb lifetime | 60 | `TextBlurb.as:26` |
| Part (Player/Jet/Brit) lifetime | rand(300)+1 | `*Part.as:26/27` |
| Game-over: bar grow start / skull / text FF / typewriter | overdelay 30 / 90 / 130 / 200 | `Game.as` |

---

## 7. Scoring (`Game.as`, enemy `removed()`)

- `gameScore:int` starts 0 (`Game.as:34`). `gameKills:Array = [0,0,0,0]` = [Brit/planes, Jet/jets, Bootje/boat, Boot/ship] (`Game.as:36`).
- **Per kill only** (no combo, no multiplier, no survival/time score):
  - Brit: +10, `gameKills[0]++` (`Brit.as:204-205`).
  - Jet: +20, `gameKills[1]++` (`Jet.as:162-163`).
  - Bootje: +40, `gameKills[2]++` (`Bootje.as:168-169`).
  - Boot: +100, `gameKills[3]++` (`Boot.as:185-186`).
  - All guarded by `classCount(Player) > 0` — kills after the player entity is removed don't score.
- `TextBlurb` shows "+10/+20/+40/+100" at the kill location, rises 1px/frame for 60 frames, color `0x610C1D` (`TextBlurb.as`). Only spawned if `player.health >= 0` (i.e. not while player is in death spiral with health <0... actually `>=0` so exactly 0 still shows).
- High score: `DataHandler` uses `SharedObject "Luftrauser"`, key `"Highscore"` (`DataHandler.as`). Read in attract screen ("BEST:") and at game-over; written at game-over only if `gameScore > stored` (`Game.as:254-256`). Port: persist a single int.
- Achievements (Mochi) — not portable, ignore: pilot (score>250), luftrauser (score>3500), stubborn (5 kills w/o firing), daredevil (25 kills holding fire), sink-a-ship (kill Boot/Bootje by ramming).

---

## 8. World

| Element | Value | Source |
|---|---|---|
| `Water.y` | **640** (constant) | `Water.as:18` |
| Water graphic | `TiledImage` 800×240, layer 1000; follows player: `x = player.x - 400` | `Water.as:13,20,32` |
| `Space.y` | **−240** (`y = -sprSpace.height`, height 240) | `Space.as:18` |
| Space graphic | `TiledImage` 800×240, layer 2000; `x = player.x - 400` | `Space.as:13,18,32` |
| Ceiling trigger (player) | `y < space.y + 248` → `y < 8`; push `vspeed += 2` | `Player.as:287` |
| Ceiling trigger (Jet/Brit) | `y < space.y + 200` → `y < -40`; `vspeed += 2` | `Jet.as:111`, `Brit.as:116` |
| Underwater trigger (player) | `y > 640`: `health -= 0.2/f`, `vspeed -= 2/f` | `Player.as:282` |
| Air-unit water death | `y > 640` → `Die()` | `Jet.as:107`, `Brit.as:112` |
| Sea units | pinned to `y = 640 - shipHeight` while alive | `Boot.as:75`, `Bootje.as:69` |

- The playfield is horizontally infinite (camera follows player; water/space/clouds/sea-units all repeat/wrap relative to camera). Vertically bounded by soft pushes at y≈8 (top) and y=640 (water).
- **Water splash mechanics:** `WaterSplash(x, y, type=0)` (`FX/WaterSplash.as`): Spritemap 16×16; `y` clamped so sprite bottom sits at water line; anim frames by `type`: 0→[0,1,2], 1→[1,2], 2→[2] @ 0.3, non-looping, self-removes on complete. `BigWaterSplash` 34×63 frames [0,1,2]@0.3. Splashes spawned by: player within 60px of water (type = dist/20), any Bullet/EBullet/Explosion/SmallExplosion crossing water (`rand(8)<1` for explosions), any Part hitting water, LargeExplosion over water → BigWaterSplash.
- **Killing the player near water:** a dead player (health≤0) keeps smoking/exploding until `y > water.y`, at which point it spawns one `LargeExplosion` and is removed → game-over sequence begins (`Player.as:356-360`). Dying exactly at/over water ends the run almost immediately; dying high up gives a long smoking descent first.

---

## 9. Audio triggers

Sounds are FlashPunk `SfxEmbed` (mp3). Pan = `FP.scaleClamp(sourceX - player.x, -300, 300, -0.25, 0.25)` (or −1..1 for `Brit_clSnd`). Volume arg = `FP.volume`.

| Event | Class / embed file | Source |
|---|---|---|
| Player spawn (launch) | `Player_clSndSpawn` (`6_...SndSpawn.mp3`) | `Player.as:81` |
| Player shoot | `Player_clSndShoot` (`8_...SndShoot.mp3`) | `Player.as:168,252` |
| Boost start / while boosting | `Player_clSndBoost` (`9_...SndBoost.mp3`) — played on rising edge, `stop()` when boost ends / health≤0 | `Player.as:222,230-232,329` |
| Boost release | `Player_clSndBoostLeave` (`7_...SndBoostLeave.mp3`) | `Player.as:236` |
| Player bullet hits enemy | `Bullet_clSnd` (`17_...Bullet_clSnd.mp3`) | `Bullet.as:51` |
| Enemy bullet hits player | `EBullet_clSnd` (`13_...EBullet_clSnd.mp3`) | `EBullet.as:50` |
| Jet death | `Jet_clSnd` (`12_...Jet_clSnd.mp3`) | `Jet.as:128` |
| Brit death | `Brit_clSnd` (`10_...Brit_clSnd.mp3`), pan −1..1 | `Brit.as:133` |
| Brit shoot | `Brit_clSnd2` (`11_...Brit_clSnd2.mp3`) | `Brit.as:162` |
| Boot (ship) death | `Boot_clSnd` (`2_...Boot_clSnd.mp3`) | `Boot.as:93` |
| Boot cannon fire | `Boot_clSnd2` (`3_...Boot_clSnd2.mp3`) | `Boot.as:170` |
| Bootje (boat) death | `Bootje_clSnd` (`4_...Bootje_clSnd.mp3`) | `Bootje.as:87` |
| Bootje cannon fire | `Bootje_clSnd2` (`5_...Bootje_clSnd2.mp3`) | `Bootje.as:148` |
| Explosion (32px) | `Explosion_clSnd` (`15_...`) — on spawn | `Explosion.as:27` |
| LargeExplosion (64px) | `LargeExplosion_clSnd` (`14_...`) — on spawn | `LargeExplosion.as:27` |
| SmallExplosion (16px) | `SmallExplosion_clSnd` (`16_...`) — on spawn | `SmallExplosion.as:27` |
| Music (loop) | `Game_clMusic` (`1_...Music.mp3`), `bgMusic.loop(FP.volume/2)` on launch; `volume = scaleClamp(player.y,100,600,0.5,0.75)`; `stop()` on restart | `Game.as:367,303,292` |

Build manifest (`build/luftrauser/assets.manifest.json`) collapses SFX to `audio/1` (0.25s) & `audio/2` (0.4s) plus `audio/music_loop` — the pipeline dedupes; map every SFX event above to the appropriate deduped clip when wiring audio, or restore per-event clips if assets allow.

---

## 10. FX inventory (brief)

| FX | Spawned by | Behavior |
|---|---|---|
| `Explosion` (`Explosion.as`) | Player death (`rand(30)<1`), Bootje death (×3), Boot dead debris | 32×32 spritemap, 10 frames @0.5, self-removes on complete; hitbox 32×32 damages `Enemy` −1 health/frame; over water `rand(8)<1` spawns WaterSplash. |
| `LargeExplosion` (`LargeExplosion.as`) | Player crash into water, Boot death (×3), PlayerPart timeout, Boot debris | 64×64, 14 frames @0.5; hitbox 64×64, −1 enemy health/frame; over water → BigWaterSplash `rand(8)<1`. |
| `SmallExplosion` (`SmallExplosion.as`) | Jet/Brit death, Player death (`rand(10)<1`), JetPart/BritPart timeout, PlayerPart (`rand(60)<1`), Bootje debris | 16×16, 7 frames @0.5; no damage; over water WaterSplash `rand(8)<1`. |
| `BulletHit` (`FX/BulletHit.as`) | Bullet/EBullet on hit, timeout, wall | 16×16, 4 frames @0.5, self-removes. Cosmetic. |
| `Smoke` (`FX/Smoke.as`) | damaged/dying Player, all Parts (`rand(15)<1`) | 16×16, 3 frames @0.2; `friction=0.1`, drifts with whatever `motionAdd` gave it, self-removes on anim complete. |
| `WaterSplash` (`FX/WaterSplash.as`) | player near water, bullets/explosions/parts crossing water | 16×16, frames vary by `type` arg (0/1/2) @0.3, bottom pinned to water line, self-removes. `scaleX ±1` for direction (player splash). |
| `BigWaterSplash` (`FX/BigWaterSplash.as`) | LargeExplosion over water | 34×63, 3 frames @0.3, pinned to water, self-removes. |
| `JetTrail` (`FX/JetTrail.as`) | every Jet, every frame | 4×4, 3 frames @0.2, stationary, self-removes on complete (~15 frames). Dense contrail. |
| `PlayerPart` (`FX/PlayerPart.as`) | Player death (batches of 5, world cap 15), `getDamage` when health<0 (5) | 16×16 random frame; `motionAdd(rand(360), 2+rand(5))`, `vspeed -= rand(2)`, `gravity=0.2`; life `rand(300)+1` then spawns LargeExplosion; on hitting water → WaterSplash + remove; trails Smoke (`rand(15)<1`), SmallExplosion (`rand(60)<1`). |
| `JetPart` (`FX/JetPart.as`) | Jet death (5, cap 15) | 16×16; `motionAdd(rand(360), 1+rand(4))`, `vspeed -= rand(2)`, `gravity=0.2`; life `rand(300)+1` → SmallExplosion; water → WaterSplash; Smoke `rand(15)<1`. Also given `motionAdd(direction, speed/2+1)` by Jet at spawn. |
| `BritPart` (`FX/BritPart.as`) | Brit death (5, cap 15) | Identical to JetPart; extra `motionAdd(direction, speed/2+1)` from Brit. |
| `Cloud` (`FX/Cloud.as`) | Game ctor (×6) | 320×160 random frame, `y = 100 + rand(400)`, `hspeed = choose(-1,1)*random`, layer 4000 (background), wraps horizontally around camera ±600. Removed at game-over. |
| `TextBlurb` (`TextBlurb.as`) | enemy deaths ("+N") | Text, color `0x610C1D`, `--y` each frame, 60-frame life. |

---

## Trickiness / porting flags

- **Frame-rate-dependent physics.** Everything is per-frame at fixed 30 fps: gravity 0.2/frame, friction 0.1/frame, all `motionAdd`, all timers in frames. The C port must run its sim at a fixed 30 Hz step (decouple from PSP's 60 Hz refresh — step twice per vsync or run logic at 30). Do **not** multiply by delta-time.
- **Float determinism.** AS3 Numbers are IEEE double. Trig via `Math.sin/cos/atan2`. `FP.RAD/DEG` are negative (angle handedness). Camera follow, `speed`-clamp re-projection, and `sin(RAD*angle)` wing-squash all accumulate FP error; use `double` and match formula order. The LCG (`_seed*16807 % 2147483647`, `rand(n) = _seed/2147483647*n` floored) is trivially portable and should be reproduced exactly if you want replay/testing parity — seed is randomized at boot so gameplay itself isn't seed-locked.
- **The `speed` setter side-effect** (`set speed` re-projects velocity onto `direction`) is the heart of the flight model and is easy to miss — clamping `speed` into [2,6] every frame while `direction` tracks the nose is what makes the plane "fly where it points". Implement `speed`/`direction`/`hspeed`/`vspeed` as coupled accessors exactly like `Entity.as`.
- **`moveBy` collision sweep.** FlashPunk's `moveBy` does an integer-stepped sweep with solid-collision callbacks; none of these entities pass a `solidType`, so it reduces to `x += hspeed; y += vspeed`. Safe to simplify.
- **PreRotation quantization is visual only** — 360 steps for the player (≈continuous), 90 steps (4°) for Jet/Brit bodies. Gameplay reads the exact stored `.angle`. Don't quantize physics.
- **`justSpawned` sub-state** has its own constants (rotate 5°/frame, `vspeed=-2` hold, exits on UP-press OR reaching ceiling) and leaves `gravity/friction/speed` at 0 until exit — a distinct mode to implement.
- **Brit AI stops re-scheduling within 200px** of the player (`performIntelligence` only re-arms its alarm in the `>200` branch) — likely intentional "commit to attack run" but looks like a bug; replicate as-is.
- **Dead-player-never-lands edge case:** game-over only triggers when a health≤0 player's `y > 640`. Gravity guarantees it eventually, but if you add any hard ceiling/no-gravity path, ensure descent still happens.
- **Enemy `removed()` scoring** is on the FlashPunk "removed from world" event, not at the moment health hits 0 (sea units animate a death/sink first). Score/kill count/`gameHard += 0.3` all fire on actual removal.
- **`gameHard` both gates spawns and sizes swarms**, and rises faster from kills (0.3) than time (0.1) — aggressive play escalates difficulty. `typeCount("Enemy") < gameHard` is the live-population cap (hard max 100).
- Camera: `camera = 1.05*target - 0.05*oldCamera + uniformNoise(shake)`. The `+shake/2 .. -shake/2` term uses `FP.rand(shake)` (LCG). Shake decays `*0.9` then `-0.1`.
- Mochi / MochiServices / MochiScores / MochiEvents and the `net/` leaderboard code are dead — stub all of it.

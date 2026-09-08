# Roadmap — one-to-one, then pandoras-box

Two phases, deliberately separated.

- **one-to-one** — a feel-faithful reproduction of *Luftrauser* as decompiled.
  Nothing added, nothing "improved". Quirks and frame-rate-dependent physics are
  replicated. Every constant traces to a `file:line` in
  [`luftrauser-port-spec.md`](luftrauser-port-spec.md). This build is the
  **reference implementation and regression oracle**.
- **pandoras-box** — additive features, each behind a flag. `[features]` all-off
  in `game.toml` **is** the one-to-one build. New code plugs into seams; it never
  edits the classic path. Classic mode must never regress.

---

## Phase 1 — one-to-one build order

Grounded in [`flashpunk-api-surface.md`](flashpunk-api-surface.md) (the engine
subset is small) and the port spec (~140 constants).

| # | Deliverable | Notes |
|---|---|---|
| 1 | `runtime/` toolchain — pspdev CMake, `EBOOT.PBP` packaging, PPSSPP run target | build a black screen first |
| 2 | `pak.c` / `ptx.c` — mount `assets.pak`, upload `.ptx` to a GU texture | |
| 3 | `spec/` — extract the ~140 constants from the port spec into headers/data | zero magic numbers in the C (see below) |
| 4 | **FP core**: fixed 30 Hz accumulator (`maxFrameSkip 5`), LCG RNG (`seed*16807 % 2^31`), angle convention (`RAD = π/-180`, deg, CW, Y-down), `FP.camera` | determinism-critical; unit-test RNG + angle math against AS3 values |
| 5 | `Entity` motion model — the coupled `speed`/`direction`/`hspeed`/`vspeed` accessors; `motionAdd`; per-type AABB collision (4 types, unrotated rect overlap, ~30 lines) | the `set speed` re-projection *is* the flight feel |
| 6 | `World` — update list, per-layer render list, per-type buckets (`classCount`/`getClass`/`furthestFromEntity`) | intrusive linked lists like FP |
| 7 | Graphics: `Image` (rotated textured quad, tint, flip), `Spritemap`/`Anim` (atlas + frame-rate playback + despawn callback), `TiledImage` (Water/Space bands) | rotation is runtime; ignore `PreRotation` (dead code) |
| 8 | Glyph-atlas **bitmap font** to replace `Text` (center align, `\n`, measure) | ~7 call sites, all in `Game.render`/`TextBlurb`; new asset we author |
| 9 | `Alarm` — frame-counting countdown + callback (no easing, no other tweens) | |
| 10 | Audio: `Sfx` via `sceAudio` (PCM), looping music via `sceMp3`, binary master mute | |
| 11 | Input: 8 logical actions on the analog nub + face buttons; mute via a button (not mouse) | |
| 12 | Port game classes: `Player` → `Bullet`/`EBullet` → `Brit`/`Jet`/`Bootje`/`Boot` → `UBoot` → `Worlds/Game` (spawn/`gameHard`/scoring/game-over) → FX | one class at a time, checked against the spec |
| 13 | Stub the dead code: all `mochi/*`, `net/` online, `FP.console.log`; `SharedObject` → a file on the memstick | |
| 14 | **Fidelity harness** (below) | gates every later change |

Resolution: build `FP` at **480×272** native — not a crop (see
[`luftrauser-getting-started.md`](luftrauser-getting-started.md)).

Pipeline work that feeds Phase 1:
- **spritesheet-aware texture converter** — slice sheets by their
  `[convert.textures.sheets]` frame size, repack into a ≤512 POT atlas, emit a
  frame table. Fixes the 3 oversized sheets (`cloud`, `largeexplosion`, `skull`);
  see [`assets_readme.md`](assets_readme.md).

---

## The seams (built in Phase 1 as no-ops)

None of these add behavior in Phase 1 — they are just the *shape* of the code, so
pandoras-box items plug in instead of requiring surgery.

| Seam | Phase 1 form | What plugs in later |
|---|---|---|
| **`GameMode` interface** — `update` / spawn / win-lose / camera / player-controller | `ClassicMode` only | wave mode, Invaders mode, boss rush, mutator playlists |
| **Entity components** — `PhysicsBody`, `Weapon`, `Health`, `SpriteRig` | fixed configs from `spec/` | swap a component → new behavior |
| **`WeaponDef` struct** | exactly one (cooldown 4f, bullet speed 16) | laser / missiles / spread / homing / melee — loadout system |
| **Modifier list** — per-frame rule hooks (gravity×, time×, spawn-rate×, score rules) | empty list | gravity mutators, slow-mo, difficulty tiers, roguelite boons |
| **Event bus** — `EnemyKilled`, `PlayerHit`, `BulletFired`, `WaterSplash`, `ComboChanged`, `StateChanged` | drives SFX + score only | achievements, combos, unlocks, powerup drops — with zero entity edits |
| **Logical input actions** — `Thrust/Left/Right/Fire/Mute` | mapped to original controls | `Bomb/Dash/SwapWeapon`, remap, twin-stick |
| **Profile blob** — key→value store | just `highScore` (was `SharedObject`) | unlocks, per-mode records, settings, run stats |
| **Seeded RNG** — one `FP.rand`-compatible stream, seed exposed | internal only | daily seeded challenge, seed sharing, deterministic replays |
| **Fixed sim / free render split** | 30 Hz sim == original; render == sim | 60 fps interpolated render, slow-mo, replay export |
| **Theme / palette indirection** (from the style-transfer step) | base palette + background color | reskins, colorblind palettes, CRT/bloom shader |

---

## `spec/` — auditable fidelity

The C port contains **no gameplay magic numbers**. Every constant lives in `spec/`
as data (generated headers or a loaded table), each annotated with its
`Interaction/Player.as:NNN` origin. Benefits:

- one-to-one fidelity is reviewable — diff `spec/` against the port spec
- pandoras-box difficulty/mutator systems scale these values instead of hardcoding
- a future extractor can regenerate `spec/` straight from the AS3

Groups (counts from the port spec): engine 4 · player physics/spawn/camera/shake ~30
· weapon 4 · Jet ~12 · Brit ~16 · Boot ~14 · Bootje ~11 · UBoot ~6 ·
spawn/difficulty + alarms ~22 · scoring 5 · world geometry 8 · FX ~20.

---

## `[features]` convention

```toml
# games/luftrauser/game.toml
[features]
# every flag defaults false; all-false == the one-to-one build
combo_multiplier   = false   # decaying score multiplier (original has none)
weapon_loadout     = false
boost_iframes      = false
render_60fps       = false
crt_shader         = false
mode               = "classic"   # classic | waves | invaders | bossrush
```

Flags are read once at startup and select seam implementations. A flag may only
*add* an implementation behind a seam — never branch inside `ClassicMode` or a
game-class update.

---

## Fidelity harness (`tests/fidelity/`)

The one-to-one build's job is to be diff-able against the original.

1. **RNG + math parity** — unit-test the LCG and `angleXY`/`angle`/`set speed`
   against value tables captured from the AS3.
2. **Replay oracle** — record `(frame, input_bitmask)` sequences; feed identical
   input to (a) the original SWF via Ruffle headless and (b) our build; dump
   `(entity, x, y, hspeed, vspeed, health)` per frame; diff. Tolerance starts at
   exact (integer-seeded float paths) and loosens only where documented.
3. **Golden frames** — PPSSPP headless screenshot dumps at fixed frames of a
   canned replay; perceptual-diff.
4. CI gate: pandoras-box PRs must keep `mode=classic` replays green.

---

## Pandoras-box backlog

Each item tagged with the seam(s) it needs. Nothing here is scheduled — this is
the menu once Phase 1 + the harness exist.

### Weapons / loadout  → `WeaponDef`, components, event bus
The 2014 sequel's whole hook (body × gun × propulsion = 125 combos).
- swappable weapon: laser, spread, missiles/homing, nuke, melee ram
- overheat vs ammo vs cooldown firing models
- charge shots
- swappable hull (hp/mass) and thruster (accel/top speed) — scales `spec/` values

### Movement  → components, modifier list, input actions
- boost-dash with i-frames
- barrel roll → bullet deflect
- air-brake / hover
- grappling hook to enemies
- real submarine mode (water as playspace, not just the death plane)

### Encounters  → `GameMode`, components, event bus
- new enemies: turrets, drone carriers, kamikaze, shielded, splitters
- expand `UBoot` into a real multi-phase boss
- coordinated formation waves (shared tech with Invaders mode)
- elite/variant enemies (a modifier applied to a base enemy)

### Meta / progression  → profile blob, event bus, seeded RNG, `GameMode`
- unlock system via challenge missions (sequel model)
- roguelite run structure: 1-of-3 boon per wave (boons = modifier-list entries)
- currency + shop
- daily seeded challenge · score attack · time attack

### Scoring / risk-reward  → event bus, modifier list
- decaying combo multiplier (**original has none — clean first add**)
- "no water touch" streak bonus
- bullet-graze bonus (near-miss detection)
- style points: rolls, multi-kills, air-vs-sea variety

### Modes  → `GameMode`
- endless (== classic) · wave/campaign with objectives · **Invaders** (style-transfer
  Tier 2) · boss rush · mutator playlists (double gravity, no water, mirror,
  one-hit, bullet-hell)
- co-op / versus via PSP ad-hoc — flagged **hard, maybe never**

### Presentation / QoL  → sim-render split, theme indirection, event bus
- 60 fps interpolated render
- CRT / scanline / bloom shader
- replay record + export (reuses the oracle infra)
- difficulty settings · aim assist · slow-mo toggle
- screen-shake / flash toggles
- colorblind palettes (composes with the theme system)

---

## Candidates for "Phase 1.5" (right after one-to-one, before the backlog)

Small, high-value, low-risk, and they exercise the seams:

- **Combo multiplier** — one event-bus consumer + one HUD number. Proves the bus.
- **Input remap + nub deadzone tuning** — needed for the game to feel right on
  hardware regardless.
- **60 fps interpolated render** — big perceived-quality win, sim stays 30 Hz.
- **Pause menu + settings** — needs the profile blob and a non-`ClassicMode` state.

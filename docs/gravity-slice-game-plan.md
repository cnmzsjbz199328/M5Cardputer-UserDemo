# Gravity Slice — New App Implementation Plan

Status: planning complete, ready for implementation by another agent.
Reviewer / version control: this repo's primary agent (not the implementing agent).

## 1. What this is

A new Mooncake app for the Cardputer ADV: a tilt-driven gravity ball that
bounces off the screen edges, with fruit/bomb/power-up projectiles launched
from the bottom of the screen. Hitting fruit scores, hitting a bomb costs
score, and power-ups grant timed abilities (invincibility, spread-shot,
spiral trail, beam). There is no fail state — a run is a fixed-length timed
challenge that always ends in a results/ranking screen, mirroring
`app_racer`'s finish flow.

Multi-ball only happens as a temporary effect of the spread-shot power-up.
The baseline game is single-ball.

## 2. Prior art already reviewed (context for the implementing agent)

- **In this repo**: [`main/apps/app_racer/app_racer.cpp`](../main/apps/app_racer/app_racer.cpp)
  is the reference for (a) the Cardputer ADV IMU axis convention (see
  `update_imu()`, lines ~165-183) and (b) the overall app lifecycle / timed
  finish-state pattern (see `finish_race()`, `RaceState` enum, best-score
  persistence via `GetHAL().getSettings()`). **Reuse the IMU axis mapping
  from `update_imu()` verbatim** — do not re-derive it or port the
  QMI8658-specific mapping mentioned below; the two boards use different IMU
  chips (BMI270 vs QMI8658) and their axis conventions are not guaranteed to
  match.
- **Sibling repo (reference only, not a build input, do not link/import it)**:
  `esp32_test/boards/touch_lcd_154/demo_gravity.cpp` is a diagnostic demo for
  a *different* board (Waveshare Touch LCD 1.54, QMI8658 IMU) that implements
  a gravity-ball bounce loop. Useful patterns to port conceptually, **not**
  its axis-mapping constants or exact tuning values:
  - Integration step: `v = (v + g*dt) * DAMPING`; on boundary hit,
    `v = -v * RESTITUTION; clamp position to boundary`.
  - Fixed-size ball pool with ring-buffer allocation (`s_next` counter) —
    used here for both the fixed-size spawn-object pool and the multi-ball
    power-up.
  - Pairwise circle-overlap separation for multiple simultaneous balls
    (position-only push-apart, no momentum exchange) — needed for the
    spread-shot power-up so balls don't stack invisibly on top of each other.
  - **Do not** port its "erase old position, then draw" incremental render
    approach — this app follows `app_racer`'s pattern of clearing and
    redrawing the whole `LGFX_Sprite` canvas every frame, which sidesteps
    that whole class of bug.

## 3. Non-goals

- No fail/game-over state, no lives, no lose condition.
- No persistent multi-ball outside the spread-shot power-up's duration.
- No SD card, no network, no BLE/ESP-NOW.
- No unit tests (this project has none — verify via `idf.py build` +
  on-device flash per CLAUDE.md).

## 4. New files

```
main/apps/app_gravity_slice/
  app_gravity_slice.h
  app_gravity_slice.cpp
  slice_renderer.h
  slice_renderer.cpp
  assets/
    gravity_slice_big.h     (56x56 icon, image_data_gravity_slice_big)
    gravity_slice_small.h   (40x40 icon, image_data_gravity_slice_small)
```

Modified files:

```
main/apps/apps.h        — add #include "app_gravity_slice/app_gravity_slice.h"
main/main.cpp            — add installApp(std::make_unique<AppGravitySlice>())
```

Follow `main/apps/app_racer/` as the structural template (`.h` holds state +
method declarations, `.cpp` holds logic, separate renderer class handles all
drawing). No `main/CMakeLists.txt` changes needed — `app_gravity_slice` has
no external include dependencies (see `MY_INCLUDE_DIRS` note in CLAUDE.md;
this app doesn't need an entry there, only `app_racer`/`app_solar_system` do).

## 5. Data structures

```cpp
enum class GameState { Playing, Finished };

enum class PowerUpKind { None, Invincible, Spiral, Spread, Beam };

enum class SpawnKind { Fruit, Bomb, PowerUp };

struct SpawnObject {
    bool live = false;
    SpawnKind kind = SpawnKind::Fruit;
    PowerUpKind powerUpKind = PowerUpKind::None; // only meaningful when kind == PowerUp
    float x = 0.0f, y = 0.0f;
    float vx = 0.0f, vy = 0.0f;
    uint16_t color = 0;
};

struct Ball {
    bool live = false;
    bool isMain = false;      // the primary ball never despawns
    float x = 0.0f, y = 0.0f;
    float vx = 0.0f, vy = 0.0f;
};
```

Use **fixed-size C arrays**, not `std::vector`, for `SpawnObject` and `Ball`
pools — per CLAUDE.md's no-PSRAM memory constraints, member containers are
permanently resident for the app's whole lifetime once installed. Suggested
caps: `kSpawnPoolSize = 10`, `kBallPoolSize = 6` (1 main + up to 5 spread
balls). Both pools live as member arrays on `AppGravitySlice`, sized at
compile time — no dynamic growth.

## 6. Constants (starting values — implementing agent should tune these on
   real hardware and record final values in the PR description, not silently
   change this doc)

```cpp
constexpr uint32_t kRunDurationMs   = 75000;   // 75s timed challenge
constexpr float kGravityPxS2        = 260.0f;  // tune for 204x109 canvas (demo_gravity used 900 on a 240x240 panel)
constexpr float kRestitution        = 0.55f;
constexpr float kDamping            = 0.995f;
constexpr float kBallRadius         = 4.0f;
constexpr uint32_t kRenderIntervalMs = 33;     // match app_racer's ~30fps cap
constexpr float kSpawnIntervalMs    = 900.0f;  // base spawn cadence, decreases as score rises
constexpr int32_t kFruitScore       = 100;
constexpr int32_t kBombPenalty      = 150;
constexpr float kPowerUpDurationS   = 4.0f;    // invincible/spiral/spread/beam
constexpr char kBestScoreSetting[]  = "gslice_best_score";
```

## 7. IMU integration

Copy `AppRacer::update_imu()`'s axis handling exactly, but instead of
collapsing to a single `_imu_steering` scalar, keep both axes as a gravity
vector:

```cpp
void AppGravitySlice::update_imu() {
    if (!GetHAL().imu.update()) return;
    _imu_data = GetHAL().imu.getImuData();
    // Same sign/scale convention as AppRacer::update_imu() — accel.x tilt
    // offset, sign-flipped for display orientation; accel.y for the second axis.
    _gravity_x = std::clamp(_imu_data.accel.x, -1.0f, 1.0f) * kGravityPxS2;
    _gravity_y = std::clamp(_imu_data.accel.y, -1.0f, 1.0f) * kGravityPxS2;
    _imu_sample_available = true;
}
```

Confirm the actual sign of `accel.y` against real hardware — `app_racer.cpp`
only asserts a documented uncertainty for its pitch sign ("if forward tilt
brakes instead of boosts, flip this sign"). Do the same live-flash-and-check
pass here for the y-axis-to-gravity-down mapping before finalizing the sign
constant.

## 8. Update loop (per frame, in `onRunning()` → `update(dt)`)

1. `update_imu()`.
2. If `_state == Finished`, skip physics, only tick `_finish_flash_remaining`.
3. Integrate all live balls: `v += gravity*dt; v *= kDamping; pos += v*dt`;
   on edge contact, clamp position and reflect the relevant velocity
   component by `-kRestitution`.
4. Run pairwise separation across live balls only if more than one is live
   (spread-shot active) — this is skipped in the common single-ball case for
   a cheap early-out.
5. Advance spawn timer; when it fires, activate the next free `SpawnObject`
   slot (ring-buffer overwrite of the oldest if the pool is full, same as
   `demo_gravity.cpp`'s `s_next`), pick `SpawnKind` by weighted random with
   bomb-weight increasing as `_elapsed`/`_score` grows.
6. Integrate spawn objects (simple upward toss + gravity fall, no bounce —
   they exit live when they fall below the screen).
7. Collision test: each live ball vs each live spawn object, circle-circle
   distance check. On hit: despawn the object, apply score/penalty/power-up,
   play the matching tone, and skip further collisions for that object this
   frame.
8. Tick any active power-up timers (`_powerup_remaining -= dt`; revert to
   `PowerUpKind::None` at zero — for `Spread`, this is also when extra balls
   are dismissed).
9. `_elapsed += dt`; when `_elapsed >= kRunDurationMs`, call `finish_run()`.

## 9. Power-ups

All four are timed states on the main ball / game, not separate entities
that need their own physics:

- **Invincible**: bomb collisions are ignored (no penalty, still despawn the
  bomb, distinct tone).
- **Spiral**: add a small sinusoidal lateral offset to the main ball's
  rendered/collision position, `offset = sin(_elapsed * spiralFreq) * spiralAmp`
  — purely cosmetic/collision-widening, does not change the underlying
  physics `x/y` used for the bounce integration.
- **Spread**: on pickup, spawn N (suggest 3) additional `Ball` entries from
  the pool at the main ball's current position with velocity fanned out at
  fixed angle offsets from the main ball's current velocity; on expiry,
  despawn the extra balls (don't try to merge them back).
- **Beam**: temporarily increase the effective collision radius used in step
  7 (both for scoring hits and rendering a wider highlight), no new entities.

Only one power-up active at a time — picking up a second while one is active
replaces it and resets the duration (simplest rule, avoids stacking
edge cases).

## 10. Rendering (`SliceRenderer`, canvas is 204x109)

- No app title text (per CLAUDE.md UI rules — launcher already shows the app
  name).
- Top strip (~10px): running score left-aligned, remaining time right-aligned
  (`mm:ss` or just seconds, given the run is under 2 minutes), current
  power-up shown as a single-letter or small icon badge between them if
  active.
- Play field: balls filled circles (main ball one color, spread balls a
  slightly dimmer variant), fruit/bomb/power-up spawn objects as small
  filled circles or simple glyphs distinguished by color (no need for real
  sprite art at this size — reuse the "small silhouette, strong contrast"
  guidance from CLAUDE.md for the two launcher icons only, not in-game
  objects).
- `renderResults()`: final score, best score, "NEW BEST" flash if beaten —
  same shape as `RoadRenderer::renderResults()`.
- Follow `app_racer`'s pattern: build the whole frame into `GetHAL().canvas`
  then `GetHAL().pushCanvas()` once per render tick, gated by
  `kRenderIntervalMs` like `AppRacer::onRunning()` does.

## 11. Audio

Reuse `apps/utils/audio/audio.h`:
- Fruit hit: short high tone.
- Bomb hit (non-invincible): short low tone, same idea as Racer's collision
  tone (`audio::play_tone(180, 0.12)`).
- Power-up pickup: distinct short tone per kind, or one shared "pickup" tone
  if four distinct ones feel excessive — implementing agent's call, keep it
  simple first.
- Run finish: `audio::play_melody(...)`, same call shape as
  `AppRacer::finish_race()`.
- Respect `audio::is_quiet_mode()` before every call, same as Racer does
  throughout.

## 12. Input / lifecycle

- `onOpen()`: reset all state (mirror `AppRacer::onOpen()`'s exhaustive reset
  list), `GetHAL().imu.begin()`, connect keyboard slot, `GetHAL().millis()`
  baseline, initial `render()`.
- `onRunning()`: `update(dt)` + interval-gated `render()` + Home-button check
  (`audio::play_random_tone(); close();` like Racer).
- `onClose()`: disconnect keyboard slot.
- This game has a single view (no sub-pages), so per CLAUDE.md's back-gesture
  rule, `KEY_ESC`/`KEY_GRAVE` behaves like Home and closes the app — same
  handling as `AppRacer::handle_key_event()`'s finished-state branch, just
  applied unconditionally since there's no sub-view to back out of. No other
  keys are needed for gameplay (tilt is the only control input), but keep the
  keyboard connection anyway for the ESC/GRAVE/Home handling.

## 13. Settings persistence

- `GetHAL().getSettings().GetInt(kBestScoreSetting, 0)` on open.
- On finish, if `_score > _best_score`, `SetInt` + `Commit()`, same pattern
  as `AppRacer::finish_race()`.

## 14. Integration checklist

- [ ] `main/apps/apps.h`: add include.
- [ ] `main/main.cpp`: add `installApp()` call.
- [ ] Icons follow the existing `iconSmall`/`iconBig` 40x40/56x56 pair
      convention (see `assets/racer_big.h`/`racer_small.h` for format).
- [ ] Launcher label short enough to not collide with neighboring icons per
      CLAUDE.md (e.g. "Slice" or "Gravity", not "Gravity Slice").

## 15. Manual verification (no unit test suite exists on this board)

Implementing agent must actually flash and test these on hardware before
handing back for review, and report results (not just "it builds"):

1. `idf.py build` succeeds with no new warnings introduced by this app.
2. Tilt the board in each of the 4 cardinal directions — confirm the ball
   accelerates toward the physically low side each time (this is the same
   axis-sign check `demo_gravity.cpp` exists to do, just against
   `app_racer`'s already-confirmed mapping instead of re-deriving it).
3. Ball reliably bounces off all 4 screen edges without escaping or getting
   stuck jittering against a wall.
4. Each spawn kind (fruit/bomb/power-up) visibly differs and scores/penalizes
   correctly on hit.
5. Each of the 4 power-ups visibly changes behavior for its duration and
   correctly reverts after.
6. Spread power-up: confirm multiple balls are visually distinguishable (not
   perfectly overlapping — this is exactly the bug `demo_gravity.cpp`'s
   comments describe hitting and fixing with pairwise separation).
7. Run reaches `kRunDurationMs` and transitions to the results screen; best
   score persists across an app close/reopen and (if convenient to test) a
   device reboot.
8. Heap sanity: this app should not meaningfully move the free-heap baseline
   versus other apps of similar complexity (Racer) — no growing allocations
   across repeated `onOpen()`/`onClose()` cycles. Spot-check with a log line
   or the existing heap-logging pattern if one exists in the repo, don't add
   a permanent one.

## 16. What the reviewing agent (this session) will check before merge

- Fixed-size arrays used for ball/spawn pools, no member `std::vector`/
  `std::string` holding sizeable data (per CLAUDE.md memory constraints).
- IMU axis mapping matches `AppRacer::update_imu()`'s established convention,
  not re-derived from scratch or copied from the QMI8658 demo.
- `KEY_ESC`/`KEY_GRAVE` handling present and matches the documented pattern.
- No app title drawn on-screen; launcher label width-safe.
- `.clang-format` compliance.
- Manual verification checklist (§15) actually run and results reported, not
  assumed from a successful build.
- Commit(s) scoped sensibly (new app addition vs. `apps.h`/`main.cpp` wiring
  can be one commit; this reviewer will handle final branch/PR flow — the
  implementing agent should not push or open a PR itself).

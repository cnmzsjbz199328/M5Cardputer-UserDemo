# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Firmware for the M5Stack Cardputer ADV (ESP32-S3), based on the official
`m5stack/M5Cardputer-UserDemo` repo (`CardputerADV` branch). Built on ESP-IDF and
the Mooncake app framework (a launcher + pluggable "app" model, similar to a tiny
mobile OS). See `docs/ble-controller-migration.md` for the fuller narrative behind
the current branch (`feat/ecp-ble-controller-app`).

A sibling repo, `esp32_test` (PlatformIO/Arduino), is the BSP/protocol test
workspace. Do not merge the two build systems — this project is ESP-IDF only.

For contribution workflow, coding conventions, testing expectations, and pull
request requirements, see [`AGENTS.md`](./AGENTS.md). This file provides the
more detailed architecture and implementation guidance for coding agents.

## Build

```bash
python3 ./fetch_repos.py   # fetch/update git-submodule-like deps listed in fetch_repos.py's repos.json
idf.py build
idf.py flash
```

Requires ESP-IDF v5.4.2 (`$IDF_PATH` must be set — see `CMakeLists.txt`).

There is no unit test suite; verification is build + on-device flashing.

### External cross-repo links

`external/ecosystem_protocol` and `external/cardputer_controller_reference` are
git-ignored local links into the sibling `esp32_test` repo (recreated via
`tools/link-esp32-test.ps1`, if present). `ecosystem_protocol.h` is the shared
BLE protocol contract; `cardputer_controller_reference` is porting-reference
source only, not a build input.

## Architecture

### Layout

- `main/hal/` — hardware abstraction: `Hal` class (`hal.h`/`hal.cpp`, accessed via
  the global `GetHAL()`) wraps display, keyboard, speaker/mic, WiFi, ESP-NOW, IR,
  BLE/USB HID, IMU, SD card, settings, and the LoRa capability
  (`hal/cap_lora868/`). Apps talk to hardware exclusively through `GetHAL()`.
- `main/apps/` — one directory per Mooncake app (`app_launcher`, `app_clock`,
  `app_ble_controller`, `app_imu`, `app_keyboard`, `app_record`, `app_repl`,
  `app_sdcard`, `app_settings`, `app_solar_system`, `app_racer`,
  `app_wifi_scan`, `app_time_machine`, `app_dummy`, plus `apps/utils/` shared
  helpers like audio/theme/common). `main/apps/apps.h` aggregates all app
  headers; `main/main.cpp` installs each app instance into the Mooncake runtime
  in `app_main()`.
- `main/main.cpp` — entry point: inits logging, `GetHAL().init()`, wires
  `ui_hal` delay/tick callbacks, installs apps, then loops
  `feedTheDog()` / `GetHAL().update()` / `GetMooncake().update()`.
- `components/` — vendored libraries used directly: `M5GFX`, `M5Unified`,
  `mooncake` (app framework), `mooncake_log`, `smooth_ui_toolkit`.
- `disabled_apps/` — apps excluded from the current build (moved out of
  `main/apps` rather than just unregistered, since `main/CMakeLists.txt` globs
  all sources under `main/apps` recursively).
- `docs/` — design/migration notes for ongoing work (e.g. BLE controller port).

### Mooncake app pattern

Every app subclasses `mooncake::AppAbility` and implements `onOpen()` /
`onRunning()` / `onClose()`; `setAppInfo().name` is set in the constructor.
Rendering goes through `GetHAL().canvas` (an `LGFX_Sprite`) followed by
`GetHAL().pushCanvas()`. Keyboard input is event-driven via
`GetHAL().keyboard.onKeyEvent.connect(...)`, returning a slot id that must be
disconnected in `onClose()`. The home button (`GetHAL().homeButton`) is the
universal "close this app" gesture, checked in `onRunning()`. See
`main/apps/app_dummy/app_dummy.cpp` for the minimal reference implementation
of this pattern.

To add a new app: create `main/apps/app_x/app_x.{h,cpp}`, include it from
`main/apps/apps.h`, and `installApp(std::make_unique<AppX>())` in
`main/main.cpp`. `main/CMakeLists.txt` globs all `.c`/`.cc`/`.cpp` under
`main/apps` automatically, so new app directories don't need CMake changes
unless they need extra include dirs (see the explicit `app_solar_system` /
`app_racer` entries in `MY_INCLUDE_DIRS`).

### Formatting

`.clang-format` (Google-based, 4-space, 120-col-ish) governs C/C++ style
project-wide.

## Memory Constraints (No PSRAM)

This board's config has **no PSRAM** (`CONFIG_SPIRAM` is not set) — everything
(WiFi driver buffers, the display's three canvas sprites, audio buffers, app
state) shares one pool of internal SRAM, roughly 245KB of which is usable at
runtime. This is tighter than it looks:

- All installed apps are constructed once at boot (`installApp()` in
  `main/main.cpp`) and live for the whole session, not just while "open" —
  `onOpen()`/`onClose()` are lifecycle hooks, not construction/destruction.
  Any sizeable buffer, `std::vector`, or `std::string` held as a member
  variable is permanently resident and shrinks the shared budget for every
  other app, even if that app is never opened. Prefer allocating anything
  non-trivial inside `onOpen()` and freeing it in `onClose()` over holding it
  as a member.
- Measured in practice: right after the WiFi driver inits (nothing else
  running), free heap drops to ~32KB with a ~24KB largest contiguous block.
  A HTTPS request (mbedTLS handshake + cert-bundle verification) reliably
  fails at that ceiling — the weather feature in `app_clock` was removed for
  exactly this reason (see git history) after confirming with heap logging
  that failure was `MBEDTLS_ERR_PK_ALLOC_FAILED`-class, not a fixable request
  bug. Treat any future HTTPS/TLS feature on this board as similarly
  unlikely to fit unless the memory budget changes (e.g. a PSRAM-equipped
  board variant).
- WiFi (and any other heavy `GetHAL()` resource with an init/deinit or
  connect/disconnect pair — BLE, ESP-NOW, etc.) should be connected only for
  as long as actively needed, then torn down: prefer `GetHAL().wifiDeinit()`
  over just `wifiDisconnect()` when an app is done with it, since deinit is
  what actually frees the driver's internal buffer pools (tens of KB);
  disconnect alone leaves them parked. `Hal::wifiDeinit()` runs
  `wifiDisconnect()` internally first so `isWifiConnected()` stays accurate.
  `app_ble_controller`'s `onClose()` (explicit disconnect, "don't leave a
  link behind") and `app_clock`'s network task are the reference examples.
  Apps that exist specifically to hold a connection (e.g. `app_wifi_scan`
  mid-test) are the exception, not apps that just need one thing fetched.
- `std::vector::resize()` is not safe to reason about as "allocates exactly
  what I asked for": libstdc++'s growth policy can round a shrinking/regrowing
  buffer up to roughly double its *previous* content size. If you gate an
  allocation on a free-heap check, compare against what `resize()` will
  actually request (drop capacity first with `clear()` + `shrink_to_fit()` if
  you need an exact-size guarantee), not a naive size delta — see
  `main/apps/utils/audio/audio.cpp`'s `acquire_buffer()` for the pattern.
- This project builds with `-fno-exceptions`. A failed allocation does not
  throw `std::bad_alloc` to catch — it calls `abort()` directly. Guard
  allocations you suspect could fail (anything sized from user/network input,
  or run after WiFi/TLS use) with an up-front `heap_caps_get_largest_free_block()`
  check and a graceful skip, not a `try`/`catch`.

## UI/UX and SD-card Review Rules

The rendered app canvas is 204x109 pixels. Preserve visual breathing room and
make the interface readable at a glance:

- `LauncherMenu::onRender()` uses 40x40 idle icons and a 56x56 selected icon.
  Keep `setAppInfo().name` as a short, one-line launcher label. Measure text
  width before drawing; truncate or choose a shorter label so it cannot touch
  another icon or wrap. Names such as `SD`, `Scan`, `BLE`, and `Record` are the
  desired scale.
- The launcher already tells the user which app is open. Do not add a repeated
  app title inside the app (for example, do not headline a WiFi settings view
  with `WiFi Settings`). Use the space for state, values, and actions; add a
  heading only when the current sub-view would otherwise be ambiguous.
- Keep icon assets in the existing `iconSmall`/`iconBig` pair at 40x40/56x56.
  Prefer a simple silhouette, consistent padding, strong contrast, and a small
  shared palette. Avoid tiny decorative details and text inside icons.
- `KEY_ESC`/`KEY_GRAVE` is the universal "back" gesture inside an app. In a
  sub-view it returns to the immediate parent view; at an app's top-level view
  it behaves like the Home button and closes the app. Any app with more than
  one internal view/page/mode must implement this consistently, using the
  `keyEvent.keyCode == KEY_ESC || keyEvent.keyCode == KEY_GRAVE` check pattern
  (see `app_sdcard.cpp`) rather than a raw key-name string compare. Exception:
  views that forward every keystroke to another device as literal input (e.g.
  `app_keyboard`'s BLE/USB HID passthrough mode) must not intercept ESC/GRAVE,
  since doing so would both act as "back" and fail to deliver that key to the
  remote host — Home remains the only exit from such a view.
- SD/TF access belongs behind `GetHAL()`. A probe should be read-only and
  repeatable; it must not create sentinel files or otherwise alter user data.
  Bound file size, directory depth, item counts, and text lengths from the card;
  check open/read/parse results and close handles on every path. Use display
  pixel width, not only byte or character count, when constraining card-supplied
  labels, and never cut a UTF-8 sequence in half.

When modifying the current SD path, note that `Hal::sdCardProbe()` presently
opens `/sdcard/test.txt` with `"w"` as a write test. Treat that as a known
review item: replace it with a non-destructive check before extending the probe
or using it as a general read-status API. Likewise, launcher labels are
currently drawn directly from app names in `main/apps/app_launcher/view/menu/`;
keep width limiting at that rendering boundary.

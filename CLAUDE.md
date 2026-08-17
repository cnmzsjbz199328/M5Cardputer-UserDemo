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

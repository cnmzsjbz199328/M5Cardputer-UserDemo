# Cardputer ADV BLE Controller Migration

This project is the formal Cardputer ADV firmware. It is based on the official
`m5stack/M5Cardputer-UserDemo` repository, `CardputerADV` branch.

The existing `esp32_test` repository remains the BSP/protocol test workspace.
Do not merge the two build systems. `esp32_test` uses PlatformIO/Arduino for
board tests, while this project uses ESP-IDF 5.4.2 and the official Mooncake
app framework.

## Repository Setup

- `upstream` points to `https://github.com/m5stack/M5Cardputer-UserDemo.git`.
- Local development branch: `feat/ecp-ble-controller-app`.
- Keep `CardputerADV` as the clean upstream tracking branch.
- Run official dependency setup after clone:

```powershell
python .\fetch_repos.py
idf.py build
```

## Cross-Repository Links

Local links are intentionally ignored by git. Recreate them with:

```powershell
powershell -ExecutionPolicy Bypass -File .\tools\link-esp32-test.ps1
```

Created links:

- `external/ecosystem_protocol` -> `..\esp32_test\lib\ecosystem_protocol`
- `external/cardputer_controller_reference` -> `..\esp32_test\controllers\cardputer`

The protocol header is the shared contract. The controller link is reference
source for porting, not a build input.

## Target App Set

Keep these official apps initially:

- `app_launcher`
- `app_set_wifi`
- `app_wifi_scan`
- `app_repl`
- `app_imu`
- `app_keyboard`
- `app_clock`
- `app_record`
- `app_sdcard`

Remove or disable these apps for the first formal controller build:

- `app_gps`
- `app_chat`
- `app_lora_chat`
- `app_remote`
- `app_stringir_toolkit`

Because `main/CMakeLists.txt` recursively compiles all files under
`main/apps`, removing an app from `apps.h` and `main.cpp` is not always enough.
The clean approach is to either move unwanted app directories out of
`main/apps`, or change `main/CMakeLists.txt` to compile an explicit app source
list.

## New App

Create:

- `main/apps/app_ble_controller/app_ble_controller.h`
- `main/apps/app_ble_controller/app_ble_controller.cpp`
- `main/apps/app_ble_controller/assets/ble_controller_big.h`
- `main/apps/app_ble_controller/assets/ble_controller_small.h`

Register it in:

- `main/apps/apps.h`
- `main/main.cpp`

The app class should follow the existing pattern:

```cpp
class AppBleController : public mooncake::AppAbility {
public:
    AppBleController();
    ~AppBleController();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;
};
```

## Porting Plan

1. Extract behavior from `external/cardputer_controller_reference/main.cpp`:
   scanning, device list, connect/disconnect, GATT discovery, state notify,
   command write, rendering, and keyboard dispatch.

2. Keep the controller boundary from `esp32_test/AGENTS.md`:
   the Cardputer app discovers devices, connects, disconnects, shows link
   status, and forwards input events. It must not own board UI state such as
   WiFi fields, menu focus, text buffers, or submit behavior.

3. Use `external/ecosystem_protocol/ecosystem_protocol.h` as the shared UUID
   and protocol constant source. Prefer keeping this file shared until the
   protocol graduates into an independent library.

4. Port BLE from Arduino `NimBLE-Arduino` calls to ESP-IDF-native BLE/NimBLE
   APIs. Avoid pulling Arduino into this ESP-IDF project just to reuse the old
   controller file unchanged.

5. Port input handling to the official HAL keyboard event API:
   `GetHAL().keyboard.onKeyEvent.connect(...)`.

6. Port rendering to `GetHAL().canvas` and `GetHAL().pushCanvas()`, reusing
   the official launcher/app visual conventions rather than the standalone
   PlatformIO controller screen wholesale.

7. Preserve the current `input.key` default path. Higher-level commands such
   as `input.text`, `config.wifi.set`, and `state.get` remain automation or
   management paths, not the normal Cardputer keyboard path.

8. Add a small internal state machine:
   `Idle`, `Scanning`, `Connecting`, `Connected`, `Disconnecting`, `Error`.

9. Verify in stages:
   build after app registration, scan for ECP devices, connect to one board,
   forward normal keys, test `FN+\`` local escape/disconnect, then test IMU
   forwarding only when the peer advertises the capability.

## Suggested First Implementation Slice

The first working slice should only include BLE scan, connect, disconnect, and
device list rendering. After that is stable, add `input.key` forwarding, then
state notifications, then optional IMU publishing.

This keeps the formal firmware usable at every step and avoids dragging the
old test-controller structure directly into the official app framework.

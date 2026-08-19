# Cardputer ADV User Demo

User demo source code of [Cardputer ADV](https://docs.m5stack.com/en/products/sku/K132-Adv).

## Build

### Fetch Dependencies

```bash
python3 ./fetch_repos.py
```

### Tool Chains

[ESP-IDF v5.4.2](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32s3/index.html)

### Build

```bash
idf.py build
```

### Flash

```bash
idf.py flash
```

## Apps

Installed at boot in `main/main.cpp`, launched from the home screen:

| Launcher label | Source | What it does |
| --- | --- | --- |
| Scan | `app_wifi_scan` | Scan for WiFi networks, connect, save credentials to NVS |
| Record | `app_record` | Mic recording |
| REPL | `app_repl` | PikaScript Python REPL |
| Clock | `app_clock` | Date/time display; connects WiFi on demand once to sync via SNTP, then disconnects |
| Keyboard | `app_keyboard` | BLE/USB HID keyboard passthrough |
| IMU | `app_imu` | IMU sensor readout |
| SDCard | `app_sdcard` | SD/TF card browsing, USB mass-storage export |
| BLE | `app_ble_controller` | ECP/BLE controller client |
| Stocks | `app_time_machine` | Plays back a historical stock-price story loaded from an SD-card JSON file |
| Solar | `app_solar_system` | Solar system simulation |
| Racer | `app_racer` | Endless-road driving mini-game |
| Settings | `app_settings` | Device settings |

`app_dummy` is a minimal reference app kept in the tree but not installed.

## Hardware notes

This board configuration has **no PSRAM** (`CONFIG_SPIRAM` is not set) —
roughly 245KB of internal SRAM covers WiFi, the display's canvas sprites,
audio buffers, and all app state combined. In practice, free heap right
after the WiFi driver initializes drops to ~30KB with a largest contiguous
block around 24KB, which is not enough headroom for a HTTPS/TLS handshake
(mbedTLS cert-bundle verification reliably fails allocating at that point).
For this reason Clock only uses WiFi to land one SNTP time sync and does not
fetch weather or anything else over HTTPS; see `CLAUDE.md`'s "Memory
Constraints" section before adding networked features.

## Acknowledgments

This project references the following open-source libraries and resources:

- https://github.com/adafruit/Adafruit_TCA8418
- https://github.com/m5stack/M5Unified.git
- https://github.com/pikasTech/PikaPython
- https://github.com/jgromes/RadioLib
- https://github.com/raysan5/raylib
- https://github.com/mikalhart/TinyGPSPlus
- https://github.com/m5stack/M5GFX.git
- https://github.com/Forairaaaaa/mooncake_log
- https://github.com/hhuysqt/esp32s3-keyboard
- https://github.com/78/xiaozhi-esp32
- https://github.com/Forairaaaaa/mooncake
- https://github.com/Forairaaaaa/smooth_ui_toolkit

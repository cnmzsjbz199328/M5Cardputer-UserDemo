/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_ble_controller.h"
#include "ble_ecp_client.h"
#include "assets/ble_controller_big.h"
#include "assets/ble_controller_small.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <mooncake_log.h>
#include <assets.h>
#include <cmath>
#include <cstdio>
#include <cstring>

using namespace mooncake;

namespace {

struct DeviceDisplay {
    char board[24];
    char machine[8];
};

const char* board_label(const char* board_id)
{
    if (strcmp(board_id, "lcd_085") == 0) return "LCD 0.85";
    if (strcmp(board_id, "touch_lcd_154") == 0) return "Touch LCD";
    if (strcmp(board_id, "epaper_154") == 0) return "ePaper";
    if (strcmp(board_id, "amoled_206") == 0) return "AMOLED";
    if (strcmp(board_id, "geek") == 0) return "Geek";
    return board_id;
}

void address_machine_code(const char* address, char* out, size_t out_size)
{
    if (strlen(address) >= 17) {
        snprintf(out, out_size, "%c%c%c%c%c%c", address[9], address[10], address[12], address[13], address[15],
                 address[16]);
    } else {
        snprintf(out, out_size, "??????");
    }
}

DeviceDisplay describe_device(const EcpClientDevice_t& dev)
{
    DeviceDisplay info = {};
    snprintf(info.board, sizeof(info.board), "%.*s", static_cast<int>(sizeof(info.board) - 1), dev.label);
    address_machine_code(dev.address, info.machine, sizeof(info.machine));

    if (strncmp(dev.label, "ECO-", 4) != 0) {
        return info;
    }

    const char* type_start = dev.label + 4;
    const char* machine_start = strrchr(type_start, '-');
    if (machine_start != nullptr && machine_start > type_start) {
        char board_id[18] = {};
        const size_t n = (static_cast<size_t>(machine_start - type_start) < sizeof(board_id) - 1)
                             ? static_cast<size_t>(machine_start - type_start)
                             : sizeof(board_id) - 1;
        memcpy(board_id, type_start, n);
        board_id[n] = '\0';
        snprintf(info.board, sizeof(info.board), "%s", board_label(board_id));
        snprintf(info.machine, sizeof(info.machine), "%s", machine_start + 1);
    }

    return info;
}

bool map_ecp_key(const Keyboard::KeyEvent_t& keyEvent, const char** key, char* char_val)
{
    *key = nullptr;
    *char_val = '\0';

    if (strcmp(keyEvent.keyName, "enter") == 0) {
        *key = "enter";
    } else if (strcmp(keyEvent.keyName, "del") == 0) {
        *key = "backspace";
    } else if (strcmp(keyEvent.keyName, "`") == 0) {
        *key = "back";
    } else if (strcmp(keyEvent.keyName, ";") == 0 || strcmp(keyEvent.keyName, "up") == 0) {
        *key = "up";
    } else if (strcmp(keyEvent.keyName, ".") == 0 || strcmp(keyEvent.keyName, "down") == 0) {
        *key = "down";
    } else if (strcmp(keyEvent.keyName, ",") == 0 || strcmp(keyEvent.keyName, "left") == 0) {
        *key = "left";
    } else if (strcmp(keyEvent.keyName, "/") == 0 || strcmp(keyEvent.keyName, "right") == 0) {
        *key = "right";
    } else if (strcmp(keyEvent.keyName, " ") == 0) {
        *key = "space";
    } else if (keyEvent.keyName[0] >= 32 && keyEvent.keyName[0] <= 126 && keyEvent.keyName[1] == '\0') {
        *key = "char";
        *char_val = keyEvent.keyName[0];
    }

    return *key != nullptr;
}

}  // namespace

AppBleController::AppBleController()
{
    setAppInfo().name     = "BLE";
    setAppInfo().userData = new AppIcon_t(image_data_ble_controller_big, image_data_ble_controller_small);
}

AppBleController::~AppBleController()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppBleController::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _cursor = 0;
    _scan_retry_time_count = 0;
    _imu_stream_active = false;
    _imu_was_connected = false;
    _imu_seq = 0;
    ecp_client_init();
    ecp_client_start_scan();
    GetHAL().imu.begin();

    _key_event_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) { handle_key_event(keyEvent); });

    render_interface();
}

void AppBleController::onRunning()
{
    if (ecp_client_get_state() == ECP_CLIENT_STATE_IDLE && strcmp(ecp_client_get_status_text(), "ble starting") == 0 &&
        GetHAL().millis() - _scan_retry_time_count > 1000) {
        ecp_client_start_scan();
        _scan_retry_time_count = GetHAL().millis();
    }

    update_imu_stream();

    // Redraw a few times a second; scan results and link state change async.
    if (GetHAL().millis() - _render_time_count > 200) {
        render_interface();
        _render_time_count = GetHAL().millis();
    }

    // Close app when home button clicked
    if (GetHAL().homeButton.wasClicked()) {
        audio::play_random_tone();
        close();
    }
}

void AppBleController::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    if (_key_event_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_event_slot_id);
        _key_event_slot_id = -1;
    }

    // Don't leave a link (or the radio time it costs) behind when the app
    // is switched away from.
    ecp_client_disconnect();
    ecp_client_stop_scan();
}

void AppBleController::handle_key_event(const Keyboard::KeyEvent_t& keyEvent)
{
    if (!keyEvent.state) return;  // only act on key-down

    const EcpClientState_t state = ecp_client_get_state();
    const bool connected_or_connecting =
        (state == ECP_CLIENT_STATE_CONNECTED || state == ECP_CLIENT_STATE_CONNECTING ||
         state == ECP_CLIENT_STATE_DISCONNECTING);

    // Fn+` resolves to keyCode KEY_ESC at the keyboard driver level; use it
    // as the universal back gesture, matching the Remote Controller Boundary
    // Constitution's FN+` local escape (esp32_test/AGENTS.md) and the
    // project-wide ESC/GRAVE back convention (see CLAUDE.md).
    if (keyEvent.keyCode == KEY_ESC) {
        if (connected_or_connecting) {
            ecp_client_disconnect();
        } else {
            close();
        }
        return;
    }

    // Bare KEY_GRAVE (no Fn) is only the top-level "close" gesture while
    // disconnected. While connected it must keep falling through to
    // map_ecp_key() below, which forwards it to the remote as a "back" HID
    // command -- intercepting it here would break that forwarding.
    if (keyEvent.keyCode == KEY_GRAVE && !connected_or_connecting) {
        close();
        return;
    }

    if (connected_or_connecting) {
        if (state == ECP_CLIENT_STATE_CONNECTED) {
            // TAB toggles input.imu streaming rather than being forwarded as
            // a key, matching the reference Cardputer controller's gesture.
            if (strcmp(keyEvent.keyName, "tab") == 0) {
                _imu_stream_active = !_imu_stream_active;
                if (_imu_stream_active) calibrate_imu_neutral();
                return;
            }

            const char* key = nullptr;
            char char_val = '\0';
            if (map_ecp_key(keyEvent, &key, &char_val)) {
                ecp_client_send_key(key, char_val);
            }
        }
        return;
    }

    if (strcmp(keyEvent.keyName, ";") == 0 || strcmp(keyEvent.keyName, "a") == 0 ||
        strcmp(keyEvent.keyName, "w") == 0 || strcmp(keyEvent.keyName, "left") == 0 ||
        strcmp(keyEvent.keyName, "up") == 0) {
        const size_t count = ecp_client_get_device_count();
        if (count > 0) _cursor = (_cursor == 0) ? count - 1 : _cursor - 1;
    } else if (strcmp(keyEvent.keyName, ".") == 0 || strcmp(keyEvent.keyName, "d") == 0 ||
               strcmp(keyEvent.keyName, "s") == 0 || strcmp(keyEvent.keyName, "right") == 0 ||
               strcmp(keyEvent.keyName, "down") == 0) {
        const size_t count = ecp_client_get_device_count();
        if (count > 0) _cursor = (_cursor + 1) % count;
    } else if (strcmp(keyEvent.keyName, "r") == 0) {
        _cursor = 0;
        ecp_client_start_scan();
    } else if (strcmp(keyEvent.keyName, "enter") == 0) {
        ecp_client_connect(_cursor);
    }
}

void AppBleController::render_interface()
{
    const EcpClientState_t state = ecp_client_get_state();
    if (state == ECP_CLIENT_STATE_CONNECTING || state == ECP_CLIENT_STATE_CONNECTED ||
        state == ECP_CLIENT_STATE_DISCONNECTING) {
        render_connected();
    } else {
        render_scan_list();
    }
}

void AppBleController::draw_device_glyph(int cx, int cy, const char* board, uint16_t color, bool active)
{
    const int size = active ? 16 : 14;
    if (active) {
        GetHAL().canvas.fillSmoothRoundRect(cx - 10, cy - 10, 20, 20, 3, THEME_COLOR_ICON);
    }

    if (strstr(board, "LCD") != nullptr) {
        GetHAL().canvas.drawRect(cx - 7, cy - 5, 14, 10, color);
        GetHAL().canvas.drawFastHLine(cx - 4, cy + 7, 8, color);
        GetHAL().canvas.drawFastVLine(cx, cy + 5, 2, color);
    } else if (strstr(board, "ePaper") != nullptr) {
        GetHAL().canvas.drawRect(cx - 5, cy - 7, 10, 14, color);
        GetHAL().canvas.drawFastHLine(cx - 3, cy - 2, 6, color);
        GetHAL().canvas.drawFastHLine(cx - 3, cy + 2, 6, color);
    } else if (strstr(board, "AMOLED") != nullptr) {
        GetHAL().canvas.drawCircle(cx, cy, 5, color);
        GetHAL().canvas.drawFastVLine(cx, cy - 8, 3, color);
        GetHAL().canvas.drawFastVLine(cx, cy + 5, 3, color);
        GetHAL().canvas.drawFastHLine(cx - 8, cy, 3, color);
        GetHAL().canvas.drawFastHLine(cx + 5, cy, 3, color);
    } else if (strstr(board, "Geek") != nullptr) {
        GetHAL().canvas.drawRect(cx - 6, cy - 6, 12, 12, color);
        GetHAL().canvas.drawRect(cx - 2, cy - 2, 4, 4, color);
    } else {
        GetHAL().canvas.drawRoundRect(cx - size / 4, cy - size / 3, size / 2, size * 2 / 3, 3, color);
        GetHAL().canvas.fillCircle(cx, cy + size / 3, 2, color);
    }
}

void AppBleController::render_scan_list()
{
    const int w = GetHAL().canvas.width();

    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setTextSize(1);

    const size_t count = ecp_client_get_device_count();
    if (count == 0) {
        GetHAL().canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        GetHAL().canvas.setCursor(4, 46);
        GetHAL().canvas.print("No devices yet");
        GetHAL().pushCanvas();
        return;
    }
    if (_cursor >= count) _cursor = 0;

    const size_t visible_rows = 5;
    size_t first = 0;
    if (_cursor >= visible_rows / 2) first = _cursor - visible_rows / 2;
    if (count > visible_rows && first + visible_rows > count) first = count - visible_rows;

    const int icon_spacing = 34;
    const size_t visible_count = (count - first < visible_rows) ? count - first : visible_rows;
    const int icons_w = static_cast<int>((visible_count - 1) * icon_spacing);
    const int start_x = w / 2 - icons_w / 2;

    for (size_t slot = 0; slot < visible_count; ++slot) {
        EcpClientDevice_t dev;
        const size_t idx = first + slot;
        if (!ecp_client_get_device(idx, &dev)) continue;

        const bool active = (idx == _cursor);
        DeviceDisplay info = describe_device(dev);
        const int cx = start_x + static_cast<int>(slot) * icon_spacing;
        draw_device_glyph(cx, 18, info.board, active ? TFT_CYAN : TFT_DARKGREY, active);
    }

    EcpClientDevice_t selected;
    if (ecp_client_get_device(_cursor, &selected)) {
        DeviceDisplay info = describe_device(selected);

        GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
        GetHAL().canvas.setCursor(4, 44);
        GetHAL().canvas.print(info.board);

        GetHAL().canvas.setTextColor(TFT_CYAN, THEME_COLOR_BG);
        GetHAL().canvas.setCursor(4, 60);
        GetHAL().canvas.print(info.machine);

        GetHAL().canvas.setTextColor(TFT_GREEN, THEME_COLOR_BG);
        GetHAL().canvas.setCursor(w - 38, 60);
        GetHAL().canvas.printf("%d", selected.rssi);

        GetHAL().canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        GetHAL().canvas.setCursor(4, 76);
        GetHAL().canvas.print(selected.address);
    }

    GetHAL().canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(w - 34, 2);
    GetHAL().canvas.printf("%u/%u", static_cast<unsigned>(_cursor + 1), static_cast<unsigned>(count));

    GetHAL().canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(4, 94);
    GetHAL().canvas.print("w/s  r  enter");

    GetHAL().pushCanvas();
}

void AppBleController::render_connected()
{
    const int w = GetHAL().canvas.width();

    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setTextSize(1);

    GetHAL().canvas.setTextColor(TFT_CYAN, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(4, 4);
    GetHAL().canvas.printf("[%s]", ecp_client_get_peer_label());

    GetHAL().canvas.setTextColor(TFT_GREEN, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(w - 56, 4);
    GetHAL().canvas.printf("MTU:%u", static_cast<unsigned>(ecp_client_get_link_mtu()));

    GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(4, 22);
    GetHAL().canvas.print(ecp_client_get_status_text());

    const char* caps = ecp_client_get_peer_capabilities();
    GetHAL().canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(4, 38);
    GetHAL().canvas.printf("caps: %s", caps[0] != '\0' ? caps : "key");

    GetHAL().canvas.setTextColor(_imu_stream_active ? TFT_CYAN : TFT_DARKGREY, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(4, 54);
    GetHAL().canvas.print(_imu_stream_active ? "IMU stream: 10Hz ON" : "IMU stream: OFF (press TAB)");

    const uint32_t drops = ecp_client_get_tx_drop_count();
    GetHAL().canvas.setTextColor(drops > 0 ? TFT_RED : TFT_DARKGREY, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(4, 70);
    GetHAL().canvas.printf("tx drops: %u", static_cast<unsigned>(drops));

    GetHAL().canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(4, 118);
    GetHAL().canvas.print("FN+` disconnect");

    GetHAL().pushCanvas();
}

void AppBleController::calibrate_imu_neutral()
{
    float ax = 0.0f, ay = 0.0f, az = 0.0f;
    int samples = 0;
    for (int i = 0; i < 10; ++i) {
        if (GetHAL().imu.update()) {
            auto data = GetHAL().imu.getImuData();
            ax += data.accel.x * 1000.0f;
            ay += data.accel.y * 1000.0f;
            az += (data.accel.z - 1.0f) * 1000.0f;  // subtract 1g resting on Z
            samples++;
        }
        GetHAL().delay(10);
    }
    if (samples > 0) {
        _imu_off_ax = ax / samples;
        _imu_off_ay = ay / samples;
        _imu_off_az = az / samples;
    }
    _imu_seq = 0;
    _imu_last_sample_ms = GetHAL().millis();
}

void AppBleController::update_imu_stream()
{
    const EcpClientState_t state = ecp_client_get_state();
    const bool connected = (state == ECP_CLIENT_STATE_CONNECTED);

    // Reset streaming state on disconnect so a stale toggle doesn't carry
    // over into the next connection.
    if (_imu_was_connected && !connected) {
        _imu_stream_active = false;
    }
    _imu_was_connected = connected;

    if (!connected || !_imu_stream_active) return;

    constexpr uint32_t kSampleIntervalMs = 100;  // 10Hz, matches reference controller
    constexpr float kDeadzoneMg = 30.0f;

    if (GetHAL().millis() - _imu_last_sample_ms < kSampleIntervalMs) return;
    if (!GetHAL().imu.update()) return;

    auto data = GetHAL().imu.getImuData();
    float ax = data.accel.x * 1000.0f - _imu_off_ax;
    float ay = data.accel.y * 1000.0f - _imu_off_ay;
    float az = data.accel.z * 1000.0f - _imu_off_az;

    if (std::abs(ax) < kDeadzoneMg) ax = 0.0f;
    if (std::abs(ay) < kDeadzoneMg) ay = 0.0f;
    if (std::abs(az) < kDeadzoneMg) az = 0.0f;

    _imu_last_sample_ms = GetHAL().millis();
    ecp_client_send_imu(++_imu_seq, static_cast<int16_t>(ax), static_cast<int16_t>(ay), static_cast<int16_t>(az),
                         static_cast<int16_t>(data.gyro.x), static_cast<int16_t>(data.gyro.y),
                         static_cast<int16_t>(data.gyro.z));
}

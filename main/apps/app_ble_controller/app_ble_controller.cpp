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
#include <cstdio>
#include <cstring>

using namespace mooncake;

AppBleController::AppBleController()
{
    setAppInfo().name     = "BLE Controller";
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
    ecp_client_init();
    ecp_client_start_scan();

    _key_event_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) { handle_key_event(keyEvent); });

    render_interface();
}

void AppBleController::onRunning()
{
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

    // Fn+` resolves to keyName "esc" at the keyboard driver level; use it as
    // the universal disconnect/back gesture, matching the Remote Controller
    // Boundary Constitution's FN+` local escape (esp32_test/AGENTS.md).
    if (strcmp(keyEvent.keyName, "esc") == 0) {
        if (connected_or_connecting) {
            ecp_client_disconnect();
        }
        return;
    }

    if (connected_or_connecting) {
        // input.key forwarding is a follow-up slice; nothing else to do yet
        // while connected.
        return;
    }

    if (strcmp(keyEvent.keyName, ";") == 0) {
        const size_t count = ecp_client_get_device_count();
        if (count > 0) _cursor = (_cursor == 0) ? count - 1 : _cursor - 1;
    } else if (strcmp(keyEvent.keyName, ".") == 0) {
        const size_t count = ecp_client_get_device_count();
        if (count > 0) _cursor = (_cursor + 1) % count;
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

void AppBleController::render_scan_list()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.setCursor(4, 4);
    GetHAL().canvas.print("BLE Controller");

    GetHAL().canvas.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(150, 4);
    GetHAL().canvas.print(ecp_client_get_status_text());

    const size_t count = ecp_client_get_device_count();
    if (count == 0) {
        GetHAL().canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        GetHAL().canvas.setCursor(4, 24);
        GetHAL().canvas.print("Scanning for ECP devices...");
        GetHAL().pushCanvas();
        return;
    }

    int y = 24;
    for (size_t i = 0; i < count && i < 6; ++i) {
        EcpClientDevice_t dev;
        if (!ecp_client_get_device(i, &dev)) break;

        if (i == _cursor) {
            GetHAL().canvas.setTextColor(TFT_CYAN, THEME_COLOR_BG);
            GetHAL().canvas.setCursor(4, y);
            GetHAL().canvas.print("> ");
        } else {
            GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
            GetHAL().canvas.setCursor(4, y);
            GetHAL().canvas.print("  ");
        }
        GetHAL().canvas.print(dev.label);
        GetHAL().canvas.setCursor(190, y);
        GetHAL().canvas.printf("%d", dev.rssi);
        y += 16;
    }

    GetHAL().canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(4, 118);
    GetHAL().canvas.print("; / . move  enter connect");

    GetHAL().pushCanvas();
}

void AppBleController::render_connected()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setTextColor(TFT_CYAN, THEME_COLOR_BG);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.setCursor(4, 4);
    GetHAL().canvas.printf("[%s]", ecp_client_get_peer_label());

    GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(4, 24);
    GetHAL().canvas.print(ecp_client_get_status_text());

    GetHAL().canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
    GetHAL().canvas.setCursor(4, 118);
    GetHAL().canvas.print("FN+` disconnect");

    GetHAL().pushCanvas();
}

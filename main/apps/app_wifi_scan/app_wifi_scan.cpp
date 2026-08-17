/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_wifi_scan.h"
#include "assets/scan_big.h"
#include "assets/scan_small.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <mooncake_log.h>
#include <assets.h>
#include <algorithm>
#include <cstring>

using namespace mooncake;

AppWifiScan::AppWifiScan()
{
    setAppInfo().name     = "Scan";
    setAppInfo().userData = new AppIcon_t(image_data_scan_big, image_data_scan_small);
}

AppWifiScan::~AppWifiScan()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppWifiScan::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    // Reset status
    _scan_result.clear();
    _selected_index     = 0;
    _input_buffer.clear();
    _wifi_ssid.clear();
    _wifi_password.clear();
    _current_state      = STATE_SCANNING;
    _connection_result  = false;

    load_saved_wifi_settings();

    // Setup keyboard event handler
    _key_event_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) { handle_key_event(keyEvent); });

    GetHAL().wifiInit();
    render_page_scanning();
    do_scan();
}

void AppWifiScan::onRunning()
{
    // Update cursor blinking
    update_cursor();

    // Process state machine
    process_state_machine();

    // Close app when home button clicked
    if (GetHAL().homeButton.wasClicked()) {
        audio::play_random_tone();
        close();
    }
}

void AppWifiScan::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    // Disconnect keyboard event handler
    if (_key_event_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_event_slot_id);
    }
}

void AppWifiScan::do_scan()
{
    GetHAL().wifiScan(_scan_result);
    if (_selected_index >= static_cast<int>(_scan_result.size())) {
        _selected_index = 0;
    }
    render_page_result();
}

void AppWifiScan::render_page_scanning()
{
    GetHAL().canvas.setBaseColor(THEME_COLOR_BG);
    GetHAL().canvas.setTextScroll(false);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setCursor(10, 5);
    GetHAL().canvas.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    GetHAL().canvas.println("Scanning WiFi...");
    GetHAL().pushCanvas();
}

void AppWifiScan::render_page_result()
{
    // Clear screen and display results
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setCursor(0, 0);
    GetHAL().canvas.setTextSize(1);

    if (_scan_result.empty()) {
        GetHAL().canvas.setTextColor(TFT_RED, THEME_COLOR_BG);
        GetHAL().canvas.println("No WiFi networks found");
    } else {
        GetHAL().canvas.setTextColor(TFT_CYAN, THEME_COLOR_BG);
        GetHAL().canvas.println("Select a network:");

        int max_count = 0;
        for (const auto& result : _scan_result) {
            if (max_count >= MAX_LIST_ITEMS) {
                break;
            }

            mclog::tagInfo(getAppInfo().name, "found: {} {}", result.second, result.first);

            bool is_selected = (max_count == _selected_index);

            // Set color based on signal strength, highlight the selected row
            if (is_selected) {
                GetHAL().canvas.setTextColor(TFT_BLACK, TFT_ORANGE);
            } else if (result.first > -60) {
                GetHAL().canvas.setTextColor(TFT_GREEN, THEME_COLOR_BG);
            } else if (result.first > -70) {
                GetHAL().canvas.setTextColor(TFT_YELLOW, THEME_COLOR_BG);
            } else {
                GetHAL().canvas.setTextColor(TFT_RED, THEME_COLOR_BG);
            }

            GetHAL().canvas.printf("%c%d ", is_selected ? '>' : ' ', result.first);
            GetHAL().canvas.println(result.second.c_str());

            max_count++;
        }

        GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
        GetHAL().canvas.println(",/. select, Enter connect");
        GetHAL().canvas.println("Space to rescan");
    }

    GetHAL().pushCanvas();
}

void AppWifiScan::render_password_prompt()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setTextScroll(true);
    GetHAL().canvas.setBaseColor(THEME_COLOR_BG);
    GetHAL().canvas.setCursor(0, 0);
    GetHAL().canvas.setTextSize(1);

    GetHAL().canvas.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    GetHAL().canvas.printf("WiFi SSID:\n%s\n", _wifi_ssid.c_str());
    GetHAL().canvas.println("Password:");
    GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    GetHAL().canvas.print(">>> ");
    GetHAL().pushCanvas();

    // Auto-fill saved password if this is the network we connected to before
    auto_fill_saved_password();
}

void AppWifiScan::render_current_input_line()
{
    // Get current cursor position - this is where we'll redraw the input line
    int cursor_y = GetHAL().canvas.getCursorY();

    // Clear the entire current line with extra space to ensure old cursor is removed
    GetHAL().canvas.fillRect(0, cursor_y, GetHAL().canvas.width(), 15, THEME_COLOR_BG);

    // Set cursor to beginning of line and redraw prompt + input + cursor
    GetHAL().canvas.setCursor(0, cursor_y);
    GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    GetHAL().canvas.print(">>> ");
    GetHAL().canvas.print(_input_buffer.c_str());

    // Draw blinking cursor
    if (_cursor_state) {
        GetHAL().canvas.print("_");
    } else {
        GetHAL().canvas.print(" ");
    }

    GetHAL().pushCanvas();
}

void AppWifiScan::handle_key_event(const Keyboard::KeyEvent_t& keyEvent)
{
    // Only handle key press events, skip modifiers
    if (!keyEvent.state || keyEvent.isModifier) {
        return;
    }

    if (_current_state == STATE_SCANNING) {
        handle_scanning_key(keyEvent);
    } else if (_current_state == STATE_WAIT_PASSWORD || _current_state == STATE_FAILED) {
        handle_password_key(keyEvent);
    }
}

void AppWifiScan::handle_scanning_key(const Keyboard::KeyEvent_t& keyEvent)
{
    // Manual rescan, works even when the list is empty
    if (keyEvent.keyCode == KEY_SPACE) {
        render_page_scanning();
        do_scan();
        return;
    }

    if (_scan_result.empty()) {
        return;
    }

    int list_count = std::min(static_cast<int>(_scan_result.size()), MAX_LIST_ITEMS);

    switch (keyEvent.keyCode) {
        // KEY_UP/KEY_DOWN come from the Fn+;/. combo; also accept the plain
        // ,/. keys since Fn combos can be finicky to hit reliably.
        case KEY_UP:
        case KEY_COMMA:
            _selected_index = (_selected_index - 1 + list_count) % list_count;
            render_page_result();
            break;

        case KEY_DOWN:
        case KEY_DOT:
            _selected_index = (_selected_index + 1) % list_count;
            render_page_result();
            break;

        case KEY_ENTER:
            _wifi_ssid = _scan_result[_selected_index].second;
            mclog::tagInfo(getAppInfo().name, "WiFi SSID selected: \"{}\"", _wifi_ssid);
            _input_buffer.clear();
            _current_state = STATE_WAIT_PASSWORD;
            render_password_prompt();
            break;

        default:
            break;
    }
}

void AppWifiScan::handle_password_key(const Keyboard::KeyEvent_t& keyEvent)
{
    switch (keyEvent.keyCode) {
        case KEY_ENTER:
            if (_current_state == STATE_WAIT_PASSWORD) {
                if (_input_buffer.empty()) {
                    return;
                }

                // Clear the current input line and redraw without cursor
                int cursor_y = GetHAL().canvas.getCursorY();
                GetHAL().canvas.fillRect(0, cursor_y, GetHAL().canvas.width(), 15, THEME_COLOR_BG);
                GetHAL().canvas.setCursor(0, cursor_y);
                GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
                GetHAL().canvas.print(">>> ");
                GetHAL().canvas.print(_input_buffer.c_str());
                GetHAL().canvas.println();  // Move to next line

                _wifi_password = _input_buffer;
                mclog::tagInfo(getAppInfo().name, "WiFi password set: \"{}\"", _wifi_password);
                _input_buffer.clear();
                _current_state = STATE_CONNECTING;
                GetHAL().pushCanvas();  // Ensure screen is updated before showing connection status
                show_connection_status();
            } else if (_current_state == STATE_FAILED) {
                // Retry connection with the same SSID and password
                _current_state = STATE_CONNECTING;
                show_connection_status();
            }
            break;

        case KEY_BACKSPACE:
            if (_current_state == STATE_WAIT_PASSWORD) {
                handle_backspace();
            }
            break;

        case KEY_SPACE:
            if (_current_state == STATE_WAIT_PASSWORD && _input_buffer.length() < INPUT_BUFFER_SIZE - 1) {
                _input_buffer += ' ';
                render_current_input_line();
            }
            break;

        default:
            // Add regular characters
            if (_current_state == STATE_WAIT_PASSWORD && keyEvent.keyName && strlen(keyEvent.keyName) == 1 &&
                _input_buffer.length() < INPUT_BUFFER_SIZE - 1) {
                _input_buffer += keyEvent.keyName;
                render_current_input_line();
            }
            break;
    }
}

void AppWifiScan::handle_backspace()
{
    if (!_input_buffer.empty()) {
        _input_buffer.pop_back();

        // Clear a larger area to ensure old cursor is completely removed
        int cursor_y = GetHAL().canvas.getCursorY();
        GetHAL().canvas.fillRect(0, cursor_y, GetHAL().canvas.width(), 15, THEME_COLOR_BG);

        // Redraw the input line
        render_current_input_line();
    }
}

void AppWifiScan::update_cursor()
{
    if (GetHAL().millis() - _cursor_update_time > CURSOR_BLINK_PERIOD) {
        _cursor_state       = !_cursor_state;
        _cursor_update_time = GetHAL().millis();
        if (_current_state == STATE_WAIT_PASSWORD) {
            render_current_input_line();
        }
    }
}

void AppWifiScan::process_state_machine()
{
    if (_current_state == STATE_CONNECTING) {
        connect_to_wifi();
        _current_state = _connection_result ? STATE_SUCCESS : STATE_FAILED;
        show_connection_result();
    }
}

void AppWifiScan::show_connection_status()
{
    GetHAL().canvas.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    GetHAL().canvas.printf("WiFi config:\n- %s\n- %s\n", _wifi_ssid.c_str(), _wifi_password.c_str());
    GetHAL().canvas.println("Connecting...");
    GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    GetHAL().pushCanvas();
}

void AppWifiScan::connect_to_wifi()
{
    mclog::tagInfo(getAppInfo().name, "attempting WiFi connection to \"{}\"", _wifi_ssid);

    // Init and connect
    GetHAL().wifiInit();
    _connection_result = GetHAL().wifiConnect(_wifi_ssid, _wifi_password);

    mclog::tagInfo(getAppInfo().name, "WiFi connection result: {}", _connection_result ? "success" : "failed");
}

void AppWifiScan::show_connection_result()
{
    if (_connection_result) {
        GetHAL().canvas.setTextColor(TFT_GREEN, THEME_COLOR_BG);
        GetHAL().canvas.println("Connected successfully!");

        // Save WiFi settings on successful connection
        save_wifi_settings();

        GetHAL().canvas.setTextColor(TFT_CYAN, THEME_COLOR_BG);
        GetHAL().canvas.println("WiFi settings saved");
        GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
        GetHAL().canvas.println("Press Home to exit");
    } else {
        GetHAL().canvas.setTextColor(TFT_RED, THEME_COLOR_BG);
        GetHAL().canvas.println("Connection failed!");
        GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
        GetHAL().canvas.println("Press Enter to retry");
        GetHAL().canvas.println("Press Home to exit");
    }

    GetHAL().pushCanvas();
}

void AppWifiScan::load_saved_wifi_settings()
{
    // Load saved SSID and password from settings
    _saved_ssid     = GetHAL().getSettings().GetString("wifi_ssid", "");
    _saved_password = GetHAL().getSettings().GetString("wifi_password", "");

    if (!_saved_ssid.empty()) {
        mclog::tagInfo(getAppInfo().name, "loaded saved WiFi SSID: \"{}\"", _saved_ssid);
    }
}

void AppWifiScan::save_wifi_settings()
{
    // Save SSID and password to settings
    GetHAL().getSettings().SetString("wifi_ssid", _wifi_ssid);
    GetHAL().getSettings().SetString("wifi_password", _wifi_password);

    mclog::tagInfo(getAppInfo().name, "saved WiFi settings - SSID: \"{}\"", _wifi_ssid);
}

void AppWifiScan::auto_fill_saved_password()
{
    // Auto-fill the input buffer with the saved password only if it matches the selected SSID
    if (!_saved_password.empty() && _wifi_ssid == _saved_ssid) {
        _input_buffer = _saved_password;
        render_current_input_line();
        mclog::tagInfo(getAppInfo().name, "auto-filled password (length: {})", _saved_password.length());
    }
}

/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <cstdint>
#include <string>
#include <hal/hal.h>

/**
 * @brief Scan for WiFi networks, pick one and enter its password to connect.
 *
 */
class AppWifiScan : public mooncake::AppAbility {
public:
    AppWifiScan();
    ~AppWifiScan();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum State_t {
        STATE_SCANNING = 0,
        STATE_WAIT_PASSWORD,
        STATE_CONNECTING,
        STATE_SUCCESS,
        STATE_FAILED
    };

    static constexpr size_t INPUT_BUFFER_SIZE     = 128;
    static constexpr uint32_t CURSOR_BLINK_PERIOD = 500;
    static constexpr uint32_t SCAN_PERIOD         = 5000;
    static constexpr int MAX_LIST_ITEMS           = 6;

    State_t _current_state = STATE_SCANNING;
    std::vector<Hal::ScanResult_t> _scan_result;
    int _selected_index   = 0;
    uint32_t _time_count  = 0;

    uint32_t _cursor_update_time = 0;
    bool _cursor_state           = false;
    std::string _input_buffer;
    std::string _wifi_ssid;
    std::string _wifi_password;
    std::string _saved_ssid;
    std::string _saved_password;
    int _key_event_slot_id  = -1;
    bool _connection_result = false;

    void handle_key_event(const Keyboard::KeyEvent_t& keyEvent);
    void handle_scanning_key(const Keyboard::KeyEvent_t& keyEvent);
    void handle_password_key(const Keyboard::KeyEvent_t& keyEvent);
    void handle_backspace();
    void update_cursor();
    void process_state_machine();

    void render_page_scanning();
    void render_page_result();
    void render_password_prompt();
    void render_current_input_line();
    void show_connection_status();
    void connect_to_wifi();
    void show_connection_result();

    void load_saved_wifi_settings();
    void save_wifi_settings();
    void auto_fill_saved_password();
};

/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <cstdint>
#include <hal/hal.h>

/**
 * @brief
 *
 */
class AppBleController : public mooncake::AppAbility {
public:
    AppBleController();
    ~AppBleController();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    int _key_event_slot_id = -1;
    size_t _cursor          = 0;
    uint32_t _render_time_count = 0;
    uint32_t _scan_retry_time_count = 0;

    // input.imu streaming state, toggled by TAB while connected. Mirrors
    // esp32_test/controllers/cardputer's ImuPublisher: 10Hz rate limit,
    // neutral-posture offset captured on connect, small deadzone.
    bool _imu_stream_active   = false;
    bool _imu_was_connected   = false;
    uint32_t _imu_seq         = 0;
    uint32_t _imu_last_sample_ms = 0;
    float _imu_off_ax = 0.0f, _imu_off_ay = 0.0f, _imu_off_az = 0.0f;

    void handle_key_event(const Keyboard::KeyEvent_t& keyEvent);
    void render_interface();
    void render_scan_list();
    void render_connected();
    void draw_device_glyph(int cx, int cy, const char* board, uint16_t color, bool active);
    void calibrate_imu_neutral();
    void update_imu_stream();
};

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

    void handle_key_event(const Keyboard::KeyEvent_t& keyEvent);
    void render_interface();
    void render_scan_list();
    void render_connected();
};

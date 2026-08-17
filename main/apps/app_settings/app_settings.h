/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include <cstdint>
#include <hal/hal.h>
#include <mooncake.h>

class AppSettings : public mooncake::AppAbility {
public:
    AppSettings();
    ~AppSettings();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    int _selected_index         = 0;
    int _key_event_slot_id      = -1;

    void handle_key_event(const Keyboard::KeyEvent_t& keyEvent);
    void render_interface();
};

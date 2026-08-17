/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <cstdint>
#include <hal/keyboard/keyboard.h>

/**
 * @brief
 *
 */
class AppSdcard : public mooncake::AppAbility {
public:
    AppSdcard();
    ~AppSdcard();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    uint32_t _time_count;
    int _key_event_slot_id = -1;

    void probe_sd_card();
    void handle_key_event(const Keyboard::KeyEvent_t& keyEvent);
};

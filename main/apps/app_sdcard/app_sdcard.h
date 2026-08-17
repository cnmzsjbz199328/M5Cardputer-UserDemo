/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <cstdint>
#include <string>
#include <vector>
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
    enum class View { Main, Diag };
    enum class MainFocus { UsbToggle, DiagMenu };

    uint32_t _time_count;
    int _key_event_slot_id = -1;
    View _view              = View::Main;
    MainFocus _main_focus   = MainFocus::UsbToggle;
    int _diag_page          = 0;
    std::vector<std::string> _diag_lines;

    void probe_sd_card();
    void handle_key_event(const Keyboard::KeyEvent_t& keyEvent);
    void render_main();
    void run_diag_probe();
    void render_diag();
};

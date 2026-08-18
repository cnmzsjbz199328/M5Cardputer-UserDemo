/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_settings.h"
#include "assets/settings_big.h"
#include "assets/settings_small.h"
#include <algorithm>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <cstdio>
#include <iterator>
#include <mooncake_log.h>
#include <string>

using namespace mooncake;

namespace {

constexpr int SETTING_COUNT = 3;
constexpr int ROW_HEIGHT    = 32;
constexpr int ROW_TOP       = 2;
constexpr int BAR_LENGTH    = 10;
constexpr uint32_t SCREEN_TIMEOUTS[] = {0, 15, 30, 60, 120, 300};

const char* setting_name(int index)
{
    switch (index) {
        case 0:
            return "Brightness";
        case 1:
            return "Volume";
        default:
            return "Screen Timeout";
    }
}

int screen_timeout_index(uint32_t value)
{
    for (int index = 0; index < static_cast<int>(std::size(SCREEN_TIMEOUTS)); ++index) {
        if (SCREEN_TIMEOUTS[index] == value) {
            return index;
        }
    }
    return 0;
}

const char* screen_timeout_label(uint32_t value)
{
    switch (value) {
        case 0:
            return "Off";
        case 15:
            return "15s";
        case 30:
            return "30s";
        case 60:
            return "1m";
        case 120:
            return "2m";
        case 300:
            return "5m";
        default:
            return "Off";
    }
}

}  // namespace

AppSettings::AppSettings()
{
    setAppInfo().name     = "Settings";
    setAppInfo().userData = new AppIcon_t(image_data_settings_big, image_data_settings_small);
}

AppSettings::~AppSettings()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppSettings::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    _selected_index = 0;
    _key_event_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) { handle_key_event(keyEvent); });
    render_interface();
}

void AppSettings::onRunning()
{
    if (GetHAL().homeButton.wasClicked()) {
        close();
    }
}

void AppSettings::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    if (_key_event_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_event_slot_id);
        _key_event_slot_id = -1;
    }
}

void AppSettings::handle_key_event(const Keyboard::KeyEvent_t& keyEvent)
{
    if (!keyEvent.state || keyEvent.isModifier) {
        return;
    }

    switch (keyEvent.keyCode) {
        case KEY_UP:
        case KEY_COMMA:
            _selected_index = (_selected_index - 1 + SETTING_COUNT) % SETTING_COUNT;
            render_interface();
            return;

        case KEY_DOWN:
        case KEY_DOT:
            _selected_index = (_selected_index + 1) % SETTING_COUNT;
            render_interface();
            return;

        case KEY_LEFT:
        case KEY_RIGHT: {
            const int direction = keyEvent.keyCode == KEY_RIGHT ? 1 : -1;
            if (_selected_index == 0) {
                const int value = std::clamp(static_cast<int>(GetHAL().getBrightness()) + direction * 17, 0, 255);
                GetHAL().setBrightness(static_cast<uint8_t>(value));
            } else if (_selected_index == 1) {
                const int value = std::clamp(static_cast<int>(GetHAL().getVolume()) + direction * 17, 0, 255);
                GetHAL().setVolume(static_cast<uint8_t>(value));
            } else {
                int index = screen_timeout_index(GetHAL().getScreenTimeoutSec());
                index = (index + direction + static_cast<int>(std::size(SCREEN_TIMEOUTS))) %
                        static_cast<int>(std::size(SCREEN_TIMEOUTS));
                GetHAL().setScreenTimeoutSec(SCREEN_TIMEOUTS[index]);
            }
            render_interface();
            return;
        }

        case KEY_ESC:
        case KEY_GRAVE:
            close();
            return;

        default:
            return;
    }
}

void AppSettings::render_interface()
{
    auto& canvas = GetHAL().canvas;
    canvas.fillScreen(THEME_COLOR_BG);
    canvas.setFont(&fonts::Font0);
    canvas.setTextScroll(false);
    canvas.setTextSize(1);

    for (int index = 0; index < SETTING_COUNT; ++index) {
        const int y             = ROW_TOP + index * ROW_HEIGHT;
        const bool is_selected  = index == _selected_index;
        const uint32_t row_bg   = is_selected ? TFT_ORANGE : THEME_COLOR_BG;
        const uint32_t text_col = is_selected ? TFT_BLACK : TFT_WHITE;

        canvas.fillRoundRect(0, y, canvas.width(), ROW_HEIGHT - 3, 4, row_bg);
        canvas.setTextColor(text_col, row_bg);
        canvas.drawString(setting_name(index), 4, y + 5);

        int percent = 0;
        const char* value_label = "";
        if (index == 0) {
            const uint8_t value = GetHAL().getBrightness();
            percent            = static_cast<int>(value) * 100 / 255;
            static char label[8];
            std::snprintf(label, sizeof(label), "%d%%", percent);
            value_label = label;
        } else if (index == 1) {
            const uint8_t value = GetHAL().getVolume();
            percent            = static_cast<int>(value) * 100 / 255;
            static char label[8];
            std::snprintf(label, sizeof(label), "%d%%", percent);
            value_label = label;
        } else {
            const int timeout_index = screen_timeout_index(GetHAL().getScreenTimeoutSec());
            percent                 = timeout_index * 100 / (static_cast<int>(std::size(SCREEN_TIMEOUTS)) - 1);
            value_label             = screen_timeout_label(GetHAL().getScreenTimeoutSec());
        }

        std::string bar("[");
        const int filled = percent * BAR_LENGTH / 100;
        for (int bar_index = 0; bar_index < BAR_LENGTH; ++bar_index) {
            bar += bar_index < filled ? '=' : '-';
        }
        bar += ']';
        canvas.drawString(bar.c_str(), 102, y + 5);
        canvas.drawRightString(value_label, canvas.width() - 5, y + 5);
    }

    GetHAL().pushCanvas();
}

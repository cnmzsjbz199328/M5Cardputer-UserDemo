/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_sdcard.h"
#include "assets/tf_big.h"
#include "assets/tf_small.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <apps/utils/sd_diagnostics.h>
#include <mooncake_log.h>
#include <assets.h>
#include <hal.h>
#include <algorithm>

using namespace mooncake;

AppSdcard::AppSdcard()
{
    setAppInfo().name     = "SDCard";
    setAppInfo().userData = new AppIcon_t(image_data_tf_big, image_data_tf_small);
}

AppSdcard::~AppSdcard()
{
    // delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppSdcard::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _view       = View::Main;
    _main_focus = MainFocus::UsbToggle;

    // Clear screen
    GetHAL().canvas.setBaseColor(THEME_COLOR_BG);
    GetHAL().canvas.setFont(FONT_REPL);
    GetHAL().canvas.setTextScroll(true);
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.setCursor(0, 0);

    probe_sd_card();
    _key_event_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) { handle_key_event(keyEvent); });
    _time_count = GetHAL().millis();
}

void AppSdcard::onRunning()
{
    if (_view == View::Main && GetHAL().millis() - _time_count > 2000) {
        probe_sd_card();
        _time_count = GetHAL().millis();
    }

    // Close app when home button clicked
    if (GetHAL().homeButton.wasClicked()) {
        audio::play_random_tone();
        if (GetHAL().sdCardUsbExportActive()) GetHAL().sdCardDisableUsbExport();
        close();
    }
}

void AppSdcard::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    if (_key_event_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_event_slot_id);
        _key_event_slot_id = -1;
    }
    if (GetHAL().sdCardUsbExportActive()) GetHAL().sdCardDisableUsbExport();
}

void AppSdcard::handle_key_event(const Keyboard::KeyEvent_t& keyEvent)
{
    if (!keyEvent.state || keyEvent.isModifier) return;

    if (_view == View::Main) {
        if (keyEvent.keyCode == KEY_DOWN) {
            if (_main_focus == MainFocus::UsbToggle) {
                _main_focus = MainFocus::DiagMenu;
                render_main();
            }
        } else if (keyEvent.keyCode == KEY_UP) {
            if (_main_focus == MainFocus::DiagMenu) {
                _main_focus = MainFocus::UsbToggle;
                render_main();
            }
        } else if (keyEvent.keyCode == KEY_ENTER) {
            if (_main_focus == MainFocus::DiagMenu) {
                _view = View::Diag;
                run_diag_probe();
            } else {
                const bool ok = GetHAL().sdCardUsbExportActive() ? GetHAL().sdCardDisableUsbExport()
                                                                 : GetHAL().sdCardEnableUsbExport();
                if (ok) probe_sd_card();
            }
        } else if (keyEvent.keyCode == KEY_GRAVE || keyEvent.keyCode == KEY_ESC) {
            if (GetHAL().sdCardUsbExportActive()) GetHAL().sdCardDisableUsbExport();
            close();
        }
    } else {  // View::Diag
        if (keyEvent.keyCode == KEY_ENTER) {
            run_diag_probe();
        } else if (keyEvent.keyCode == KEY_UP) {
            if (_diag_page > 0) --_diag_page;
            render_diag();
        } else if (keyEvent.keyCode == KEY_DOWN) {
            const int pages = std::max(1, (static_cast<int>(_diag_lines.size()) + 3) / 4);
            if (_diag_page + 1 < pages) ++_diag_page;
            render_diag();
        } else if (keyEvent.keyCode == KEY_GRAVE || keyEvent.keyCode == KEY_ESC) {
            _view = View::Main;
            probe_sd_card();
        }
    }
}

void AppSdcard::probe_sd_card()
{
    mclog::tagInfo(getAppInfo().name, "probe sd card");
    render_main();
}

void AppSdcard::render_main()
{
    auto result          = GetHAL().sdCardProbe();
    const bool usb_active = GetHAL().sdCardUsbExportActive();

    auto& canvas = GetHAL().canvas;
    canvas.setFont(FONT_REPL);
    canvas.setTextSize(1);
    canvas.setTextScroll(false);
    canvas.fillScreen(THEME_COLOR_BG);

    // Status block (fixed slots so the menu below never shifts)
    int y = 2;
    if (usb_active) {
        canvas.setTextColor(TFT_YELLOW, THEME_COLOR_BG);
        canvas.setCursor(0, y);
        canvas.print("USB Disk Mode");
        y += 16;
        canvas.setCursor(0, y);
        canvas.print("SD card owned by host");
    } else if (result.is_mounted) {
        canvas.setTextColor(TFT_CYAN, THEME_COLOR_BG);
        canvas.setCursor(0, y);
        canvas.print(result.name.c_str());
        y += 16;
        canvas.setCursor(0, y);
        canvas.print(result.size.c_str());
        y += 16;
        canvas.setCursor(0, y);
        canvas.print(result.type.c_str());
    } else {
        canvas.setTextColor(TFT_RED, THEME_COLOR_BG);
        canvas.setCursor(0, y);
        canvas.print("SD Card Not Found");
    }

    // Menu: two equal-weight, cursor-selectable rows
    const bool usb_focused  = (_main_focus == MainFocus::UsbToggle);
    const bool diag_focused = (_main_focus == MainFocus::DiagMenu);
    const char* usb_label   = usb_active ? "Eject USB Disk" : "USB Disk Mode";

    const int menu_y1 = canvas.height() - 46;
    const int menu_y2 = canvas.height() - 30;
    const int hint_y  = canvas.height() - 12;

    canvas.setTextColor(usb_focused ? TFT_BLACK : TFT_WHITE, usb_focused ? TFT_WHITE : THEME_COLOR_BG);
    canvas.setCursor(0, menu_y1);
    canvas.printf("%s%s", usb_focused ? "> " : "  ", usb_label);

    canvas.setTextColor(diag_focused ? TFT_BLACK : TFT_WHITE, diag_focused ? TFT_WHITE : THEME_COLOR_BG);
    canvas.setCursor(0, menu_y2);
    canvas.printf("%s%s", diag_focused ? "> " : "  ", "Diagnostics");

    canvas.setTextColor(TFT_DARKGRAY, THEME_COLOR_BG);
    canvas.setCursor(0, hint_y);
    canvas.print("Fn+;/. Move  Enter OK");

    GetHAL().pushCanvas();
}

void AppSdcard::run_diag_probe()
{
    _diag_page  = 0;
    _diag_lines = SdCardDiagnostics::scan();
    _diag_lines.emplace_back("Enter retry   Esc back");
    render_diag();
}

void AppSdcard::render_diag()
{
    auto& canvas = GetHAL().canvas;
    canvas.setTextScroll(false);
    canvas.fillScreen(THEME_COLOR_BG);
    canvas.setFont(FONT_BASIC);
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_CYAN, THEME_COLOR_BG);
    canvas.setCursor(2, 2);
    canvas.print("SD DIAGNOSTIC");
    canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    const int first = _diag_page * 4;
    const int last  = std::min(first + 4, static_cast<int>(_diag_lines.size()));
    int y            = 24;
    for (int i = first; i < last; ++i) {
        const auto& line = _diag_lines[i];
        if (y > canvas.height() - 5) break;
        std::string shown = line;
        if (shown.size() > 25) shown.resize(25);
        canvas.setCursor(2, y);
        canvas.print(shown.c_str());
        y += 20;
    }
    const int pages = std::max(1, (static_cast<int>(_diag_lines.size()) + 3) / 4);
    canvas.setCursor(2, canvas.height() - 12);
    canvas.printf("Page %d/%d  Up/Down", _diag_page + 1, pages);
    GetHAL().pushCanvas();
}

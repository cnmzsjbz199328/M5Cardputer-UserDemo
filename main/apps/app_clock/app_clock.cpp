/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_clock.h"
#include "assets/timer_big.h"
#include "assets/timer_small.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <mooncake_log.h>
#include <assets.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

using namespace mooncake;

AppClock::AppClock()
{
    setAppInfo().name     = "Clock";
    setAppInfo().userData = new AppIcon_t(image_data_timer_big, image_data_timer_small);
}

AppClock::~AppClock()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppClock::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    render_interface();
    start_network_task();

    _key_event_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) { handle_key_event(keyEvent); });
}

void AppClock::onRunning()
{
    // Update time display every second
    if (GetHAL().millis() - _time_count > UPDATE_INTERVAL) {
        update_time_display();
        _time_count = GetHAL().millis();
    }

    // Close app when home button clicked
    if (GetHAL().homeButton.wasClicked()) {
        audio::play_random_tone();
        close();
    }
}

void AppClock::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    if (_key_event_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_event_slot_id);
        _key_event_slot_id = -1;
    }
    // A background wifi task may still be running; it holds no reference to
    // view state and writes into this long-lived app instance, so it is left
    // to finish on its own rather than being torn down here.
}

void AppClock::handle_key_event(const Keyboard::KeyEvent_t& keyEvent)
{
    if (!keyEvent.state || keyEvent.isModifier) {
        return;
    }
    if (keyEvent.keyCode == KEY_ESC || keyEvent.keyCode == KEY_GRAVE) {
        close();
    }
}

void AppClock::render_interface()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setFont(&fonts::Font0);
    GetHAL().pushCanvas();
}

void AppClock::update_time_display()
{
    start_network_task();
    draw_clock_frame();
}

void AppClock::draw_clock_frame()
{
    const int cx = GetHAL().canvas.width() / 2;

    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setFont(&fonts::Font0);

    // --- Status line -----------------------------------------------------
    // isTimeSynced() only reflects whether the clock has ever been set; it
    // stays true even after WiFi later drops, so it must not be treated as a
    // live connection indicator here (that previously showed a stale green
    // "Network Time" while WiFi was actually disconnected).
    bool time_synced    = GetHAL().isTimeSynced();
    bool wifi_connected = GetHAL().isWifiConnected();
    GetHAL().canvas.setCursor(6, 3);
    GetHAL().canvas.setTextSize(1);
    if (time_synced && wifi_connected) {
        GetHAL().canvas.setTextColor(TFT_GREEN, THEME_COLOR_BG);
        GetHAL().canvas.print("Network Time");
    } else if (time_synced) {
        GetHAL().canvas.setTextColor(TFT_YELLOW, THEME_COLOR_BG);
        GetHAL().canvas.print("Time Synced (Offline)");
    } else {
        switch (_net_status.load()) {
            case NET_CONNECTING:
                GetHAL().canvas.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
                GetHAL().canvas.print("Connecting WiFi...");
                break;
            case NET_FAILED_NO_CREDS:
                GetHAL().canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
                GetHAL().canvas.print("No WiFi saved");
                break;
            case NET_FAILED_CONNECT:
                GetHAL().canvas.setTextColor(TFT_RED, THEME_COLOR_BG);
                GetHAL().canvas.print("WiFi connect failed");
                break;
            default:
                GetHAL().canvas.setTextColor(TFT_YELLOW, THEME_COLOR_BG);
                GetHAL().canvas.print("System Uptime");
                break;
        }
    }

    // --- Date ---------------------------------------------------------------
    // The system bar already shows the time, so this view only needs the
    // date. Rows are stacked from a running cursor (line height read back
    // from the font actually in effect) instead of hand-picked absolute y
    // values, so layout stays correct regardless of text size per row.
    std::string date_str;
    if (time_synced) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        date_str = fmt::format("{:04d}-{:02d}-{:02d}", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    }

    const int y = 40;

    GetHAL().canvas.setTextSize(2);
    if (time_synced) {
        GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
        GetHAL().canvas.drawCenterString(date_str.c_str(), cx, y);
    } else {
        GetHAL().canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        GetHAL().canvas.drawCenterString("Set WiFi for real time", cx, y);
    }

    GetHAL().pushCanvas();
}

/* -------------------------------------------------------------------------- */
/*                              WiFi / time-sync task                         */
/* -------------------------------------------------------------------------- */

void AppClock::start_network_task()
{
    if (_net_task_running.load() || GetHAL().isTimeSynced()) {
        return;
    }

    bool has_creds = !GetHAL().getSettings().GetString("wifi_ssid", "").empty();
    if (!GetHAL().isWifiConnected() && !has_creds) {
        _net_status = NET_FAILED_NO_CREDS;
        return;
    }

    _net_task_running = true;
    xTaskCreate(&AppClock::network_task_entry, "clock_net", 8192, this, tskIDLE_PRIORITY + 1, nullptr);
}

void AppClock::network_task_entry(void* param)
{
    static_cast<AppClock*>(param)->network_task_body();
}

void AppClock::network_task_body()
{
    // This board has no PSRAM, and the WiFi driver's own buffer pools (tens
    // of KB) are the biggest thing standing between it and anything else
    // that wants heap. WiFi here only exists to land one SNTP sync; the
    // clock display doesn't need a live connection afterwards, so tear the
    // driver all the way down (wifiDeinit(), not just disconnect) once the
    // sync attempt is over. wifiConnect() re-inits on demand next time, so
    // this costs only reconnect latency, not functionality.
    bool we_connected_wifi = false;

    if (!GetHAL().isWifiConnected()) {
        _net_status = NET_CONNECTING;

        auto ssid     = GetHAL().getSettings().GetString("wifi_ssid", "");
        auto password = GetHAL().getSettings().GetString("wifi_password", "");
        if (ssid.empty()) {
            _net_status      = NET_FAILED_NO_CREDS;
            _net_task_running = false;
            vTaskDelete(NULL);
            return;
        }

        if (!GetHAL().wifiConnect(ssid, password)) {
            _net_status      = NET_FAILED_CONNECT;
            GetHAL().wifiDeinit();
            _net_task_running = false;
            vTaskDelete(NULL);
            return;
        }
        we_connected_wifi = true;
    }

    // Give SNTP a window to land the first sync; on this network that has
    // been observed to take ~10s.
    for (int i = 0; i < 200 && !GetHAL().isTimeSynced(); ++i) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    _net_status = GetHAL().isTimeSynced() ? NET_CONNECTED : NET_FAILED_CONNECT;

    if (we_connected_wifi) {
        GetHAL().wifiDeinit();
    }

    _net_task_running = false;
    vTaskDelete(NULL);
}

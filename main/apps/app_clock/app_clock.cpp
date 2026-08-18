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
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <cJSON.h>
#include <algorithm>

using namespace mooncake;

namespace {

// Adelaide (West Terrace / ngayirdapira) station, public/unauthenticated BOM
// observations feed. Documented at http://www.bom.gov.au/catalogue/data-feeds.shtml
// Must be https and carry a non-generic User-Agent: BOM's CDN 403s plain http
// and known scraper user agents (e.g. curl's default) over https.
constexpr char WEATHER_URL[]          = "https://www.bom.gov.au/fwo/IDS60801/IDS60801.94648.json";
constexpr char WEATHER_USER_AGENT[]   = "CardputerADV/1.0 (M5Stack Cardputer)";
constexpr size_t WEATHER_MAX_RESPONSE = 65536;

struct HttpRespBuf {
    std::string data;
};

esp_err_t http_event_handler(esp_http_client_event_t* evt)
{
    if (evt->event_id != HTTP_EVENT_ON_DATA) {
        return ESP_OK;
    }
    auto* buf = static_cast<HttpRespBuf*>(evt->user_data);
    if (!buf || buf->data.size() >= WEATHER_MAX_RESPONSE) {
        return ESP_OK;
    }
    size_t remaining = WEATHER_MAX_RESPONSE - buf->data.size();
    size_t take      = std::min(static_cast<size_t>(evt->data_len), remaining);
    buf->data.append(static_cast<const char*>(evt->data), take);
    return ESP_OK;
}

}  // namespace

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
    // A background wifi/weather task may still be running; it holds no
    // reference to view state and writes into this long-lived app instance,
    // so it is left to finish on its own rather than being torn down here.
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
    GetHAL().canvas.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    GetHAL().canvas.setTextSize(2);
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

    // --- Status line -----------------------------------------------------
    bool time_synced = GetHAL().isTimeSynced();
    GetHAL().canvas.setCursor(6, 3);
    GetHAL().canvas.setTextSize(1);
    if (time_synced) {
        GetHAL().canvas.setTextColor(TFT_GREEN, THEME_COLOR_BG);
        GetHAL().canvas.print("Network Time");
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

    // --- Time / date -------------------------------------------------------
    std::string time_str;
    std::string date_str;
    if (time_synced) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        time_str = fmt::format("{:02d}:{:02d}:{:02d}", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        date_str = fmt::format("{:04d}-{:02d}-{:02d}", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
    } else {
        uint32_t total_seconds = GetHAL().millis() / 1000;
        uint32_t hours         = total_seconds / 3600;
        uint32_t minutes       = (total_seconds % 3600) / 60;
        uint32_t seconds       = total_seconds % 60;
        time_str                = fmt::format("{:02d}:{:02d}:{:02d}", hours, minutes, seconds);
    }

    GetHAL().canvas.setTextSize(2);
    GetHAL().canvas.setTextColor(TFT_ORANGE, THEME_COLOR_BG);
    GetHAL().canvas.drawCenterString(time_str.c_str(), cx, 18);

    GetHAL().canvas.setTextSize(1);
    if (time_synced) {
        GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
        GetHAL().canvas.drawCenterString(date_str.c_str(), cx, 40);
    } else {
        GetHAL().canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        GetHAL().canvas.drawCenterString("Set WiFi for real time", cx, 40);
    }

    // --- Weather -------------------------------------------------------
    WeatherData_t weather;
    {
        std::lock_guard<std::mutex> lock(_weather_mutex);
        weather = _weather;
    }

    GetHAL().canvas.setTextSize(1);
    if (weather.valid) {
        auto line1 = fmt::format("Adelaide {:.1f}C  {}", weather.temp, weather.condition.c_str());
        GetHAL().canvas.setTextColor(TFT_CYAN, THEME_COLOR_BG);
        GetHAL().canvas.drawCenterString(line1.c_str(), cx, 56);

        std::string line2 = fmt::format("Feels {:.1f}C", weather.feels_like);
        if (weather.humidity >= 0) {
            line2 += fmt::format("  Hum {}%", weather.humidity);
        }
        GetHAL().canvas.setTextColor(TFT_LIGHTGREY, THEME_COLOR_BG);
        GetHAL().canvas.drawCenterString(line2.c_str(), cx, 68);

        if (weather.wind_kmh >= 0) {
            auto line3 = fmt::format("Wind {} {:.0f}km/h", weather.wind_dir.empty() ? "-" : weather.wind_dir,
                                     weather.wind_kmh);
            GetHAL().canvas.drawCenterString(line3.c_str(), cx, 80);
        }
    } else if (time_synced) {
        GetHAL().canvas.setTextColor(TFT_DARKGREY, THEME_COLOR_BG);
        GetHAL().canvas.drawCenterString("Fetching weather...", cx, 60);
    }

    GetHAL().pushCanvas();
}

/* -------------------------------------------------------------------------- */
/*                          WiFi / weather background task                    */
/* -------------------------------------------------------------------------- */

void AppClock::start_network_task()
{
    if (_net_task_running.load()) {
        return;
    }

    bool has_creds     = !GetHAL().getSettings().GetString("wifi_ssid", "").empty();
    bool needs_connect = !GetHAL().isWifiConnected() && has_creds;
    bool weather_stale = _last_weather_fetch_ms == 0 ||
                          (GetHAL().millis() - _last_weather_fetch_ms) > WEATHER_REFRESH_INTERVAL;
    bool needs_weather = GetHAL().isWifiConnected() && weather_stale;

    if (!needs_connect && !needs_weather) {
        return;
    }

    // esp_http_client + mbedTLS (crt bundle verification) needs a generous
    // stack; 8KB proved too tight for the HTTPS handshake on this task.
    _net_task_running = true;
    xTaskCreate(&AppClock::network_task_entry, "clock_net", 16384, this, tskIDLE_PRIORITY + 1, nullptr);
}

void AppClock::network_task_entry(void* param)
{
    static_cast<AppClock*>(param)->network_task_body();
}

void AppClock::network_task_body()
{
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
            _net_task_running = false;
            vTaskDelete(NULL);
            return;
        }
    }

    // Give SNTP a short window to land the first sync.
    for (int i = 0; i < 50 && !GetHAL().isTimeSynced(); ++i) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    _net_status = NET_CONNECTED;

    WeatherData_t fresh;
    if (fetch_weather(fresh)) {
        std::lock_guard<std::mutex> lock(_weather_mutex);
        _weather              = fresh;
        _last_weather_fetch_ms = GetHAL().millis();
    }

    _net_task_running = false;
    vTaskDelete(NULL);
}

bool AppClock::fetch_weather(WeatherData_t& out)
{
    HttpRespBuf resp;

    esp_http_client_config_t config = {};
    config.url                      = WEATHER_URL;
    config.event_handler            = http_event_handler;
    config.user_data                = &resp;
    config.timeout_ms               = 8000;
    config.crt_bundle_attach        = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        mclog::tagError(getAppInfo().name, "weather: failed to init http client");
        return false;
    }
    esp_http_client_set_header(client, "User-Agent", WEATHER_USER_AGENT);

    esp_err_t err = esp_http_client_perform(client);
    int status     = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200 || resp.data.empty()) {
        mclog::tagError(getAppInfo().name, "weather: http request failed ({}), status {}", esp_err_to_name(err),
                        status);
        return false;
    }

    cJSON* root = cJSON_Parse(resp.data.c_str());
    if (!root) {
        mclog::tagError(getAppInfo().name, "weather: json parse failed");
        return false;
    }

    bool ok             = false;
    cJSON* observations = cJSON_GetObjectItem(root, "observations");
    cJSON* data_arr      = observations ? cJSON_GetObjectItem(observations, "data") : nullptr;

    if (cJSON_IsArray(data_arr) && cJSON_GetArraySize(data_arr) > 0) {
        cJSON* first       = cJSON_GetArrayItem(data_arr, 0);
        cJSON* air_temp    = cJSON_GetObjectItem(first, "air_temp");
        cJSON* apparent_t  = cJSON_GetObjectItem(first, "apparent_t");
        // "weather" is almost always "-"; the actual sky description lives in
        // "cloud" (e.g. "Partly cloudy"), so prefer that and fall back.
        cJSON* cloud       = cJSON_GetObjectItem(first, "cloud");
        cJSON* weather     = cJSON_GetObjectItem(first, "weather");
        cJSON* rel_hum     = cJSON_GetObjectItem(first, "rel_hum");
        cJSON* wind_dir    = cJSON_GetObjectItem(first, "wind_dir");
        cJSON* wind_spd    = cJSON_GetObjectItem(first, "wind_spd_kmh");

        auto is_meaningful = [](cJSON* item) {
            return cJSON_IsString(item) && item->valuestring[0] != '-' && item->valuestring[0] != '\0';
        };

        if (cJSON_IsNumber(air_temp)) {
            out.temp        = static_cast<float>(air_temp->valuedouble);
            out.feels_like   = cJSON_IsNumber(apparent_t) ? static_cast<float>(apparent_t->valuedouble) : out.temp;
            out.condition    = is_meaningful(cloud) ? cloud->valuestring
                                                     : (is_meaningful(weather) ? weather->valuestring : "Clear");
            out.humidity     = cJSON_IsNumber(rel_hum) ? rel_hum->valueint : -1;
            out.wind_dir     = cJSON_IsString(wind_dir) ? wind_dir->valuestring : "";
            out.wind_kmh     = cJSON_IsNumber(wind_spd) ? static_cast<float>(wind_spd->valuedouble) : -1;
            out.valid        = true;
            ok               = true;
        }
    }

    cJSON_Delete(root);
    return ok;
}

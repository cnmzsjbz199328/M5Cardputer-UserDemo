/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "keyboard/keyboard.h"
#include "cap_lora868/cap_lora868.h"
#include "utils/settings/settings.h"
#include <M5Unified.hpp>
#include <M5GFX.h>
#include <memory>
#include <cstdint>
#include <string>
#include <vector>

class Hal {
public:
    void init();
    void update();

    /* --------------------------------- System --------------------------------- */
    void delay(std::uint32_t ms)
    {
        m5gfx::delay(ms);
    }
    std::uint32_t millis()
    {
        return m5gfx::millis();
    }
    void feedTheDog();
    std::vector<uint8_t> getDeviceMac();
    std::string getDeviceMacString();

    /* --------------------------------- Display -------------------------------- */
    M5GFX& display                = M5.Display;
    LGFX_Sprite canvas            = LGFX_Sprite(&M5.Display);
    LGFX_Sprite canvasSystemBar   = LGFX_Sprite(&M5.Display);
    LGFX_Sprite canvasKeyboardBar = LGFX_Sprite(&M5.Display);

    inline void pushCanvasSystemBar()
    {
        canvasSystemBar.pushSprite(canvasKeyboardBar.width(), 0);
    }
    inline void pushCanvasKeyboardBar()
    {
        canvasKeyboardBar.pushSprite(0, 0);
    }
    inline void pushCanvas()
    {
        canvas.pushSprite(canvasKeyboardBar.width(), canvasSystemBar.height());
    }

    uint8_t getBrightness() const
    {
        return _brightness;
    }
    void setBrightness(uint8_t value);

    /* ---------------------------------- Audio --------------------------------- */
    m5::Speaker_Class& speaker = M5.Speaker;
    m5::Mic_Class& mic         = M5.Mic;

    uint8_t getVolume() const
    {
        return _volume;
    }
    void setVolume(uint8_t value);

    /* ---------------------------------- Input --------------------------------- */
    m5::Button_Class& homeButton = M5.BtnA;
    Keyboard keyboard;

    /* ---------------------------------- Power --------------------------------- */
    inline uint8_t getBatLevel()
    {
        return M5.Power.getBatteryLevel();
    }

    uint32_t getScreenTimeoutSec() const
    {
        return _screen_timeout_sec;
    }
    void setScreenTimeoutSec(uint32_t seconds);
    void resetScreenIdleTimer();

    /* ---------------------------------- WiFi ---------------------------------- */
    using ScanResult_t = std::pair<int, std::string>;
    void wifiInit();
    void wifiDeinit();
    void wifiScan(std::vector<ScanResult_t>& scanResult);
    bool wifiConnect(const std::string& ssid, const std::string& password);
    bool isWifiConnected() const
    {
        return _is_wifi_connected;
    }
    void wifiDisconnect();

    /* ---------------------------------- Time ---------------------------------- */
    bool isTimeSynced() const
    {
        // Time code in HAL because the clock is technically in hardware. And the
        // WiFi components set it. Don't check esp_sntp_get_sync_status() because
        // we're not continuously syncing.
        constexpr time_t t2026 = 1767225600;  // "2026-01-01T00:00Z"
        return time(NULL) > t2026;
    }

    /* --------------------------------- EspNow --------------------------------- */
    void espNowInit();
    void espNowDeinit();
    void espNowSend(const std::string& data);
    bool espNowAvailable();
    const std::string& espNowGetReceivedData();
    void espNowClearReceivedData();

    /* ----------------------------------- IR ----------------------------------- */
    void irInit();
    void irSend(uint8_t addr, uint8_t cmd);

    /* ----------------------------------- BLE ---------------------------------- */
    void bleKeyboardInit();
    bool bleKeyboardIsConnected() const;

    /* ----------------------------------- USB ---------------------------------- */
    void usbKeyboardInit();
    bool usbKeyboardIsConnected() const;

    /* -------------------------------- Settings -------------------------------- */
    Settings& getSettings()
    {
        return *_settings;
    }

    /* ----------------------------------- IMU ---------------------------------- */
    m5::IMU_Class& imu = M5.Imu;

    /* --------------------------------- SD Card -------------------------------- */
    struct SdCardProbeResult_t {
        bool is_mounted = false;
        std::string size;
        std::string type;
        std::string name;

        bool operator==(const SdCardProbeResult_t& other) const
        {
            return is_mounted == other.is_mounted && size == other.size && type == other.type && name == other.name;
        }
    };

    SdCardProbeResult_t sdCardProbe();
    bool sdCardEnableUsbExport();
    bool sdCardDisableUsbExport();
    void onUsbMscMountChanged(bool is_mounted);
    bool sdCardUsbExportActive() const
    {
        return _sd_card_usb_export;
    }

    /* ----------------------------------- Cap ---------------------------------- */
    CapLoRa868 capLora868;

private:
    Settings* _settings             = nullptr;
    bool _is_wifi_inited            = false;
    bool _is_wifi_connected         = false;
    bool _is_esp_now_inited         = false;
    bool _is_ir_inited              = false;
    bool _is_ble_keyboard_inited    = false;
    bool _is_usb_keyboard_inited    = false;
    bool _is_sd_card_mounted        = false;
    bool _sd_card_usb_export        = false;
    bool _sd_card_msc_ready         = false;
    int _ble_keyboard_event_slot_id = -1;
    int _usb_keyboard_event_slot_id = -1;
    uint8_t _brightness              = 128;
    uint8_t _volume                  = 90;
    uint32_t _screen_timeout_sec     = 0;
    uint32_t _last_activity_ms       = 0;
    bool _screen_dimmed              = false;
    int _screen_timeout_event_slot_id = -1;
    std::unique_ptr<CapLoRa868> _cap_lora868;

    void display_init();
    void i2c_scan();
    void keyboard_init();
    void screen_timeout_init();
    void start_sntp();
    void stop_sntp();
    void setting_init();
    void spi_init();
    void sd_card_init();
    void handle_ble_keyboard_event(const Keyboard::KeyEvent_t& keyEvent);
    void handle_usb_keyboard_event(const Keyboard::KeyEvent_t& keyEvent);
};

Hal& GetHAL();

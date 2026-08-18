/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "hal.h"
#include "hal_config.h"
#include <apps/utils/audio/audio.h>
#include <mooncake_log.h>
#include <M5Unified.hpp>
#include <esp_mac.h>
#include <algorithm>
#include <limits>
#include <memory>

static std::unique_ptr<Hal> _hal_instance;
static const std::string _tag = "HAL";

Hal& GetHAL()
{
    if (!_hal_instance) {
        mclog::tagInfo(_tag, "creating hal instance");
        _hal_instance = std::make_unique<Hal>();
    }
    return *_hal_instance.get();
}

void Hal::init()
{
    mclog::tagInfo(_tag, "init");

    M5.begin();
    M5.Display.setBrightness(0);
    M5.Speaker.begin();  // Codec takes some time to initialize

    display_init();
    i2c_scan();
    keyboard_init();
    screen_timeout_init();
    setting_init();
    spi_init();
}

void Hal::update()
{
    M5.update();
    if (homeButton.wasClicked()) {
        const bool was_dimmed = _screen_dimmed;
        resetScreenIdleTimer();
        if (was_dimmed) {
            // Waking the screen shouldn't also trigger the app's own
            // Home-button-closes-app handling in the same frame.
            homeButton.setState(millis(), m5::Button_Class::state_nochange);
        }
    }
    keyboard.update();
    capLora868.update();

    if (_screen_timeout_sec > 0 && !_screen_dimmed) {
        constexpr uint32_t kMillisPerSecond = 1000;
        const uint32_t timeout_ms          = _screen_timeout_sec * kMillisPerSecond;
        if (millis() - _last_activity_ms > timeout_ms) {
            display.setBrightness(0);
            _screen_dimmed = true;
        }
    }
}

void Hal::feedTheDog()
{
    vTaskDelay(1);
}

std::vector<uint8_t> Hal::getDeviceMac()
{
    std::vector<uint8_t> mac(6);
    esp_read_mac(mac.data(), ESP_MAC_EFUSE_FACTORY);
    return mac;
}

std::string Hal::getDeviceMacString()
{
    auto mac = getDeviceMac();
    return fmt::format("{:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

/* -------------------------------------------------------------------------- */
/*                                  Dispplay                                  */
/* -------------------------------------------------------------------------- */
void Hal::display_init()
{
    mclog::tagInfo(_tag, "display init");

    canvas.createSprite(204, 109);
    canvasKeyboardBar.createSprite(display.width() - canvas.width(), display.height());
    canvasSystemBar.createSprite(canvas.width(), display.height() - canvas.height());
}

/* -------------------------------------------------------------------------- */
/*                                     I2C                                    */
/* -------------------------------------------------------------------------- */
void Hal::i2c_scan()
{
    mclog::tagInfo(_tag, "i2c scan");

    bool ret[128] = {false};
    M5.In_I2C.scanID(ret);

    uint8_t address;
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
    for (int i = 0; i < 128; i += 16) {
        printf("%02x: ", i);
        for (int j = 0; j < 16; j++) {
            fflush(stdout);
            address = i + j;
            if (ret[address]) {
                printf("%02x ", address);
            } else {
                printf("-- ");
            }
        }
        printf("\r\n");
    }
}

/* -------------------------------------------------------------------------- */
/*                                  Settings                                  */
/* -------------------------------------------------------------------------- */
// https://github.com/78/xiaozhi-esp32/blob/main/main/main.cc

void Hal::setting_init()
{
    mclog::tagInfo(_tag, "setting init");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        mclog::tagWarn(_tag, "erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    _settings = new Settings("cardputer", true);

    _brightness = static_cast<uint8_t>(std::clamp<int32_t>(_settings->GetInt("brightness", 128), 0, 255));
    _volume     = static_cast<uint8_t>(std::clamp<int32_t>(_settings->GetInt("volume", audio::DEFAULT_VOLUME), 0, 255));

    const int32_t screen_timeout = _settings->GetInt("screen_timeout", 300);
    if (screen_timeout <= 0) {
        _screen_timeout_sec = 0;
    } else {
        const auto max_timeout = std::numeric_limits<uint32_t>::max() / 1000;
        _screen_timeout_sec = static_cast<uint32_t>(screen_timeout) > max_timeout
                                  ? max_timeout
                                  : static_cast<uint32_t>(screen_timeout);
    }

    display.setBrightness(_brightness);
    speaker.setVolume(_volume);
}

void Hal::setBrightness(uint8_t value)
{
    _brightness = value;
    display.setBrightness(_screen_dimmed ? 0 : _brightness);
    getSettings().SetInt("brightness", _brightness);
    getSettings().Commit();
}

void Hal::setVolume(uint8_t value)
{
    _volume = value;
    speaker.setVolume(_volume);
    getSettings().SetInt("volume", _volume);
    getSettings().Commit();
}

void Hal::setScreenTimeoutSec(uint32_t seconds)
{
    const auto max_timeout = std::numeric_limits<uint32_t>::max() / 1000;
    _screen_timeout_sec = std::min(seconds, max_timeout);
    getSettings().SetInt("screen_timeout", static_cast<int32_t>(_screen_timeout_sec));
    getSettings().Commit();
    resetScreenIdleTimer();
}

void Hal::resetScreenIdleTimer()
{
    _last_activity_ms = millis();
    if (_screen_dimmed) {
        display.setBrightness(_brightness);
        _screen_dimmed = false;
    }
}

void Hal::screen_timeout_init()
{
    _last_activity_ms = millis();
    _screen_timeout_event_slot_id = keyboard.onKeyEvent.connect([this](const Keyboard::KeyEvent_t&) {
        // This is a simple global idle timer. Long-running apps can call
        // GetHAL().resetScreenIdleTimer() periodically if they need to suppress auto-dim.
        resetScreenIdleTimer();
    });
}

/* -------------------------------------------------------------------------- */
/*                                  Keyboard                                  */
/* -------------------------------------------------------------------------- */
void Hal::keyboard_init()
{
    mclog::tagInfo(_tag, "keyboard init");

    if (!keyboard.init()) {
        mclog::tagError(_tag, "keyboard init failed");
        return;
    }
}

/* -------------------------------------------------------------------------- */
/*                                    WiFI                                    */
/* -------------------------------------------------------------------------- */
// https://github.com/espressif/esp-idf/blob/v5.3.3/examples/wifi/scan/main/scan.c
// https://github.com/espressif/esp-idf/blob/v5.4.2/examples/wifi/getting_started/station/main/station_example_main.c
// http://github.com/espressif/esp-idf/blob/v5.4.2/examples/protocols/sntp/main/sntp_example_main.c
#include <esp_wifi.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <esp_netif.h>
#include <esp_err.h>
#include <esp_system.h>
#include <esp_event.h>
#include <lwip/err.h>
#include <lwip/sys.h>
#include <vector>
#include <time.h>
#include <sys/time.h>
#include <esp_sntp.h>

#define DEFAULT_SCAN_LIST_SIZE 6
static wifi_ap_record_t _ap_info[DEFAULT_SCAN_LIST_SIZE];

void Hal::wifiScan(std::vector<ScanResult_t>& scanResult)
{
    mclog::tagInfo(_tag, "wifi scan");

    scanResult.clear();

    uint16_t number   = DEFAULT_SCAN_LIST_SIZE;
    uint16_t ap_count = 0;
    memset(_ap_info, 0, sizeof(_ap_info));

    // Start WiFi scan
    esp_err_t ret = esp_wifi_scan_start(NULL, true);
    if (ret != ESP_OK) {
        mclog::tagError(_tag, "failed to start wifi scan: {}", esp_err_to_name(ret));
        return;
    }

    ret = esp_wifi_scan_get_ap_num(&ap_count);
    if (ret != ESP_OK) {
        mclog::tagError(_tag, "failed to get AP number: {}", esp_err_to_name(ret));
        return;
    }

    ret = esp_wifi_scan_get_ap_records(&number, _ap_info);
    if (ret != ESP_OK) {
        mclog::tagError(_tag, "failed to get AP records: {}", esp_err_to_name(ret));
        return;
    }

    // Process scan results
    for (int i = 0; i < number; i++) {
        std::string ssid = (char*)_ap_info[i].ssid;
        int rssi         = _ap_info[i].rssi;

        // Skip empty SSID
        if (ssid.empty()) {
            continue;
        }

        // Add to ap_list
        scanResult.push_back(std::make_pair(rssi, ssid));
    }

    // Sort ap_list by RSSI (from strongest to weakest)
    std::sort(scanResult.begin(), scanResult.end(),
              [](const std::pair<int, std::string>& a, const std::pair<int, std::string>& b) {
                  return a.first > b.first;  // Higher RSSI first
              });

    mclog::tagInfo(_tag, "wifi scan completed, found {} APs", scanResult.size());
}

static EventGroupHandle_t s_wifi_event_group = NULL;
static const int WIFI_CONNECTED_BIT          = BIT0;
static const int WIFI_DISCONNECTED_BIT       = BIT1;
static const int WIFI_FAIL_BIT               = BIT2;
static const int WIFI_STARTED_BIT            = BIT3;

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    const char* TAG = "wifi";

    // Wifi started
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_STARTED_BIT);
    }

    // Disconnected
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(s_wifi_event_group, WIFI_DISCONNECTED_BIT);
        xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
    }

    // Connected
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void Hal::wifiInit()
{
    mclog::tagInfo(_tag, "wifi init");

    if (_is_wifi_inited) {
        return;
    }

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t* sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    if (!s_wifi_event_group) {
        s_wifi_event_group = xEventGroupCreate();
    }

    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, nullptr, nullptr));
    ESP_ERROR_CHECK(
        esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, nullptr, nullptr));

    ESP_ERROR_CHECK(esp_wifi_start());
    _is_wifi_inited = true;
}

void Hal::wifiDeinit()
{
    mclog::tagInfo(_tag, "wifi deinit");

    if (!_is_wifi_inited) {
        return;
    }

    esp_wifi_stop();
    esp_wifi_deinit();
    _is_wifi_inited = false;
}

bool Hal::wifiConnect(const std::string& ssid, const std::string& password)
{
    mclog::tagInfo(_tag, "wifi connect to ssid: {} password: {}", ssid, password);

    if (!_is_wifi_inited) {
        wifiInit();
    }

    wifiDisconnect();

    // Hold until wifi started
    xEventGroupWaitBits(s_wifi_event_group, WIFI_STARTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(3000));

    // Reset event status
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | WIFI_DISCONNECTED_BIT);

    // Set Wi-Fi config
    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, ssid.c_str(), sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, password.c_str(), sizeof(wifi_config.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_connect());

    // Wait for connection result
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT, pdTRUE, pdFALSE,
                                           pdMS_TO_TICKS(10000));

    if (bits & WIFI_CONNECTED_BIT) {
        mclog::tagInfo(_tag, "connected to SSID: {}", ssid);
        _is_wifi_connected = true;
        start_sntp();
        return true;
    } else if (bits & WIFI_FAIL_BIT) {
        mclog::tagError(_tag, "failed to connect to SSID: {}", ssid);
        return false;
    } else {
        mclog::tagError(_tag, "wifi connect timeout");
        return false;
    }
}

void Hal::wifiDisconnect()
{
    mclog::tagInfo(_tag, "wifi disconnect");

    if (!_is_wifi_inited) {
        return;
    }

    if (!_is_wifi_connected) {
        return;
    }

    // Hold until wifi started
    xEventGroupWaitBits(s_wifi_event_group, WIFI_STARTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(3000));

    // Disconnect old connection
    ESP_ERROR_CHECK(esp_wifi_disconnect());

    // Wait for disconnect result
    xEventGroupWaitBits(s_wifi_event_group, WIFI_DISCONNECTED_BIT, pdFALSE, pdFALSE, pdMS_TO_TICKS(5000));

    // Reset event status
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT | WIFI_DISCONNECTED_BIT);

    stop_sntp();

    _is_wifi_connected = false;
}

static void sntp_sync_notification_cb(struct timeval* tv)
{
    mclog::tagInfo("hal", "sntp time synced: {}", tv->tv_sec);
}

void Hal::start_sntp()
{
    mclog::tagInfo(_tag, "start sntp");

    if (!_is_wifi_connected) {
        mclog::tagError(_tag, "wifi not connected");
        return;
    }

    // Set timezone to Adelaide, Australia (ACST/ACDT with DST rules)
    setenv("TZ", "ACST-9:30ACDT-10:30,M10.1.0,M4.1.0/3", 1);
    tzset();

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    sntp_set_time_sync_notification_cb(sntp_sync_notification_cb);
    esp_sntp_init();
}

void Hal::stop_sntp()
{
    mclog::tagInfo(_tag, "stop sntp");

    if (!_is_wifi_connected) {
        mclog::tagError(_tag, "wifi not connected");
        return;
    }

    esp_sntp_stop();
}

/* -------------------------------------------------------------------------- */
/*                                   EspNow                                   */
/* -------------------------------------------------------------------------- */
// https://github.com/espressif/esp-now/blob/master/examples/get-started/main/app_main.c
#include <esp_mac.h>
#include <espnow.h>
#include <espnow_storage.h>
#include <espnow_utils.h>
#include <esp_check.h>

static std::string _espnow_received_data;
static esp_err_t _handle_espnow_received(uint8_t* src_addr, void* data, size_t size, wifi_pkt_rx_ctrl_t* rx_ctrl)
{
    const char* TAG = "espnow";

    ESP_PARAM_CHECK(src_addr);
    ESP_PARAM_CHECK(data);
    ESP_PARAM_CHECK(size);
    ESP_PARAM_CHECK(rx_ctrl);

    static uint32_t count = 0;

    ESP_LOGI(TAG, "espnow_recv, <%" PRIu32 "> [" MACSTR "][%d][%d][%u]: %.*s", count++, MAC2STR(src_addr),
             rx_ctrl->channel, rx_ctrl->rssi, size, size, (char*)data);

    _espnow_received_data = std::string((char*)data, size);

    return ESP_OK;
}

void Hal::espNowInit()
{
    mclog::tagInfo(_tag, "esp now init");

    if (!_is_wifi_inited) {
        wifiInit();
    }

    if (_is_wifi_connected) {
        wifiDisconnect();
        espNowDeinit();
    }

    if (_is_esp_now_inited) {
        mclog::tagInfo(_tag, "esp now already inited");
        return;
    }

    // espnow_storage_init();

    espnow_config_t espnow_config = ESPNOW_INIT_CONFIG_DEFAULT();
    espnow_init(&espnow_config);
    espnow_set_config_for_data_type(ESPNOW_DATA_TYPE_DATA, true, _handle_espnow_received);

    _is_esp_now_inited = true;
}

void Hal::espNowDeinit()
{
    mclog::tagInfo(_tag, "esp now deinit");

    if (!_is_esp_now_inited) {
        mclog::tagInfo(_tag, "esp now not inited");
        return;
    }

    espnow_deinit();

    _is_esp_now_inited = false;
}

void Hal::espNowSend(const std::string& data)
{
    mclog::tagInfo(_tag, "esp now send: {}", data);

    if (!_is_esp_now_inited) {
        mclog::tagError(_tag, "esp now not inited");
        return;
    }

    espnow_frame_head_t frame_head = ESPNOW_FRAME_CONFIG_DEFAULT();
    auto ret = espnow_send(ESPNOW_DATA_TYPE_DATA, ESPNOW_ADDR_BROADCAST, data.c_str(), data.size(), &frame_head,
                           portMAX_DELAY);

    if (ret != ESP_OK) {
        mclog::tagError(_tag, "failed to send esp now: {}", esp_err_to_name(ret));
    }
}

bool Hal::espNowAvailable()
{
    return _espnow_received_data.size() > 0;
}

const std::string& Hal::espNowGetReceivedData()
{
    return _espnow_received_data;
}

void Hal::espNowClearReceivedData()
{
    _espnow_received_data.clear();
}

/* -------------------------------------------------------------------------- */
/*                                     IR                                     */
/* -------------------------------------------------------------------------- */
#include "utils/ir_nec/ir_helper.h"

void Hal::irInit()
{
    mclog::tagInfo(_tag, "ir init");

    if (_is_ir_inited) {
        mclog::tagInfo(_tag, "ir already inited");
        return;
    }

    ir_helper_init((gpio_num_t)HAL_PIN_IR_TX);

    _is_ir_inited = true;
}

void Hal::irSend(uint8_t addr, uint8_t cmd)
{
    mclog::tagInfo(_tag, "ir send: addr: {:02X}, cmd: {:02X}", addr, cmd);

    if (!_is_ir_inited) {
        mclog::tagError(_tag, "ir not inited");
        return;
    }

    ir_helper_send(addr, cmd);
}

/* -------------------------------------------------------------------------- */
/*                                     BLE                                    */
/* -------------------------------------------------------------------------- */
#include "utils/ble_hid_device/ble_hid_device_helper.h"

void Hal::bleKeyboardInit()
{
    if (_is_ble_keyboard_inited) {
        mclog::tagWarn(_tag, "ble keyboard already initialized");
        return;
    }

    mclog::tagInfo(_tag, "ble keyboard init");

    // Initialize BLE HID device
    ble_hid_device_helper_init();

    // Register keyboard event callback to automatically forward keys
    _ble_keyboard_event_slot_id = keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) { handle_ble_keyboard_event(keyEvent); });

    _is_ble_keyboard_inited = true;
    mclog::tagInfo(_tag, "ble keyboard init done, auto-forwarding enabled");
}

bool Hal::bleKeyboardIsConnected() const
{
    if (!_is_ble_keyboard_inited) {
        return false;
    }

    auto state = ble_hid_device_helper_get_state();
    return (state == BLE_HID_DEVICE_STATE_CONNECTED);
}

void Hal::handle_ble_keyboard_event(const Keyboard::KeyEvent_t& keyEvent)
{
    // Only forward if BLE keyboard is connected
    if (!bleKeyboardIsConnected()) {
        return;
    }

    // Create HID buffer (8 bytes: modifier, reserved, keycode1-6)
    uint8_t buffer[8] = {0};

    // Handle key press/release
    if (keyEvent.state) {
        // Get current modifier state from keyboard
        uint8_t modifierMask = keyboard.getModifierMask();

        // Set modifier byte (physical modifiers + any firmware-injected ones, e.g. Fn+alpha -> LSHIFT)
        buffer[0] = modifierMask | keyEvent.extraModifiers;

        // For modifier keys themselves, don't set keycode
        if (keyEvent.isModifier) {
            buffer[2] = 0;  // No keycode for pure modifier keys
        } else {
            buffer[2] = keyEvent.keyCode;
        }

        // Send key press
        ble_hid_device_helper_send(buffer);
        mclog::tagDebug(_tag, "ble keyboard sent key: {} (code: {}, modifier: {:08b})",
                        keyEvent.keyName ? keyEvent.keyName : "special", (int)keyEvent.keyCode, modifierMask);
    } else {
        // Key released - always preserve current modifier state so held modifiers (e.g. ALT) stay active
        buffer[0] = keyboard.getModifierMask();
        buffer[2] = 0;
        ble_hid_device_helper_send(buffer);
        mclog::tagDebug(_tag, "ble keyboard key released (modifier: {:08b})", buffer[0]);
    }
}

/* -------------------------------------------------------------------------- */
/*                                     USB                                    */
/* -------------------------------------------------------------------------- */
// https://github.com/espressif/esp-idf/blob/v5.4.2/examples/peripherals/usb/device/tusb_hid
#include "utils/tusb_hid_device/tusb_hid_device_helper.h"

void Hal::usbKeyboardInit()
{
    if (_is_usb_keyboard_inited) {
        mclog::tagWarn(_tag, "usb keyboard already initialized");
        return;
    }

    mclog::tagInfo(_tag, "usb keyboard init");

    delay(200);

    tusb_hid_device_helper_init();

    _usb_keyboard_event_slot_id = keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) { handle_usb_keyboard_event(keyEvent); });

    _is_usb_keyboard_inited = true;
}

bool Hal::usbKeyboardIsConnected() const
{
    if (!_is_usb_keyboard_inited) {
        return false;
    }

    return tusb_hid_device_helper_is_mounted();
}

void Hal::handle_usb_keyboard_event(const Keyboard::KeyEvent_t& keyEvent)
{
    // Only forward if USB keyboard is connected
    if (!usbKeyboardIsConnected()) {
        return;
    }

    // Handle key press/release
    if (keyEvent.state) {
        uint8_t mod = GetHAL().keyboard.getModifierMask() | keyEvent.extraModifiers;
        if (keyEvent.isModifier) {
            tusb_hid_device_helper_report(mod, NULL);
        } else {
            uint8_t keycode[6] = {keyEvent.keyCode};
            tusb_hid_device_helper_report(mod, keycode);
        }
        mclog::tagDebug(_tag, "usb keyboard sent key: {} (code: {}, modifier: {:08b})",
                        keyEvent.keyName ? keyEvent.keyName : "special", (int)keyEvent.keyCode, mod);
    } else {
        tusb_hid_device_helper_report(GetHAL().keyboard.getModifierMask(), NULL);
        mclog::tagDebug(_tag, "usb keyboard key released");
    }
}

/* -------------------------------------------------------------------------- */
/*                                     SPI                                    */
/* -------------------------------------------------------------------------- */
#include <driver/spi_master.h>
#include <driver/sdspi_host.h>
#include <driver/sdmmc_host.h>

static bool _spi_bus_initialized = false;

void Hal::spi_init()
{
    mclog::tagInfo(_tag, "spi init");

    esp_err_t ret;

    // spi_host_device_t host_id = SPI2_HOST;
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    spi_bus_config_t bus_cfg = {
        .mosi_io_num     = HAL_PIN_SPI_MOSI,
        .miso_io_num     = HAL_PIN_SPI_MISO,
        .sclk_io_num     = HAL_PIN_SPI_SCLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = 4000,
    };

    // Initialize SPI bus only if not already initialized
    if (!_spi_bus_initialized) {
        ret = spi_bus_initialize((spi_host_device_t)host.slot, &bus_cfg, SDSPI_DEFAULT_DMA);
        if (ret != ESP_OK) {
            mclog::tagError(_tag, "failed to initialize SPI bus");
            return;
        }
        _spi_bus_initialized = true;
        mclog::tagInfo(_tag, "spi bus initialized");
    } else {
        mclog::tagWarn(_tag, "spi bus already initialized, reusing");
    }
}

/* -------------------------------------------------------------------------- */
/*                                   SD Card                                  */
/* -------------------------------------------------------------------------- */
// https://github.com/espressif/esp-idf/blob/v5.3.3/examples/storage/sd_card/sdspi
// https://github.com/m5stack/M5PaperS3-UserDemo/blob/main/main/hal/hal.h
#include <driver/spi_master.h>
#include <driver/sdspi_host.h>
#include <driver/sdmmc_host.h>
#include <sdmmc_cmd.h>
#include <esp_vfs_fat.h>
#include <cstdlib>

#define MOUNT_POINT "/sdcard"

static sdmmc_card_t* _sd_card = nullptr;
static sdspi_dev_handle_t _msc_sd_slot = -1;
static sdmmc_host_t _msc_sd_host = SDSPI_HOST_DEFAULT();

void Hal::sd_card_init()
{
    mclog::tagInfo(_tag, "sd card init");

    if (!_spi_bus_initialized) {
        spi_init();
    }

    // If already mounted successfully, return
    if (_is_sd_card_mounted) {
        mclog::tagInfo(_tag, "sd card already mounted");
        return;
    }

    esp_err_t ret;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();

    // Options for mounting the filesystem
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false, .max_files = 5, .allocation_unit_size = 16 * 1024,
        .disk_status_check_enable = false, .use_one_fat = false};

    const char mount_point[] = MOUNT_POINT;
    mclog::tagInfo(_tag, "initializing SD card");

    // Initialize SD card slot
    sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_config.gpio_cs               = HAL_PIN_SD_CARD_CS;
    slot_config.host_id               = (spi_host_device_t)host.slot;

    mclog::tagInfo(_tag, "mounting filesystem");
    ret = esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config, &mount_config, &_sd_card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            mclog::tagError(_tag, "failed to mount filesystem");
        } else {
            mclog::tagError(_tag, "failed to initialize the card, make sure SD card lines have pull-up resistors");
        }

        // Don't clean up SPI bus on failure - leave it for retry
        mclog::tagInfo(_tag, "sd card init failed, but spi bus remains initialized for retry");
        return;
    }

    mclog::tagInfo(_tag, "filesystem mounted successfully");

    sdmmc_card_print_info(stdout, _sd_card);

    _is_sd_card_mounted = true;
}

Hal::SdCardProbeResult_t Hal::sdCardProbe()
{
    SdCardProbeResult_t result;

    if (_sd_card_usb_export) {
        result.size = "USB Disk Mode";
        result.type = "SD card owned by USB host";
        return result;
    }

    if (!_is_sd_card_mounted) {
        sd_card_init();
        if (!_is_sd_card_mounted) {
            result.is_mounted = false;
            result.size       = "Not Found";
            return result;
        }
    }

    result.is_mounted = true;

    // Try write to sd card
    FILE* fp = fopen(MOUNT_POINT "/test.txt", "w");
    if (fp) {
        fwrite("Hello, World!", 1, 13, fp);
        fclose(fp);

        result.size =
            fmt::format("Size: {:.1f} GB",
                        ((float)((uint64_t)_sd_card->csd.capacity) * _sd_card->csd.sector_size) / (1024 * 1024 * 1024));
    } else {
        result.size = "Write Failed";
    }

    result.type = "Type: ";
    if (_sd_card->is_sdio) {
        result.type += "SDIO";
    } else if (_sd_card->is_mmc) {
        result.type += "MMC";
    } else {
        result.type += (_sd_card->ocr & (1 << 30)) ? "SDHC/SDXC" : "SDSC";
    }

    result.name = fmt::format("Name: {}", std::string(_sd_card->cid.name));

    return result;
}

bool Hal::sdCardEnableUsbExport()
{
    if (_sd_card_usb_export) return true;

    // The raw SDSPI card handle and the TinyUSB MSC storage class can only be
    // set up once (tinyusb_msc_storage_init_sdmmc() asserts if called again
    // while a storage handle already exists). Do that one-time setup only on
    // the first export; later toggles just hand the existing storage to the
    // host via tusb_hid_device_helper_init_msc() below.
    if (!_sd_card_msc_ready) {
        sd_card_init();
        if (!_is_sd_card_mounted || _sd_card == nullptr) {
            mclog::tagError(_tag, "cannot enable USB disk: SD card is not mounted");
            return false;
        }

        // Release the POSIX/FAT mount before handing the media to MSC. The unmount
        // also releases the old card handle, so initialize a fresh raw card handle
        // for TinyUSB.
        if (esp_vfs_fat_sdcard_unmount(MOUNT_POINT, _sd_card) != ESP_OK) {
            mclog::tagError(_tag, "cannot enable USB disk: SD unmount failed");
            return false;
        }
        _sd_card = nullptr;
        _is_sd_card_mounted = false;

        // The host must be initialized before a device can be attached to it.
        _msc_sd_host = SDSPI_HOST_DEFAULT();
        if (_msc_sd_host.init() != ESP_OK) {
            mclog::tagError(_tag, "cannot enable USB disk: SDSPI host init failed");
            return false;
        }

        sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
        slot_config.gpio_cs = HAL_PIN_SD_CARD_CS;
        slot_config.host_id = (spi_host_device_t)_msc_sd_host.slot;
        if (sdspi_host_init_device(&slot_config, &_msc_sd_slot) != ESP_OK) {
            mclog::tagError(_tag, "cannot enable USB disk: SDSPI device init failed");
            _msc_sd_host.deinit();
            return false;
        }
        _msc_sd_host.slot = _msc_sd_slot;
        _sd_card = static_cast<sdmmc_card_t*>(std::calloc(1, sizeof(sdmmc_card_t)));
        if (_sd_card == nullptr || sdmmc_card_init(&_msc_sd_host, _sd_card) != ESP_OK) {
            mclog::tagError(_tag, "cannot enable USB disk: raw SD init failed");
            sdspi_host_remove_device(_msc_sd_slot);
            _msc_sd_host.deinit();
            std::free(_sd_card);
            _sd_card = nullptr;
            return false;
        }

        _sd_card_msc_ready = true;
    }

    if (!tusb_hid_device_helper_init_msc(_sd_card)) {
        mclog::tagError(_tag, "cannot enable USB disk: TinyUSB MSC init failed");
        return false;
    }

    _sd_card_usb_export = true;
    mclog::tagInfo(_tag, "USB disk mode enabled");
    return true;
}

bool Hal::sdCardDisableUsbExport()
{
    if (!_sd_card_usb_export) return _is_sd_card_mounted;

    if (!tusb_hid_device_helper_eject_msc()) {
        mclog::tagError(_tag, "cannot leave USB disk mode: eject failed");
        return false;
    }

    tusb_hid_device_helper_stop_msc();
    if (_msc_sd_slot >= 0) {
        sdspi_host_remove_device(_msc_sd_slot);
        _msc_sd_slot = -1;
    }
    _msc_sd_host.deinit();
    std::free(_sd_card);
    _sd_card = nullptr;
    _sd_card_msc_ready = false;
    _sd_card_usb_export = false;
    _is_sd_card_mounted = false;
    sd_card_init();
    if (!_is_sd_card_mounted) {
        mclog::tagError(_tag, "USB disk returned but SD remount failed");
        return false;
    }
    mclog::tagInfo(_tag, "USB disk mode disabled");
    return true;
}

extern "C" void hal_usb_msc_mount_changed(bool is_mounted)
{
    GetHAL().onUsbMscMountChanged(is_mounted);
}

void Hal::onUsbMscMountChanged(bool is_mounted)
{
    _sd_card_usb_export = !is_mounted;
    _is_sd_card_mounted = is_mounted;
    if (is_mounted) {
        mclog::tagInfo(_tag, "USB disk ejected; SD card returned to firmware");
    } else {
        mclog::tagInfo(_tag, "SD card is owned by USB host");
    }
}

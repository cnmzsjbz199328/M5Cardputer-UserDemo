/*
 * SPDX-FileCopyrightText: 2022-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Unlicense OR CC0-1.0
 */

#include "tusb_hid_device_helper.h"

#include <stdlib.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "tinyusb.h"
#include "tusb_msc_storage.h"
#include "class/hid/hid_device.h"
#include "class/msc/msc_device.h"
#include "driver/gpio.h"

#define APP_BUTTON (GPIO_NUM_0)  // Use BOOT signal by default
static const char *TAG = "tusb_hid";
static bool s_msc_initialized = false;

extern void hal_usb_msc_mount_changed(bool is_mounted);

static void msc_mount_changed_cb(tinyusb_msc_event_t *event)
{
    if (event != NULL && event->type == TINYUSB_MSC_EVENT_MOUNT_CHANGED) {
        hal_usb_msc_mount_changed(event->mount_changed_data.is_mounted);
    }
}

/************* TinyUSB descriptors ****************/

#define TUSB_DESC_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN + TUD_MSC_DESC_LEN)

/**
 * @brief HID report descriptor
 *
 * In this example we implement Keyboard + Mouse HID device,
 * so we must define both report descriptors
 */
const uint8_t hid_report_descriptor[] = {TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(HID_ITF_PROTOCOL_KEYBOARD)),
                                         TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(HID_ITF_PROTOCOL_MOUSE))};

/**
 * @brief String descriptor
 */
const char *hid_string_descriptor[6] = {
    // array of pointer to string descriptors
    (char[]){0x09, 0x04},     // 0: is supported language is English (0x0409)
    "M5Stack",                // 1: Manufacturer
    "CardputerADV",           // 2: Product
    "123456",                 // 3: Serials, should use chip ID
    "HID + SD Card",          // 4: HID
    "SD Card MSC",            // 5: MSC
};

/**
 * @brief Configuration descriptor
 *
 * This is a simple configuration descriptor that defines 1 configuration and 1 HID interface
 */
static const uint8_t hid_configuration_descriptor[] = {
    // Configuration number, interface count, string index, total length, attribute, power in mA
    TUD_CONFIG_DESCRIPTOR(1, 2, 0, TUSB_DESC_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Interface number, string index, boot protocol, report descriptor len, EP In address, size & polling interval
    TUD_HID_DESCRIPTOR(0, 4, false, sizeof(hid_report_descriptor), 0x81, 16, 10),
    TUD_MSC_DESCRIPTOR(1, 5, 0x02, 0x83, 64),
};

/********* TinyUSB HID callbacks ***************/

// Invoked when received GET HID REPORT DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance)
{
    // We use only one interface and one HID report descriptor, so we can ignore parameter 'instance'
    return hid_report_descriptor;
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen)
{
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;

    return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize)
{
}

/********* Application ***************/

typedef enum {
    MOUSE_DIR_RIGHT,
    MOUSE_DIR_DOWN,
    MOUSE_DIR_LEFT,
    MOUSE_DIR_UP,
    MOUSE_DIR_MAX,
} mouse_dir_t;

#define DISTANCE_MAX 125
#define DELTA_SCALAR 5

#if 0
static void mouse_draw_square_next_delta(int8_t *delta_x_ret, int8_t *delta_y_ret)
{
    static mouse_dir_t cur_dir = MOUSE_DIR_RIGHT;
    static uint32_t distance   = 0;

    // Calculate next delta
    if (cur_dir == MOUSE_DIR_RIGHT) {
        *delta_x_ret = DELTA_SCALAR;
        *delta_y_ret = 0;
    } else if (cur_dir == MOUSE_DIR_DOWN) {
        *delta_x_ret = 0;
        *delta_y_ret = DELTA_SCALAR;
    } else if (cur_dir == MOUSE_DIR_LEFT) {
        *delta_x_ret = -DELTA_SCALAR;
        *delta_y_ret = 0;
    } else if (cur_dir == MOUSE_DIR_UP) {
        *delta_x_ret = 0;
        *delta_y_ret = -DELTA_SCALAR;
    }

    // Update cumulative distance for current direction
    distance += DELTA_SCALAR;
    // Check if we need to change direction
    if (distance >= DISTANCE_MAX) {
        distance = 0;
        cur_dir++;
        if (cur_dir == MOUSE_DIR_MAX) {
            cur_dir = 0;
        }
    }
}

static void app_send_hid_demo(void)
{
    // Keyboard output: Send key 'a/A' pressed and released
    ESP_LOGI(TAG, "Sending Keyboard report");
    uint8_t keycode[6] = {HID_KEY_A};
    tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, keycode);
    vTaskDelay(pdMS_TO_TICKS(50));
    tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, 0, NULL);

    // Mouse output: Move mouse cursor in square trajectory
    ESP_LOGI(TAG, "Sending Mouse report");
    int8_t delta_x;
    int8_t delta_y;
    for (int i = 0; i < (DISTANCE_MAX / DELTA_SCALAR) * 4; i++) {
        // Get the next x and y delta in the draw square pattern
        mouse_draw_square_next_delta(&delta_x, &delta_y);
        tud_hid_mouse_report(HID_ITF_PROTOCOL_MOUSE, 0x00, delta_x, delta_y, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
#endif

static bool s_driver_installed = false;

// Installs the TinyUSB device stack. TinyUSB can only be installed once for
// the lifetime of the firmware (a second call to tinyusb_driver_install()
// re-acquires the USB PHY/OTG peripheral and the TinyUSB task while they are
// already owned by the first install, which fails outright). Callers must
// therefore always route through this guarded helper instead of calling
// tinyusb_driver_install() directly.
static bool _install_driver(void)
{
    if (s_driver_installed) return true;

    // // Initialize button that will trigger HID reports
    // const gpio_config_t boot_button_config = {
    //     .pin_bit_mask = BIT64(APP_BUTTON),
    //     .mode         = GPIO_MODE_INPUT,
    //     .intr_type    = GPIO_INTR_DISABLE,
    //     .pull_up_en   = true,
    //     .pull_down_en = false,
    // };
    // ESP_ERROR_CHECK(gpio_config(&boot_button_config));

    ESP_LOGI(TAG, "USB initialization");
    const tinyusb_config_t tusb_cfg = {
        .device_descriptor       = NULL,
        .string_descriptor       = hid_string_descriptor,
        .string_descriptor_count = sizeof(hid_string_descriptor) / sizeof(hid_string_descriptor[0]),
        .external_phy            = false,
#if (TUD_OPT_HIGH_SPEED)
        .fs_configuration_descriptor =
            hid_configuration_descriptor,  // HID configuration descriptor for full-speed and high-speed are the same
        .hs_configuration_descriptor = hid_configuration_descriptor,
        .qualifier_descriptor        = NULL,
#else
        .configuration_descriptor = hid_configuration_descriptor,
#endif  // TUD_OPT_HIGH_SPEED
    };

    esp_err_t err = tinyusb_driver_install(&tusb_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "USB initialization failed: %s", esp_err_to_name(err));
        return false;
    }
    s_driver_installed = true;
    ESP_LOGI(TAG, "USB initialization DONE");
    return true;

    // while (1) {
    //     if (tud_mounted()) {
    //         static bool send_hid_data = true;
    //         if (send_hid_data) {
    //             app_send_hid_demo();
    //         }
    //         send_hid_data = !gpio_get_level(APP_BUTTON);
    //     }
    //     vTaskDelay(pdMS_TO_TICKS(100));
    // }
}

void tusb_hid_device_helper_init(void)
{
    (void)_install_driver();
}

bool tusb_hid_device_helper_init_msc(sdmmc_card_t *card)
{
    if (!_install_driver()) return false;

    // tinyusb_msc_storage_init_sdmmc() asserts if called while a storage
    // handle already exists, so it may only run once for the given card.
    // Re-enabling USB export after an eject just hands the already
    // registered storage back to the host.
    if (!s_msc_initialized) {
        const tinyusb_msc_sdmmc_config_t msc_cfg = {
            .card = card,
            .callback_mount_changed = msc_mount_changed_cb,
            .callback_premount_changed = NULL,
            .mount_config = {
                .format_if_mount_failed = false,
                .max_files = 5,
                .allocation_unit_size = 16 * 1024,
                .disk_status_check_enable = false,
                .use_one_fat = false,
            },
        };
        esp_err_t err = tinyusb_msc_storage_init_sdmmc(&msc_cfg);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "MSC storage init failed: %s", esp_err_to_name(err));
            return false;
        }
        s_msc_initialized = true;
    }

    // Hand the storage to the USB host. tinyusb_msc_storage_mount() (used by
    // the eject path) does the opposite: it claims the storage for the
    // firmware and blocks host access, per its documented contract.
    esp_err_t err = tinyusb_msc_storage_unmount();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "MSC unmount (hand to host) failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool tusb_hid_device_helper_eject_msc(void)
{
    if (!s_msc_initialized) return false;
    tud_disconnect();
    for (int i = 0; i < 30 && tinyusb_msc_storage_in_use_by_usb_host(); ++i) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    return true;
}

void tusb_hid_device_helper_stop_msc(void)
{
    if (!s_msc_initialized) return;
    (void)tinyusb_msc_storage_unmount();
    tinyusb_msc_storage_deinit();
    s_msc_initialized = false;
}

void tusb_hid_device_helper_report(uint8_t modifier, uint8_t *keycode)
{
    tud_hid_keyboard_report(HID_ITF_PROTOCOL_KEYBOARD, modifier, keycode);
}

bool tusb_hid_device_helper_is_mounted(void)
{
    return tud_mounted();
}

/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "ble_ecp_client.h"
#include "ecosystem_protocol.h"

#include <cstdio>
#include <cstring>

#include "esp_log.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_gap.h"
#include "host/ble_gattc.h"
#include "host/ble_uuid.h"

namespace {

const char* TAG = "ecp_client";

struct EcpUuids {
    ble_uuid128_t service;
    ble_uuid128_t info;
    ble_uuid128_t command;
    ble_uuid128_t response;
    ble_uuid128_t state;
};

// Parses a canonical "xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx" UUID string into
// a NimBLE ble_uuid128_t. NimBLE stores 128-bit UUID bytes little-endian
// (the reverse of the string's big-endian byte order), matching how
// BLE_UUID128_INIT literals are written throughout the NimBLE examples.
bool parse_uuid128(const char* str, ble_uuid128_t* out)
{
    uint8_t bytes[16];
    size_t byte_idx = 0;
    int hi           = -1;
    for (const char* p = str; *p != '\0' && byte_idx < 16; ++p) {
        char c = *p;
        int v;
        if (c >= '0' && c <= '9')
            v = c - '0';
        else if (c >= 'a' && c <= 'f')
            v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F')
            v = c - 'A' + 10;
        else
            continue;  // skip '-'
        if (hi < 0) {
            hi = v;
        } else {
            bytes[byte_idx++] = static_cast<uint8_t>((hi << 4) | v);
            hi                = -1;
        }
    }
    if (byte_idx != 16) return false;

    out->u.type = BLE_UUID_TYPE_128;
    for (size_t i = 0; i < 16; ++i) {
        out->value[i] = bytes[15 - i];
    }
    return true;
}

EcpUuids s_uuids;
bool s_uuids_ready = false;

EcpClientState_t s_state = ECP_CLIENT_STATE_IDLE;
char s_status_text[32]   = "idle";
char s_peer_label[24]    = "";

EcpClientDevice_t s_devices[ECP_CLIENT_MAX_DEVICES];
size_t s_device_count = 0;

uint16_t s_conn_handle      = BLE_HS_CONN_HANDLE_NONE;
uint16_t s_svc_start_handle = 0;
uint16_t s_svc_end_handle   = 0;
uint16_t s_info_handle      = 0;
uint16_t s_command_handle   = 0;
uint16_t s_response_handle  = 0;
uint16_t s_state_handle     = 0;

void set_status(const char* text)
{
    snprintf(s_status_text, sizeof(s_status_text), "%s", text);
}

void reset_link()
{
    s_conn_handle       = BLE_HS_CONN_HANDLE_NONE;
    s_svc_start_handle  = 0;
    s_svc_end_handle    = 0;
    s_info_handle       = 0;
    s_command_handle    = 0;
    s_response_handle   = 0;
    s_state_handle      = 0;
    s_peer_label[0]     = '\0';
}

int gap_event_handler(struct ble_gap_event* event, void* arg);

void start_scan_locked()
{
    s_device_count = 0;
    s_state        = ECP_CLIENT_STATE_SCANNING;
    set_status("scanning");

    struct ble_gap_disc_params params;
    memset(&params, 0, sizeof(params));
    params.passive           = 0;
    params.filter_duplicates = 1;
    params.itvl              = 0x50;
    params.window            = 0x30;

    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &params, gap_event_handler, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_disc failed rc=%d", rc);
        s_state = ECP_CLIENT_STATE_ERROR;
        set_status("scan failed");
    }
}

bool adv_matches_ecp_service(const uint8_t* data, uint8_t length)
{
    struct ble_hs_adv_fields fields;
    if (ble_hs_adv_parse_fields(&fields, const_cast<uint8_t*>(data), length) != 0) return false;
    for (int i = 0; i < fields.num_uuids128; ++i) {
        if (ble_uuid_cmp(&fields.uuids128[i].u, &s_uuids.service.u) == 0) return true;
    }
    return false;
}

void addr_to_str(const ble_addr_t* addr, char* out, size_t out_size)
{
    snprintf(out, out_size, "%02x:%02x:%02x:%02x:%02x:%02x", addr->val[5], addr->val[4], addr->val[3], addr->val[2],
              addr->val[1], addr->val[0]);
}

void handle_disc_event(struct ble_gap_disc_desc* disc)
{
    if (!adv_matches_ecp_service(disc->data, disc->length_data)) return;

    char address[18];
    addr_to_str(&disc->addr, address, sizeof(address));

    struct ble_hs_adv_fields fields;
    char label[24] = "";
    if (ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) == 0 && fields.name != nullptr &&
        fields.name_len > 0) {
        size_t n = fields.name_len < sizeof(label) - 1 ? fields.name_len : sizeof(label) - 1;
        memcpy(label, fields.name, n);
        label[n] = '\0';
    } else {
        snprintf(label, sizeof(label), "%s", address);
    }

    for (size_t i = 0; i < s_device_count; ++i) {
        if (strcmp(s_devices[i].address, address) == 0) {
            s_devices[i].rssi = static_cast<int8_t>(disc->rssi);
            return;
        }
    }
    if (s_device_count >= ECP_CLIENT_MAX_DEVICES) return;

    EcpClientDevice_t& dev = s_devices[s_device_count++];
    snprintf(dev.address, sizeof(dev.address), "%s", address);
    dev.address_type = disc->addr.type;
    snprintf(dev.label, sizeof(dev.label), "%s", label);
    dev.rssi = static_cast<int8_t>(disc->rssi);
}

int chr_disc_cb(uint16_t conn_handle, const struct ble_gatt_error* error, const struct ble_gatt_chr* chr, void* arg)
{
    (void)arg;
    if (conn_handle != s_conn_handle) return 0;

    if (error->status == 0 && chr != nullptr) {
        if (ble_uuid_cmp(&chr->uuid.u, &s_uuids.info.u) == 0)
            s_info_handle = chr->val_handle;
        else if (ble_uuid_cmp(&chr->uuid.u, &s_uuids.command.u) == 0)
            s_command_handle = chr->val_handle;
        else if (ble_uuid_cmp(&chr->uuid.u, &s_uuids.response.u) == 0)
            s_response_handle = chr->val_handle;
        else if (ble_uuid_cmp(&chr->uuid.u, &s_uuids.state.u) == 0)
            s_state_handle = chr->val_handle;
        return 0;
    }

    // Any non-zero status here (typically BLE_HS_EDONE) marks the end of
    // discovery, successful or not.
    if (s_info_handle && s_command_handle && s_response_handle && s_state_handle) {
        s_state = ECP_CLIENT_STATE_CONNECTED;
        set_status("connected");
        ESP_LOGI(TAG, "ECP characteristics discovered, info=%u command=%u response=%u state=%u", s_info_handle,
                 s_command_handle, s_response_handle, s_state_handle);
    } else {
        ESP_LOGW(TAG, "ECP characteristic discovery incomplete");
        set_status("protocol incomplete");
        s_state = ECP_CLIENT_STATE_ERROR;
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    return 0;
}

int svc_disc_cb(uint16_t conn_handle, const struct ble_gatt_error* error, const struct ble_gatt_svc* service,
                void* arg)
{
    (void)arg;
    if (conn_handle != s_conn_handle) return 0;

    if (error->status == 0 && service != nullptr) {
        s_svc_start_handle = service->start_handle;
        s_svc_end_handle   = service->end_handle;
        return 0;
    }

    if (s_svc_start_handle == 0) {
        ESP_LOGW(TAG, "ECP service not found on peer");
        set_status("protocol incomplete");
        s_state = ECP_CLIENT_STATE_ERROR;
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        return 0;
    }

    int rc = ble_gattc_disc_all_chrs(s_conn_handle, s_svc_start_handle, s_svc_end_handle, chr_disc_cb, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gattc_disc_all_chrs failed rc=%d", rc);
        set_status("protocol incomplete");
        s_state = ECP_CLIENT_STATE_ERROR;
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    return 0;
}

int gap_event_handler(struct ble_gap_event* event, void* arg)
{
    (void)arg;
    switch (event->type) {
        case BLE_GAP_EVENT_DISC:
            handle_disc_event(&event->disc);
            return 0;

        case BLE_GAP_EVENT_CONNECT:
            if (event->connect.status == 0) {
                s_conn_handle = event->connect.conn_handle;
                s_state       = ECP_CLIENT_STATE_CONNECTING;
                set_status("discovering");
                int rc = ble_gattc_disc_svc_by_uuid(s_conn_handle, &s_uuids.service.u, svc_disc_cb, nullptr);
                if (rc != 0) {
                    ESP_LOGE(TAG, "ble_gattc_disc_svc_by_uuid failed rc=%d", rc);
                    set_status("connect failed");
                    s_state = ECP_CLIENT_STATE_ERROR;
                    ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
                }
            } else {
                ESP_LOGW(TAG, "connect failed status=%d", event->connect.status);
                reset_link();
                s_state = ECP_CLIENT_STATE_IDLE;
                set_status("connect failed");
                start_scan_locked();
            }
            return 0;

        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "disconnected reason=%d", event->disconnect.reason);
            reset_link();
            set_status("scanning");
            start_scan_locked();
            return 0;

        default:
            return 0;
    }
}

}  // namespace

void ecp_client_init(void)
{
    if (s_uuids_ready) return;
    parse_uuid128(ecp::SERVICE_UUID, &s_uuids.service);
    parse_uuid128(ecp::INFO_UUID, &s_uuids.info);
    parse_uuid128(ecp::COMMAND_UUID, &s_uuids.command);
    parse_uuid128(ecp::RESPONSE_UUID, &s_uuids.response);
    parse_uuid128(ecp::STATE_UUID, &s_uuids.state);
    s_uuids_ready = true;
}

void ecp_client_start_scan(void)
{
    if (!s_uuids_ready) return;
    if (s_state == ECP_CLIENT_STATE_CONNECTED || s_state == ECP_CLIENT_STATE_CONNECTING) return;
    start_scan_locked();
}

void ecp_client_stop_scan(void)
{
    if (s_state != ECP_CLIENT_STATE_SCANNING) return;
    (void)ble_gap_disc_cancel();
    s_state = ECP_CLIENT_STATE_IDLE;
    set_status("idle");
}

size_t ecp_client_get_device_count(void)
{
    return s_device_count;
}

bool ecp_client_get_device(size_t index, EcpClientDevice_t* out)
{
    if (index >= s_device_count || out == nullptr) return false;
    *out = s_devices[index];
    return true;
}

bool ecp_client_connect(size_t index)
{
    if (index >= s_device_count) return false;
    if (s_state == ECP_CLIENT_STATE_CONNECTING || s_state == ECP_CLIENT_STATE_CONNECTED) return false;

    if (s_state == ECP_CLIENT_STATE_SCANNING) {
        (void)ble_gap_disc_cancel();
    }

    const EcpClientDevice_t& dev = s_devices[index];
    snprintf(s_peer_label, sizeof(s_peer_label), "%s", dev.label);

    ble_addr_t addr;
    addr.type = dev.address_type;
    // dev.address is printed "aa:bb:cc:dd:ee:ff" (human, big-endian); NimBLE
    // stores address bytes little-endian, so the parsed bytes are reversed.
    unsigned b[6];
    if (sscanf(dev.address, "%02x:%02x:%02x:%02x:%02x:%02x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
        return false;
    }
    for (int i = 0; i < 6; ++i) addr.val[i] = static_cast<uint8_t>(b[5 - i]);

    s_state = ECP_CLIENT_STATE_CONNECTING;
    set_status("connecting");

    int rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &addr, 30000, nullptr, gap_event_handler, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_connect failed rc=%d", rc);
        s_state = ECP_CLIENT_STATE_ERROR;
        set_status("connect failed");
        return false;
    }
    return true;
}

void ecp_client_disconnect(void)
{
    if (s_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
    s_state = ECP_CLIENT_STATE_DISCONNECTING;
    set_status("disconnecting");
    ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
}

EcpClientState_t ecp_client_get_state(void)
{
    return s_state;
}

const char* ecp_client_get_status_text(void)
{
    return s_status_text;
}

const char* ecp_client_get_peer_label(void)
{
    return s_peer_label;
}

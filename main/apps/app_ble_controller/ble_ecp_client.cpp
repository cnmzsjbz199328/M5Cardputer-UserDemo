/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "ble_ecp_client.h"
#include "ecosystem_protocol.h"

#include <cstdio>
#include <cstring>

#include "esp_err.h"
#include "esp_log.h"
#include "host/ble_att.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_hs_mbuf.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

extern "C" void ble_store_config_init(void);

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
bool s_nimble_started = false;
bool s_scan_requested = false;

EcpClientState_t s_state = ECP_CLIENT_STATE_IDLE;
char s_status_text[32]   = "idle";
char s_peer_label[32]    = "";
char s_peer_caps[64]     = "";
uint32_t s_tx_drop_count = 0;

EcpClientDevice_t s_devices[ECP_CLIENT_MAX_DEVICES];
size_t s_device_count = 0;

uint16_t s_conn_handle      = BLE_HS_CONN_HANDLE_NONE;
uint16_t s_svc_start_handle = 0;
uint16_t s_svc_end_handle   = 0;
uint16_t s_info_handle      = 0;
uint16_t s_command_handle   = 0;
uint16_t s_response_handle  = 0;
uint16_t s_state_handle     = 0;
uint32_t s_pending_rid      = 0;
uint16_t s_link_mtu         = BLE_ATT_MTU_DFLT;

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
    s_peer_caps[0]      = '\0';
    s_link_mtu          = BLE_ATT_MTU_DFLT;
    s_tx_drop_count     = 0;
}

void start_scan_locked();
void start_service_discovery();

bool get_own_addr_type(uint8_t* own_addr_type)
{
    int rc = ble_hs_id_infer_auto(0, own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto failed rc=%d", rc);
        s_state = ECP_CLIENT_STATE_ERROR;
        set_status("addr failed");
        return false;
    }
    return true;
}

void on_host_sync()
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_util_ensure_addr failed rc=%d", rc);
        s_state = ECP_CLIENT_STATE_ERROR;
        set_status("addr failed");
        return;
    }

    ESP_LOGI(TAG, "NimBLE host synced");
    if (s_scan_requested && s_state != ECP_CLIENT_STATE_CONNECTED && s_state != ECP_CLIENT_STATE_CONNECTING) {
        start_scan_locked();
    }
}

void host_task(void* param)
{
    (void)param;
    ESP_LOGI(TAG, "NimBLE host task started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

bool ensure_nimble_started()
{
    if (s_nimble_started) return true;

    esp_err_t ret = nimble_port_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "nimble_port_init failed ret=%d", ret);
        s_state = ECP_CLIENT_STATE_ERROR;
        set_status("ble init fail");
        return false;
    }

    ble_hs_cfg.sync_cb = on_host_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_store_config_init();
    int mtu_rc = ble_att_set_preferred_mtu(ecp::MAX_MESSAGE_BYTES + 3);
    if (mtu_rc != 0) {
        ESP_LOGW(TAG, "ble_att_set_preferred_mtu failed rc=%d", mtu_rc);
    }

    if (ret == ESP_OK) {
        nimble_port_freertos_init(host_task);
    }

    s_nimble_started = true;
    return true;
}

int gap_event_handler(struct ble_gap_event* event, void* arg);

void start_scan_locked()
{
    if (!ble_hs_synced()) {
        ESP_LOGW(TAG, "skip scan: NimBLE host not synced yet");
        s_state = ECP_CLIENT_STATE_IDLE;
        set_status("ble starting");
        return;
    }

    uint8_t own_addr_type = 0;
    if (!get_own_addr_type(&own_addr_type)) return;

    s_device_count = 0;
    s_state        = ECP_CLIENT_STATE_SCANNING;
    set_status("scanning");

    struct ble_gap_disc_params params;
    memset(&params, 0, sizeof(params));
    params.passive           = 0;
    params.filter_duplicates = 1;
    params.itvl              = 0x50;
    params.window            = 0x30;

    int rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &params, gap_event_handler, nullptr);
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

bool adv_name_starts_with_eco(const struct ble_hs_adv_fields* fields)
{
    return fields->name != nullptr && fields->name_len >= 4 && memcmp(fields->name, "ECO-", 4) == 0;
}

void addr_to_str(const ble_addr_t* addr, char* out, size_t out_size)
{
    snprintf(out, out_size, "%02x:%02x:%02x:%02x:%02x:%02x", addr->val[5], addr->val[4], addr->val[3], addr->val[2],
              addr->val[1], addr->val[0]);
}

void handle_disc_event(struct ble_gap_disc_desc* disc)
{
    char address[18];
    addr_to_str(&disc->addr, address, sizeof(address));

    struct ble_hs_adv_fields fields;
    char label[32] = "";
    const bool fields_ok = ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) == 0;

    size_t existing = ECP_CLIENT_MAX_DEVICES;
    for (size_t i = 0; i < s_device_count; ++i) {
        if (strcmp(s_devices[i].address, address) == 0) {
            existing = i;
            break;
        }
    }

    const bool ecp_service = fields_ok && adv_matches_ecp_service(disc->data, disc->length_data);
    const bool eco_name = fields_ok && adv_name_starts_with_eco(&fields);
    if (!ecp_service && !eco_name && existing == ECP_CLIENT_MAX_DEVICES) return;

    if (fields_ok && fields.name != nullptr && fields.name_len > 0) {
        size_t n = fields.name_len < sizeof(label) - 1 ? fields.name_len : sizeof(label) - 1;
        memcpy(label, fields.name, n);
        label[n] = '\0';
    } else {
        snprintf(label, sizeof(label), "%s", address);
    }

    if (existing != ECP_CLIENT_MAX_DEVICES) {
        s_devices[existing].rssi = static_cast<int8_t>(disc->rssi);
        if (label[0] != '\0' && strcmp(label, address) != 0) {
            snprintf(s_devices[existing].label, sizeof(s_devices[existing].label), "%s", label);
        }
        return;
    }
    if (s_device_count >= ECP_CLIENT_MAX_DEVICES) return;

    EcpClientDevice_t& dev = s_devices[s_device_count++];
    snprintf(dev.address, sizeof(dev.address), "%s", address);
    dev.address_type = disc->addr.type;
    snprintf(dev.label, sizeof(dev.label), "%s", label);
    dev.rssi = static_cast<int8_t>(disc->rssi);
}

void json_escape_char(char ch, char* out, size_t out_size)
{
    if (out_size == 0) return;
    if (ch == '"' || ch == '\\') {
        snprintf(out, out_size, "\\%c", ch);
    } else if (ch == '\n') {
        snprintf(out, out_size, "\\n");
    } else if (ch == '\r') {
        snprintf(out, out_size, "\\r");
    } else if (ch == '\t') {
        snprintf(out, out_size, "\\t");
    } else if (static_cast<unsigned char>(ch) < 0x20) {
        snprintf(out, out_size, "\\u%04x", static_cast<unsigned char>(ch));
    } else {
        snprintf(out, out_size, "%c", ch);
    }
}

// Extracts the "capabilities":[...] string array from an INFO characteristic
// JSON payload into a flat comma-separated list (e.g. "imu,input.remote.imu"),
// without depending on a full JSON parser. Matches the field the reference
// Cardputer controller reads via ArduinoJson in connect_selected()/
// set_capabilities().
void extract_capabilities(const char* json, char* out, size_t out_size)
{
    out[0] = '\0';
    const char* key = strstr(json, "\"capabilities\"");
    if (key == nullptr) return;
    const char* start = strchr(key, '[');
    if (start == nullptr) return;
    const char* end = strchr(start, ']');
    if (end == nullptr) return;

    size_t oi = 0;
    for (const char* p = start + 1; p < end && oi < out_size - 1; ++p) {
        if (*p == '"' || *p == ' ') continue;
        out[oi++] = *p;
    }
    out[oi] = '\0';
}

int info_read_cb(uint16_t conn_handle, const struct ble_gatt_error* error, struct ble_gatt_attr* attr, void* arg)
{
    (void)arg;
    if (conn_handle != s_conn_handle) return 0;

    if (error->status != 0 || attr == nullptr || attr->om == nullptr) {
        ESP_LOGW(TAG, "INFO read failed status=%d", error != nullptr ? error->status : -1);
        return 0;
    }

    char buf[224];
    uint16_t len = OS_MBUF_PKTLEN(attr->om);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    if (ble_hs_mbuf_to_flat(attr->om, buf, len, nullptr) != 0) return 0;
    buf[len] = '\0';

    extract_capabilities(buf, s_peer_caps, sizeof(s_peer_caps));
    ESP_LOGI(TAG, "INFO capabilities=\"%s\"", s_peer_caps);
    return 0;
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

        int rc = ble_gattc_read(s_conn_handle, s_info_handle, info_read_cb, nullptr);
        if (rc != 0) {
            ESP_LOGW(TAG, "ble_gattc_read(info) failed rc=%d", rc);
        }
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

void start_service_discovery()
{
    int rc = ble_gattc_disc_svc_by_uuid(s_conn_handle, &s_uuids.service.u, svc_disc_cb, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gattc_disc_svc_by_uuid failed rc=%d", rc);
        set_status("connect failed");
        s_state = ECP_CLIENT_STATE_ERROR;
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

int mtu_exchange_cb(uint16_t conn_handle, const struct ble_gatt_error* error, uint16_t mtu, void* arg)
{
    (void)arg;
    if (conn_handle != s_conn_handle) return 0;

    if (error->status == 0) {
        s_link_mtu = mtu;
        ESP_LOGI(TAG, "MTU exchange complete mtu=%u", static_cast<unsigned>(mtu));
    } else {
        ESP_LOGW(TAG, "MTU exchange failed status=%d; continuing with mtu=%u", error->status,
                 static_cast<unsigned>(s_link_mtu));
    }

    set_status("discovering");
    start_service_discovery();
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
                set_status("mtu");
                int rc = ble_gattc_exchange_mtu(s_conn_handle, mtu_exchange_cb, nullptr);
                if (rc != 0) {
                    ESP_LOGW(TAG, "ble_gattc_exchange_mtu failed rc=%d; discovering anyway", rc);
                    set_status("discovering");
                    start_service_discovery();
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

        case BLE_GAP_EVENT_MTU:
            if (event->mtu.conn_handle == s_conn_handle) {
                s_link_mtu = event->mtu.value;
                ESP_LOGI(TAG, "MTU update event mtu=%u", static_cast<unsigned>(s_link_mtu));
            }
            return 0;

        default:
            return 0;
    }
}

}  // namespace

void ecp_client_init(void)
{
    ensure_nimble_started();

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
    s_scan_requested = true;
    if (!ensure_nimble_started()) return;
    if (!s_uuids_ready) return;
    if (s_state == ECP_CLIENT_STATE_CONNECTED || s_state == ECP_CLIENT_STATE_CONNECTING) return;
    start_scan_locked();
}

void ecp_client_stop_scan(void)
{
    s_scan_requested = false;
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
    s_scan_requested = false;

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

    if (!ble_hs_synced()) {
        ESP_LOGW(TAG, "skip connect: NimBLE host not synced yet");
        s_state = ECP_CLIENT_STATE_IDLE;
        set_status("ble starting");
        return false;
    }

    uint8_t own_addr_type = 0;
    if (!get_own_addr_type(&own_addr_type)) return false;

    int rc = ble_gap_connect(own_addr_type, &addr, 30000, nullptr, gap_event_handler, nullptr);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gap_connect failed rc=%d", rc);
        s_state = ECP_CLIENT_STATE_ERROR;
        set_status("connect failed");
        return false;
    }
    return true;
}

bool ecp_client_send_key(const char* key, char char_val)
{
    if (s_state != ECP_CLIENT_STATE_CONNECTED || s_conn_handle == BLE_HS_CONN_HANDLE_NONE || s_command_handle == 0 ||
        key == nullptr) {
        return false;
    }

    s_pending_rid++;
    if (s_pending_rid == 0) s_pending_rid++;

    char payload[160];
    if (strcmp(key, "char") == 0) {
        char escaped[8];
        json_escape_char(char_val, escaped, sizeof(escaped));
        snprintf(payload, sizeof(payload), "{\"v\":%u,\"rid\":%lu,\"op\":\"%s\",\"key\":\"char\",\"event\":\"press\","
                                           "\"char\":\"%s\"}",
                 static_cast<unsigned>(ecp::PROTOCOL_VERSION), static_cast<unsigned long>(s_pending_rid),
                 ecp::OP_INPUT_KEY, escaped);
    } else {
        snprintf(payload, sizeof(payload), "{\"v\":%u,\"rid\":%lu,\"op\":\"%s\",\"key\":\"%s\",\"event\":\"press\"}",
                 static_cast<unsigned>(ecp::PROTOCOL_VERSION), static_cast<unsigned long>(s_pending_rid),
                 ecp::OP_INPUT_KEY, key);
    }

    const size_t payload_len = strlen(payload);
    if (payload_len > static_cast<size_t>(s_link_mtu > 3 ? s_link_mtu - 3 : 0)) {
        ESP_LOGW(TAG, "input.key payload too large len=%u mtu=%u key=%s", static_cast<unsigned>(payload_len),
                 static_cast<unsigned>(s_link_mtu), key);
        set_status("mtu small");
        s_tx_drop_count++;
        return false;
    }

    int rc = ble_gattc_write_no_rsp_flat(s_conn_handle, s_command_handle, payload, payload_len);
    if (rc != 0) {
        ESP_LOGW(TAG, "input.key write failed rc=%d key=%s", rc, key);
        set_status("key drop");
        s_tx_drop_count++;
        return false;
    }

    set_status("keys active");
    return true;
}

bool ecp_client_send_imu(uint32_t seq, int16_t ax, int16_t ay, int16_t az, int16_t gx, int16_t gy, int16_t gz)
{
    if (s_state != ECP_CLIENT_STATE_CONNECTED || s_conn_handle == BLE_HS_CONN_HANDLE_NONE || s_command_handle == 0) {
        return false;
    }

    s_pending_rid++;
    if (s_pending_rid == 0) s_pending_rid++;

    char payload[192];
    snprintf(payload, sizeof(payload),
             "{\"v\":%u,\"rid\":%lu,\"op\":\"%s\",\"seq\":%lu,\"ax\":%d,\"ay\":%d,\"az\":%d,\"gx\":%d,\"gy\":%d,"
             "\"gz\":%d}",
             static_cast<unsigned>(ecp::PROTOCOL_VERSION), static_cast<unsigned long>(s_pending_rid),
             ecp::OP_INPUT_IMU, static_cast<unsigned long>(seq), ax, ay, az, gx, gy, gz);

    const size_t payload_len = strlen(payload);
    if (payload_len > static_cast<size_t>(s_link_mtu > 3 ? s_link_mtu - 3 : 0)) {
        ESP_LOGW(TAG, "input.imu payload too large len=%u mtu=%u", static_cast<unsigned>(payload_len),
                 static_cast<unsigned>(s_link_mtu));
        s_tx_drop_count++;
        return false;
    }

    int rc = ble_gattc_write_no_rsp_flat(s_conn_handle, s_command_handle, payload, payload_len);
    if (rc != 0) {
        ESP_LOGW(TAG, "input.imu write failed rc=%d", rc);
        s_tx_drop_count++;
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

const char* ecp_client_get_peer_capabilities(void)
{
    return s_peer_caps;
}

uint16_t ecp_client_get_link_mtu(void)
{
    return s_link_mtu;
}

uint32_t ecp_client_get_tx_drop_count(void)
{
    return s_tx_drop_count;
}

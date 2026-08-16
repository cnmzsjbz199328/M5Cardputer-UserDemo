/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
// Minimal BLE GAP-central client for the "ecosystem protocol" (ECP), ported
// from external/cardputer_controller_reference/main.cpp (Arduino NimBLE) to
// ESP-IDF's native NimBLE host API, following the local blueprint already
// used for the peripheral HID role in hal/utils/ble_hid_device.
//
// IMPORTANT: this module does NOT initialize the Bluetooth controller or the
// NimBLE host stack. ble_hid_device_helper_init() (called from HAL init)
// already brings the NimBLE host up as a BLE HID peripheral; this module
// only issues additional GAP-central calls (scan/connect) against that
// already-running host, which NimBLE supports concurrently with the
// peripheral role.
//
// Scope of this first slice (see docs/ble-controller-migration.md): scan,
// connect, GATT service/characteristic discovery, disconnect, and device
// listing. input.key and input.imu forwarding have since been added; STATE
// notify subscription is still a follow-up slice.
//
// NOT build-verified: this environment has no ESP-IDF toolchain available,
// so this file has only been reviewed statically against the NimBLE APIs
// used elsewhere in this repo (hal/utils/ble_hid_device/ble_hid_gap.c) and
// the well-known esp-idf blecent example. Run `idf.py build` and then a
// real scan/connect test against an ECP peripheral before relying on it.
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ECP_CLIENT_STATE_IDLE = 0,
    ECP_CLIENT_STATE_SCANNING,
    ECP_CLIENT_STATE_CONNECTING,
    ECP_CLIENT_STATE_CONNECTED,
    ECP_CLIENT_STATE_DISCONNECTING,
    ECP_CLIENT_STATE_ERROR,
} EcpClientState_t;

#define ECP_CLIENT_MAX_DEVICES 8

typedef struct {
    char address[18];  // "aa:bb:cc:dd:ee:ff"
    uint8_t address_type;
    char label[32];
    int8_t rssi;
} EcpClientDevice_t;

// Must be called once after the NimBLE host is up (i.e. after
// ble_hid_device_helper_init()) and before any other ecp_client_* call.
void ecp_client_init(void);

// Starts (or restarts) scanning for peripherals advertising the ECP service
// UUID. Clears the current device list.
void ecp_client_start_scan(void);
void ecp_client_stop_scan(void);

size_t ecp_client_get_device_count(void);
// Returns false if index is out of range.
bool ecp_client_get_device(size_t index, EcpClientDevice_t* out);

// Connects to the device at the given index in the current scan list and
// runs ECP service/characteristic discovery. Returns false immediately if
// the index is invalid or a connection attempt is already in progress;
// actual connect/discovery result arrives asynchronously and is reflected
// by ecp_client_get_state()/ecp_client_get_status_text().
bool ecp_client_connect(size_t index);

// Sends an ECP input.key press to the connected peer. char_val is only used
// when key is "char"; pass '\0' for semantic keys such as "enter".
bool ecp_client_send_key(const char* key, char char_val);

// Sends one ECP input.imu sample to the connected peer. Accel fields are
// milli-g, gyro fields are degrees/sec, matching the reference Cardputer
// controller's wire format (external/cardputer_controller_reference).
bool ecp_client_send_imu(uint32_t seq, int16_t ax, int16_t ay, int16_t az, int16_t gx, int16_t gy, int16_t gz);

// Disconnects the current peer (if any) and returns to scanning.
void ecp_client_disconnect(void);

EcpClientState_t ecp_client_get_state(void);
// Short human-readable status, safe to render directly (e.g. "connected",
// "connect failed", "protocol incomplete").
const char* ecp_client_get_status_text(void);
// Label of the currently connected/connecting peer, empty string if none.
const char* ecp_client_get_peer_label(void);
// Comma-separated capability list read from the peer's INFO characteristic
// (e.g. "imu,input.remote.imu"), empty string if not yet read or unknown.
const char* ecp_client_get_peer_capabilities(void);
// Negotiated ATT MTU of the current/last link, in bytes.
uint16_t ecp_client_get_link_mtu(void);
// Count of input.key/input.imu writes dropped (oversized payload or GATT
// write failure) since the current connection was established.
uint32_t ecp_client_get_tx_drop_count(void);

#ifdef __cplusplus
}
#endif

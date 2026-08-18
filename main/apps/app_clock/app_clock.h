/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <cstdint>
#include <hal/hal.h>
#include <ctime>
#include <string>
#include <atomic>

/**
 * @brief
 *
 */
class AppClock : public mooncake::AppAbility {
public:
    AppClock();
    ~AppClock();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    static constexpr uint32_t UPDATE_INTERVAL = 1000;  // Update every 1 second

    enum NetStatus {
        NET_IDLE,
        NET_CONNECTING,
        NET_CONNECTED,
        NET_FAILED_NO_CREDS,
        NET_FAILED_CONNECT,
    };

    uint32_t _time_count    = 0;
    int _key_event_slot_id  = -1;

    std::atomic<bool> _net_task_running{false};
    std::atomic<int> _net_status{NET_IDLE};

    void render_interface();
    void update_time_display();
    void draw_clock_frame();
    void handle_key_event(const Keyboard::KeyEvent_t& keyEvent);

    void start_network_task();
    static void network_task_entry(void* param);
    void network_task_body();
};

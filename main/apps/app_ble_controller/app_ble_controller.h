/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <mooncake.h>
#include <cstdint>
#include <hal/hal.h>

/**
 * @brief
 *
 */
class AppBleController : public mooncake::AppAbility {
public:
    AppBleController();
    ~AppBleController();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    void render_interface();
};

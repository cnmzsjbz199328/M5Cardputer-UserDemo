/*
 * SPDX-FileCopyrightText: 2025 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "app_ble_controller.h"
#include "assets/ble_controller_big.h"
#include "assets/ble_controller_small.h"
#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <apps/utils/theme.h>
#include <mooncake_log.h>
#include <assets.h>

using namespace mooncake;

AppBleController::AppBleController()
{
    setAppInfo().name     = "BLE Controller";
    setAppInfo().userData = new AppIcon_t(image_data_ble_controller_big, image_data_ble_controller_small);
}

AppBleController::~AppBleController()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppBleController::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");
    render_interface();
}

void AppBleController::onRunning()
{
    // Close app when home button clicked
    if (GetHAL().homeButton.wasClicked()) {
        audio::play_random_tone();
        close();
    }
}

void AppBleController::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
}

void AppBleController::render_interface()
{
    GetHAL().canvas.fillScreen(THEME_COLOR_BG);
    GetHAL().canvas.setTextColor(TFT_WHITE, THEME_COLOR_BG);
    GetHAL().canvas.setTextSize(2);
    GetHAL().canvas.setCursor(10, 10);
    GetHAL().canvas.print("BLE Controller");
    GetHAL().canvas.setTextSize(1);
    GetHAL().canvas.setCursor(10, 40);
    GetHAL().canvas.print("(work in progress)");
    GetHAL().pushCanvas();
}

#pragma once

#include <mooncake.h>
#include <hal/hal.h>
#include <cstdint>

#include "core/clock.h"
#include "platform_graphics.h"
#include "render/atlas_renderer.h"
#include "render/camera.h"
#include "render/viewport.h"

class AppSolarSystem : public mooncake::AppAbility {
public:
    AppSolarSystem();
    ~AppSolarSystem();

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    solar::Camera _camera;
    solar::SimulationClock _clock;
    solar::Viewport _viewport{};
    solar::AtlasRenderer _renderer;
    HalGraphics _graphics;
    uint32_t _last_update_ms = 0;
    uint32_t _last_render_ms = 0;
    int _key_slot_id = -1;

    void handle_key_event(const Keyboard::KeyEvent_t& keyEvent);
    void render();
};

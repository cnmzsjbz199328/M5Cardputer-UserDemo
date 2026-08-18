#include "app_solar_system.h"
#include "platform_keymap.h"

#include <apps/utils/audio/audio.h>
#include <mooncake_log.h>
#include <algorithm>

namespace {

constexpr uint32_t kRenderIntervalMs = 33;

float clamp_delta_seconds(uint32_t elapsed_ms)
{
    return std::min(0.12f, static_cast<float>(elapsed_ms) / 1000.0f);
}

} // namespace

AppSolarSystem::AppSolarSystem()
{
    setAppInfo().name = "Solar System";
}

AppSolarSystem::~AppSolarSystem() = default;

void AppSolarSystem::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _camera = solar::Camera{};
    _clock = solar::SimulationClock(solar::DEFAULT_BUILD_EPOCH_DAYS);
    _viewport = solar::Viewport::create(GetHAL().canvas.width(), GetHAL().canvas.height());
    _last_update_ms = GetHAL().millis();
    _last_render_ms = 0;

    _key_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) { handle_key_event(keyEvent); });

    render();
}

void AppSolarSystem::onRunning()
{
    const uint32_t now = GetHAL().millis();
    const float dt = clamp_delta_seconds(now - _last_update_ms);
    _last_update_ms = now;

    _clock.update(dt);

    if (now - _last_render_ms >= kRenderIntervalMs) {
        render();
        _last_render_ms = now;
    }

    if (GetHAL().homeButton.wasClicked()) {
        audio::play_random_tone();
        close();
    }
}

void AppSolarSystem::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    if (_key_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_slot_id);
        _key_slot_id = -1;
    }
}

void AppSolarSystem::handle_key_event(const Keyboard::KeyEvent_t& keyEvent)
{
    if (!keyEvent.state || keyEvent.isModifier) return;

    const solar::InputEvent event = solar::cardputerIntent(keyEvent);
    if (event.intent == solar::Intent::Back && _camera.systemIndex == solar::SUN_INDEX) {
        // Already at the top-level heliocentric view: ESC/GRAVE/Backspace
        // closes the app, matching the project-wide back-gesture convention.
        close();
    } else if (event.intent != solar::Intent::None) {
        _camera.handleInput(event, _clock);
    }
}

void AppSolarSystem::render()
{
    _renderer.drawFrame(_graphics, _viewport, _camera, _clock);
    GetHAL().pushCanvas();
}

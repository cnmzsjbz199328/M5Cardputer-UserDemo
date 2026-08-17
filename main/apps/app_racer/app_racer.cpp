#include "app_racer.h"

#include <apps/utils/audio/audio.h>
#include <hal/hal.h>
#include <mooncake_log.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr uint32_t kRenderIntervalMs = 33;
constexpr float kMaxSpeed = 280.0f;

float clamp_delta_seconds(uint32_t elapsed_ms)
{
    return std::min(0.12f, static_cast<float>(elapsed_ms) / 1000.0f);
}

} // namespace

AppRacer::AppRacer()
{
    setAppInfo().name = "Racer";
}

AppRacer::~AppRacer() = default;

void AppRacer::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _track.reset();
    _distance = 0.0f;
    _player_x = 0.0f;
    _speed = 0.0f;
    _steer_left_held = false;
    _steer_right_held = false;
    _last_update_ms = GetHAL().millis();
    _last_render_ms = 0;

    _key_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& keyEvent) { handle_key_event(keyEvent); });

    render();
}

void AppRacer::onRunning()
{
    const uint32_t now = GetHAL().millis();
    const float dt = clamp_delta_seconds(now - _last_update_ms);
    _last_update_ms = now;

    update(dt);

    if (now - _last_render_ms >= kRenderIntervalMs) {
        render();
        _last_render_ms = now;
    }

    if (GetHAL().homeButton.wasClicked()) {
        audio::play_random_tone();
        close();
    }
}

void AppRacer::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");

    if (_key_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_slot_id);
        _key_slot_id = -1;
    }

    _steer_left_held = false;
    _steer_right_held = false;
}

void AppRacer::handle_key_event(const Keyboard::KeyEvent_t& keyEvent)
{
    if (keyEvent.keyCode == KEY_LEFT) {
        _steer_left_held = keyEvent.state;
    } else if (keyEvent.keyCode == KEY_RIGHT) {
        _steer_right_held = keyEvent.state;
    }
}

void AppRacer::update(float dt)
{
    _speed += (kMaxSpeed - _speed) * std::min(1.0f, dt * 1.8f);

    const float steeringInput = static_cast<float>(_steer_right_held) - static_cast<float>(_steer_left_held);
    const float steeringRate = 1.0f + (_speed / kMaxSpeed) * 1.8f;
    _player_x += steeringInput * steeringRate * dt;

    // Curves gently push the car outward. This gives the keyboard version a
    // little weight without adding collision or off-track rules yet.
    _player_x -= _track.segmentAtDistance(_distance).curve * _speed * dt * 0.22f;
    _player_x = std::clamp(_player_x, -1.0f, 1.0f);

    _distance += _speed * dt;
    if (_distance >= _track.length()) {
        _distance = std::fmod(_distance, _track.length());
    }
}

void AppRacer::render()
{
    _renderer.render(_track, _distance, _player_x, _speed);
    GetHAL().pushCanvas();
}

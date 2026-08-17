#include "app_racer.h"

#include <apps/utils/audio/audio.h>
#include <hal/hal.h>
#include <mooncake_log.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr uint32_t kRenderIntervalMs = 33;
constexpr float kMaxSpeed = 280.0f;
constexpr float kCollisionDistance = 23.0f;
constexpr float kCollisionLaneOverlap = 0.28f;

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
    _imu_data = {};
    _imu_steering = 0.0f;
    _imu_sample_available = false;
    _steer_left_held = false;
    _steer_right_held = false;
    _collision_cooldown = 0.0f;
    _engine_audio_elapsed = 0.0f;
    reset_traffic();

    GetHAL().imu.begin();
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

void AppRacer::reset_traffic()
{
    _traffic = {
        TrafficCar{360.0f, -0.58f, 150.0f, TFT_CYAN},
        TrafficCar{690.0f, 0.00f, 190.0f, TFT_BLUE},
        TrafficCar{1040.0f, 0.58f, 215.0f, TFT_ORANGE},
        TrafficCar{1430.0f, -0.24f, 175.0f, TFT_YELLOW},
    };
}

void AppRacer::update_imu()
{
    if (!GetHAL().imu.update()) {
        return;
    }

    _imu_data = GetHAL().imu.getImuData();

    // Match app_imu's Cardputer orientation mapping: accel.x is scaled to a
    // small screen offset and then sign-flipped for the display orientation.
    int tilt_offset_x = std::clamp(static_cast<int>(_imu_data.accel.x * 15.0f), -15, 15);
    tilt_offset_x = -tilt_offset_x;
    _imu_steering = static_cast<float>(tilt_offset_x) / 15.0f;
    _imu_sample_available = true;
}

void AppRacer::update(float dt)
{
    update_imu();

    _speed += (kMaxSpeed - _speed) * std::min(1.0f, dt * 1.8f);

    const float keyboardSteering = static_cast<float>(_steer_right_held) -
                                   static_cast<float>(_steer_left_held);
    const float tiltSteering = _imu_sample_available ? _imu_steering * 0.85f : 0.0f;
    const float steeringInput = std::clamp(keyboardSteering + tiltSteering, -1.0f, 1.0f);
    const float steeringRate = 1.0f + (_speed / kMaxSpeed) * 1.8f;
    _player_x += steeringInput * steeringRate * dt;

    // Curves gently push the car outward. This gives the keyboard version a
    // little weight without adding collision or off-track rules yet.
    _player_x -= _track.segmentAtDistance(_distance).curve * _speed * dt * 0.22f;
    _player_x = std::clamp(_player_x, -1.0f, 1.0f);

    _distance += _speed * dt;
    update_traffic(dt);
    update_audio(dt);
}

void AppRacer::update_traffic(float dt)
{
    for (TrafficCar& car : _traffic) {
        car.distance += car.speed * dt;
    }

    _collision_cooldown = std::max(0.0f, _collision_cooldown - dt);
    if (_collision_cooldown > 0.0f) {
        return;
    }

    for (TrafficCar& car : _traffic) {
        const float relativeDistance = _track.forwardDistance(_distance, car.distance);
        const float laneDistance = std::abs(car.lane - _player_x);
        if (relativeDistance <= 1.0f || relativeDistance > kCollisionDistance ||
            laneDistance > kCollisionLaneOverlap) {
            continue;
        }

        _speed *= 0.30f;
        _collision_cooldown = 0.8f;
        car.distance = _distance + 120.0f;
        if (!audio::is_quiet_mode()) {
            audio::play_tone(180, 0.12);
        }
        break;
    }
}

void AppRacer::update_audio(float dt)
{
    _engine_audio_elapsed += dt;
    if (_engine_audio_elapsed < 0.12f) {
        return;
    }
    _engine_audio_elapsed = 0.0f;

    if (_speed < 8.0f || audio::is_quiet_mode()) {
        return;
    }

    const int frequency = 220 + static_cast<int>((_speed / kMaxSpeed) * 320.0f);
    audio::play_tone(frequency, 0.035);
}

void AppRacer::render()
{
    _renderer.render(_track, _distance, _player_x, _speed, _traffic);
    GetHAL().pushCanvas();
}

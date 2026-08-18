#include "app_racer.h"
#include "assets/racer_big.h"
#include "assets/racer_small.h"

#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <hal/hal.h>
#include <mooncake_log.h>

#include <algorithm>
#include <cmath>

namespace {

constexpr uint32_t kRenderIntervalMs = 33;
constexpr float kMaxSpeed = 280.0f;
constexpr float kMaxBoostedSpeed = kMaxSpeed * 1.30f;
constexpr float kLaneHalfWidth = 1.0f;
constexpr float kPlayerHalfWidth = 1.6f;
constexpr float kOffRoadSpeedFactor = 0.62f;
constexpr float kOffRoadSteeringFactor = 0.68f;
constexpr float kCollisionDistance = 23.0f;
constexpr float kCollisionLaneOverlap = 0.28f;
constexpr float kCleanOvertakeWindow = 320.0f;
constexpr float kBehindWindow = 40.0f;
constexpr int32_t kCollisionScorePenalty = 250;
constexpr int32_t kCleanOvertakeScore = 180;
constexpr float kPitchScale = 1.15f;
constexpr float kPitchBoost = 0.15f;
constexpr float kPitchBrakeRate = 190.0f;
constexpr float kDriftYawThreshold = 35.0f;
constexpr float kDriftCurveThreshold = 0.001f;
constexpr float kDriftChargeRate = 1.0f;
constexpr float kDriftDecayRate = 3.5f;
constexpr float kDriftMinimumCharge = 0.30f;
constexpr float kDriftBoostDuration = 1.0f;
constexpr float kDriftBoostMultiplier = 1.18f;
constexpr char kBestLapSetting[] = "racer_best_lap_ms";

float clamp_delta_seconds(uint32_t elapsed_ms)
{
    return std::min(0.12f, static_cast<float>(elapsed_ms) / 1000.0f);
}

} // namespace

AppRacer::AppRacer()
{
    setAppInfo().name     = "Racer";
    setAppInfo().userData = new AppIcon_t(image_data_racer_big, image_data_racer_small);
}

AppRacer::~AppRacer()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppRacer::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    _track.reset();
    _distance = 0.0f;
    _player_x = 0.0f;
    _speed = 0.0f;
    _imu_data = {};
    _imu_steering = 0.0f;
    _imu_pitch = 0.0f;
    _imu_yaw_rate = 0.0f;
    _imu_sample_available = false;
    _steer_left_held = false;
    _steer_right_held = false;
    _on_road = true;
    _was_on_road = true;
    _collision_cooldown = 0.0f;
    _drift_charge = 0.0f;
    _drift_boost_remaining = 0.0f;
    _was_in_curve = false;
    _score = 0;
    _completed_laps = 0;
    _race_elapsed = 0.0f;
    _lap_elapsed = 0.0f;
    _run_best_lap_ms = 0;
    _all_time_best_lap_ms = GetHAL().getSettings().GetInt(kBestLapSetting, 0);
    _finish_time_ms = 0;
    _new_record = false;
    _finish_flash_remaining = 0.0f;
    _race_state = RaceState::Racing;
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
    if (_race_state == RaceState::Finished) {
        _finish_flash_remaining = std::max(0.0f, _finish_flash_remaining - dt);
    }

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
    if (_race_state == RaceState::Finished) {
        if (keyEvent.state) {
            close();
        }
        return;
    }

    if (keyEvent.keyCode == KEY_LEFT) {
        _steer_left_held = keyEvent.state;
    } else if (keyEvent.keyCode == KEY_RIGHT) {
        _steer_right_held = keyEvent.state;
    } else if (keyEvent.state && (keyEvent.keyCode == KEY_ESC || keyEvent.keyCode == KEY_GRAVE)) {
        close();
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
    // The Cardputer's pitch sign depends on how the player holds the device.
    // If forward tilt brakes instead of boosting on hardware, flip this sign.
    _imu_pitch = std::clamp(_imu_data.accel.y * kPitchScale, -1.0f, 1.0f);
    _imu_yaw_rate = _imu_data.gyro.z;
    _imu_sample_available = true;
}

void AppRacer::update(float dt)
{
    if (_race_state != RaceState::Racing) {
        return;
    }

    update_imu();

    const float previousDistance = _distance;
    const Segment& currentSegment = _track.segmentAtDistance(_distance);
    const bool inCurve = std::abs(currentSegment.curve) > kDriftCurveThreshold;

    const float keyboardSteering = static_cast<float>(_steer_right_held) -
                                   static_cast<float>(_steer_left_held);
    const float tiltSteering = _imu_sample_available ? _imu_steering * 0.85f : 0.0f;
    const float steeringInput = std::clamp(keyboardSteering + tiltSteering, -1.0f, 1.0f);
    const float steeringRate = (1.0f + (_speed / kMaxSpeed) * 1.8f) *
                               (_on_road ? 1.0f : kOffRoadSteeringFactor);
    _player_x += steeringInput * steeringRate * dt;

    // Curves gently push the car outward, while the wider limit makes grass a
    // real state rather than a cosmetic extension of the road edge.
    _player_x -= currentSegment.curve * _speed * dt * 0.22f;
    _player_x = std::clamp(_player_x, -kPlayerHalfWidth, kPlayerHalfWidth);
    _on_road = std::abs(_player_x) <= kLaneHalfWidth;

    if (_was_on_road && !_on_road && !audio::is_quiet_mode()) {
        audio::play_tone(150, 0.07);
        _engine_audio_elapsed = 0.0f;
    }

    float speedTarget = kMaxSpeed * (_on_road ? 1.0f : kOffRoadSpeedFactor);
    if (_imu_pitch > 0.0f) {
        speedTarget *= 1.0f + _imu_pitch * kPitchBoost;
    }
    if (_drift_boost_remaining > 0.0f) {
        const float boostProgress = _drift_boost_remaining / kDriftBoostDuration;
        speedTarget *= 1.0f + boostProgress * 0.15f;
        _drift_boost_remaining = std::max(0.0f, _drift_boost_remaining - dt);
    }
    _speed += (speedTarget - _speed) * std::min(1.0f, dt * 1.8f);
    if (_imu_pitch < 0.0f) {
        _speed -= -_imu_pitch * kPitchBrakeRate * dt;
    }
    _speed = std::clamp(_speed, 0.0f, kMaxBoostedSpeed);

    const bool leavingCurve = _was_in_curve && !inCurve;
    if (inCurve && std::abs(_imu_yaw_rate) > kDriftYawThreshold) {
        _drift_charge = std::min(1.0f, _drift_charge + dt * kDriftChargeRate);
    } else if (!leavingCurve) {
        _drift_charge = std::max(0.0f, _drift_charge - dt * kDriftDecayRate);
    }

    if (leavingCurve && _drift_charge >= kDriftMinimumCharge && _on_road) {
        _speed = std::min(kMaxBoostedSpeed, _speed * kDriftBoostMultiplier);
        _drift_boost_remaining = kDriftBoostDuration;
        _score += static_cast<int32_t>(std::lround(_drift_charge * 500.0f));
        if (!audio::is_quiet_mode()) {
            audio::play_tone(760, 0.08);
        }
        _drift_charge = 0.0f;
    }
    _was_in_curve = inCurve;

    _distance += _speed * dt;
    const float distanceDelta = _distance - previousDistance;
    _race_elapsed += dt;
    _lap_elapsed += dt;
    _score += static_cast<int32_t>(std::lround(distanceDelta * (_on_road ? 1.0f : 0.35f)));

    update_traffic(dt, previousDistance);
    update_audio(dt);

    const int lapsCompleted = static_cast<int>(std::floor(_distance / _track.length()));
    while (_completed_laps < lapsCompleted) {
        _completed_laps++;
        const int32_t lapTimeMs = std::max<int32_t>(1, static_cast<int32_t>(std::lround(_lap_elapsed * 1000.0f)));
        if (_run_best_lap_ms == 0 || lapTimeMs < _run_best_lap_ms) {
            _run_best_lap_ms = lapTimeMs;
        }
        _lap_elapsed = 0.0f;
    }

    _was_on_road = _on_road;
    if (_completed_laps >= 3) {
        finish_race();
    }
}

void AppRacer::update_traffic(float dt, float previous_distance)
{
    for (TrafficCar& car : _traffic) {
        const float previousRelativeDistance = _track.forwardDistance(previous_distance, car.distance);
        car.distance += car.speed * dt;
        const float relativeDistance = _track.forwardDistance(_distance, car.distance);

        if (!car.clean_overtake_eligible && relativeDistance > 60.0f && relativeDistance < kCleanOvertakeWindow) {
            car.clean_overtake_eligible = true;
        }

        if (car.clean_overtake_eligible && previousRelativeDistance <= kCleanOvertakeWindow &&
            relativeDistance >= _track.length() - kBehindWindow) {
            _score += kCleanOvertakeScore;
            car.clean_overtake_eligible = false;
            if (!audio::is_quiet_mode()) {
                audio::play_tone(620, 0.04);
            }
        }
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
        _score = std::max<int32_t>(0, _score - kCollisionScorePenalty);
        _collision_cooldown = 0.8f;
        car.distance = _distance + 120.0f;
        car.clean_overtake_eligible = false;
        if (!audio::is_quiet_mode()) {
            audio::play_tone(180, 0.12);
        }
        break;
    }
}

void AppRacer::finish_race()
{
    _race_state = RaceState::Finished;
    _speed = 0.0f;
    _finish_time_ms = std::max<int32_t>(1, static_cast<int32_t>(std::lround(_race_elapsed * 1000.0f)));
    _new_record = _run_best_lap_ms > 0 &&
                  (_all_time_best_lap_ms == 0 || _run_best_lap_ms < _all_time_best_lap_ms);
    if (_new_record) {
        _all_time_best_lap_ms = _run_best_lap_ms;
        GetHAL().getSettings().SetInt(kBestLapSetting, _all_time_best_lap_ms);
        GetHAL().getSettings().Commit();
    }

    _finish_flash_remaining = 0.35f;
    if (!audio::is_quiet_mode()) {
        audio::play_melody({72, 76, 79}, 0.07);
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
    if (_race_state == RaceState::Finished) {
        _renderer.renderResults(_finish_time_ms, _run_best_lap_ms, _all_time_best_lap_ms, _score,
                                _new_record, _finish_flash_remaining > 0.0f);
    } else {
        _renderer.render(_track, _distance, _player_x, _speed, _traffic, _on_road, _completed_laps, _score);
    }
    GetHAL().pushCanvas();
}

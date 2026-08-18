#pragma once

#include <mooncake.h>
#include <hal/hal.h>

#include <cstdint>
#include <vector>

#include "road_renderer.h"
#include "track.h"

class AppRacer : public mooncake::AppAbility {
public:
    AppRacer();
    ~AppRacer() override;

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum class RaceState {
        Racing,
        Finished,
    };

    Track _track;
    RoadRenderer _renderer;
    float _distance = 0.0f;
    float _player_x = 0.0f;
    float _speed = 0.0f;
    m5::imu_data_t _imu_data{};
    float _imu_steering = 0.0f;
    float _imu_pitch = 0.0f;
    float _imu_yaw_rate = 0.0f;
    bool _imu_sample_available = false;
    bool _steer_left_held = false;
    bool _steer_right_held = false;
    bool _on_road = true;
    bool _was_on_road = true;
    std::vector<TrafficCar> _traffic;
    float _collision_cooldown = 0.0f;
    float _drift_charge = 0.0f;
    float _drift_boost_remaining = 0.0f;
    bool _was_in_curve = false;
    int32_t _score = 0;
    int _completed_laps = 0;
    float _race_elapsed = 0.0f;
    float _lap_elapsed = 0.0f;
    int32_t _run_best_lap_ms = 0;
    int32_t _all_time_best_lap_ms = 0;
    int32_t _finish_time_ms = 0;
    bool _new_record = false;
    float _finish_flash_remaining = 0.0f;
    RaceState _race_state = RaceState::Racing;
    float _engine_audio_elapsed = 0.0f;
    uint32_t _last_update_ms = 0;
    uint32_t _last_render_ms = 0;
    int _key_slot_id = -1;

    void handle_key_event(const Keyboard::KeyEvent_t& keyEvent);
    void reset_traffic();
    void update_imu();
    void update(float dt);
    void update_traffic(float dt, float previous_distance);
    void update_audio(float dt);
    void finish_race();
    void render();
};

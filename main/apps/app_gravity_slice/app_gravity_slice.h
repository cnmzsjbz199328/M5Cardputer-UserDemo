#pragma once

#include <mooncake.h>
#include <hal/hal.h>

#include <cstddef>
#include <cstdint>

#include "slice_renderer.h"

class AppGravitySlice : public mooncake::AppAbility {
public:
    AppGravitySlice();
    ~AppGravitySlice() override;

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum class GameState {
        Playing,
        Finished,
    };

    gravity_slice::SliceRenderer _renderer;
    gravity_slice::SpawnObject _spawns[gravity_slice::kSpawnPoolSize]{};
    gravity_slice::Ball _balls[gravity_slice::kBallPoolSize]{};
    m5::imu_data_t _imu_data{};
    float _gravity_x = 0.0f;
    float _gravity_y = 0.0f;
    bool _imu_sample_available = false;
    float _elapsed_seconds = 0.0f;
    float _spawn_timer_ms = 0.0f;
    uint32_t _random_state = 0;
    std::size_t _next_spawn_slot = 0;
    gravity_slice::PowerUpKind _power_up = gravity_slice::PowerUpKind::None;
    float _power_up_remaining = 0.0f;
    int32_t _score = 0;
    int32_t _best_score = 0;
    bool _new_record = false;
    float _finish_flash_remaining = 0.0f;
    GameState _state = GameState::Playing;
    uint32_t _last_update_ms = 0;
    uint32_t _last_render_ms = 0;
    int _key_slot_id = -1;

    void handle_key_event(const Keyboard::KeyEvent_t& key_event);
    void reset_game();
    void reset_balls();
    void update_imu();
    void update(float dt);
    void update_balls(float dt);
    void separate_balls();
    void update_spawns(float dt);
    void maybe_spawn_object(float dt);
    void spawn_object();
    void handle_collisions();
    void activate_power_up(gravity_slice::PowerUpKind kind);
    void clear_extra_balls();
    void finish_run();
    void render();

    uint32_t next_random();
    float random_unit();
    gravity_slice::PowerUpKind random_power_up();
    gravity_slice::SpawnKind random_spawn_kind();
    float spawn_interval_ms() const;
    float spiral_offset_x() const;
};

#pragma once

#include <mooncake.h>
#include <hal/hal.h>

#include <cstdint>

#include "pool_renderer.h"

class AppPool : public mooncake::AppAbility {
public:
    AppPool();
    ~AppPool() override;

    void onOpen() override;
    void onRunning() override;
    void onClose() override;

private:
    enum class GameState {
        Aiming,
        BallsMoving,
        Finished,
    };

    pool::PoolRenderer _renderer;
    pool::Ball _balls[pool::kBallPoolSize]{};
    pool::Pocket _pockets[pool::kPocketCount]{};
    m5::imu_data_t _imu_data{};
    float _aim_angle = 0.0f;
    int32_t _score = 0;
    int32_t _best_score = 0;
    bool _new_record = false;
    bool _cue_respawn_pending = false;
    float _finish_flash_remaining = 0.0f;
    GameState _state = GameState::Aiming;
    uint32_t _last_update_ms = 0;
    uint32_t _last_render_ms = 0;
    int _key_slot_id = -1;

    void handle_key_event(const Keyboard::KeyEvent_t& key_event);
    void reset_game();
    void reset_balls();
    void reset_pockets();
    void update_aim();
    void update(float dt);
    void update_balls(float dt);
    void resolve_collisions();
    void handle_pockets();
    void settle_round();
    void fire_cue_ball();
    void respawn_cue_ball();
    void finish_run();
    void render();

    bool all_balls_stopped() const;
    bool all_objects_potted() const;
};

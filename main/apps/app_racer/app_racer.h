#pragma once

#include <mooncake.h>
#include <hal/hal.h>

#include <cstdint>

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
    Track _track;
    RoadRenderer _renderer;
    float _distance = 0.0f;
    float _player_x = 0.0f;
    float _speed = 0.0f;
    bool _steer_left_held = false;
    bool _steer_right_held = false;
    uint32_t _last_update_ms = 0;
    uint32_t _last_render_ms = 0;
    int _key_slot_id = -1;

    void handle_key_event(const Keyboard::KeyEvent_t& keyEvent);
    void update(float dt);
    void render();
};

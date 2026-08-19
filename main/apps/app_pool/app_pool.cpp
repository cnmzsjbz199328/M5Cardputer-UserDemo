#include "app_pool.h"

#include "assets/pool_big.h"
#include "assets/pool_small.h"

#include <apps/utils/common.h>
#include <hal/hal.h>
#include <mooncake_log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

using pool::Ball;
using pool::Pocket;

constexpr uint32_t kRenderIntervalMs = 33;
constexpr float kImuPitchScale = 1.15f;
constexpr float kShotSpeed = 150.0f;
constexpr float kFriction = 2.2f;
constexpr float kWallRestitution = 0.9f;
constexpr float kStopSpeed = 3.0f;
constexpr int32_t kBallScore = 100;
constexpr int32_t kScratchPenalty = 50;
// NVS keys are capped at 15 bytes (ESP_ERR_NVS_KEY_TOO_LONG past that).
constexpr char kBestScoreSetting[] = "pool_best";

float clamp_delta_seconds(uint32_t elapsed_ms)
{
    return std::min(0.12f, static_cast<float>(elapsed_ms) / 1000.0f);
}

float distance_squared(float ax, float ay, float bx, float by)
{
    const float dx = ax - bx;
    const float dy = ay - by;
    return dx * dx + dy * dy;
}

float table_right()
{
    return static_cast<float>(GetHAL().canvas.width()) - pool::kTableLeft - 1.0f;
}

float table_bottom()
{
    return static_cast<float>(GetHAL().canvas.height()) - pool::kTableBottomInset - 1.0f;
}

uint16_t object_color(std::size_t index)
{
    constexpr uint16_t colors[] = {
        static_cast<uint16_t>(TFT_RED),
        static_cast<uint16_t>(TFT_YELLOW),
        static_cast<uint16_t>(TFT_BLUE),
        static_cast<uint16_t>(TFT_ORANGE),
        static_cast<uint16_t>(TFT_MAGENTA),
    };
    return colors[index % (sizeof(colors) / sizeof(colors[0]))];
}

}  // namespace

AppPool::AppPool()
{
    setAppInfo().name = "Pool";
    setAppInfo().userData = new AppIcon_t(image_data_pool_big, image_data_pool_small);
}

AppPool::~AppPool()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppPool::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    GetHAL().canvas.setTextWrap(false);
    _best_score = GetHAL().getSettings().GetInt(kBestScoreSetting, 0);
    reset_game();

    GetHAL().imu.begin();
    _last_update_ms = GetHAL().millis();
    _last_render_ms = 0;
    _key_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& key_event) { handle_key_event(key_event); });

    render();
}

void AppPool::onRunning()
{
    const uint32_t now = GetHAL().millis();
    const float dt = clamp_delta_seconds(now - _last_update_ms);
    _last_update_ms = now;

    update(dt);
    if (_state == GameState::Finished) {
        _finish_flash_remaining = std::max(0.0f, _finish_flash_remaining - dt);
    }

    if (now - _last_render_ms >= kRenderIntervalMs) {
        render();
        _last_render_ms = now;
    }

    if (GetHAL().homeButton.wasClicked()) close();
}

void AppPool::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    GetHAL().canvas.setTextWrap(true);
    if (_key_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_slot_id);
        _key_slot_id = -1;
    }
}

void AppPool::handle_key_event(const Keyboard::KeyEvent_t& key_event)
{
    if (!key_event.state) return;

    if (key_event.keyCode == KEY_ESC || key_event.keyCode == KEY_GRAVE) {
        close();
        return;
    }
    if (_state == GameState::Finished) {
        reset_game();
        return;
    }
    if (_state == GameState::Aiming) fire_cue_ball();
}

void AppPool::reset_game()
{
    _imu_data = {};
    _aim_angle = 0.0f;
    _score = 0;
    _new_record = false;
    _cue_respawn_pending = false;
    _finish_flash_remaining = 0.0f;
    _state = GameState::Aiming;
    reset_pockets();
    reset_balls();
}

void AppPool::reset_balls()
{
    for (Ball& ball : _balls) {
        ball = Ball{};
        ball.live = false;
    }

    const float center_y = (pool::kTableTop + table_bottom()) * 0.5f;
    _balls[0].live = true;
    _balls[0].is_cue = true;
    _balls[0].x = pool::kTableLeft + 31.0f;
    _balls[0].y = center_y;
    _balls[0].color = static_cast<uint16_t>(TFT_WHITE);

    constexpr float kRackSpacing = pool::kBallRadius * 2.1f;
    const float rack_x = table_right() - 46.0f;
    const float half_spacing = kRackSpacing * 0.5f;
    const float positions[5][2] = {
        {rack_x, center_y},
        {rack_x + kRackSpacing, center_y - half_spacing},
        {rack_x + kRackSpacing, center_y + half_spacing},
        {rack_x + kRackSpacing * 2.0f, center_y - kRackSpacing},
        {rack_x + kRackSpacing * 2.0f, center_y + kRackSpacing},
    };
    for (std::size_t i = 0; i < 5; ++i) {
        Ball& ball = _balls[i + 1];
        ball.live = true;
        ball.x = positions[i][0];
        ball.y = positions[i][1];
        ball.color = object_color(i);
    }
}

void AppPool::reset_pockets()
{
    const float left = pool::kTableLeft;
    const float right = table_right();
    const float top = pool::kTableTop;
    const float bottom = table_bottom();
    const float middle = (left + right) * 0.5f;

    _pockets[0] = {left, top};
    _pockets[1] = {middle, top};
    _pockets[2] = {right, top};
    _pockets[3] = {left, bottom};
    _pockets[4] = {middle, bottom};
    _pockets[5] = {right, bottom};
}

void AppPool::update_aim()
{
    if (_state != GameState::Aiming) return;
    if (!GetHAL().imu.update()) return;

    _imu_data = GetHAL().imu.getImuData();
    const float ax = -_imu_data.accel.x;
    const float ay = std::clamp(_imu_data.accel.y * kImuPitchScale, -1.0f, 1.0f);
    if (std::abs(ax) > 0.05f || std::abs(ay) > 0.05f) _aim_angle = std::atan2(ay, ax);
}

void AppPool::update(float dt)
{
    update_aim();
    if (_state != GameState::BallsMoving) return;

    update_balls(dt);
    resolve_collisions();
    handle_pockets();
    settle_round();
}

void AppPool::update_balls(float dt)
{
    const float min_x = pool::kTableLeft + pool::kBallRadius;
    const float max_x = table_right() - pool::kBallRadius;
    const float min_y = pool::kTableTop + pool::kBallRadius;
    const float max_y = table_bottom() - pool::kBallRadius;
    const float friction = std::exp(-kFriction * dt);

    for (Ball& ball : _balls) {
        if (!ball.live) continue;

        ball.x += ball.vx * dt;
        ball.y += ball.vy * dt;
        ball.vx *= friction;
        ball.vy *= friction;

        if (ball.x < min_x) {
            ball.x = min_x;
            if (ball.vx < 0.0f) ball.vx = -ball.vx * kWallRestitution;
        } else if (ball.x > max_x) {
            ball.x = max_x;
            if (ball.vx > 0.0f) ball.vx = -ball.vx * kWallRestitution;
        }

        if (ball.y < min_y) {
            ball.y = min_y;
            if (ball.vy < 0.0f) ball.vy = -ball.vy * kWallRestitution;
        } else if (ball.y > max_y) {
            ball.y = max_y;
            if (ball.vy > 0.0f) ball.vy = -ball.vy * kWallRestitution;
        }

        if (std::sqrt(ball.vx * ball.vx + ball.vy * ball.vy) <= kStopSpeed) {
            ball.vx = 0.0f;
            ball.vy = 0.0f;
        }
    }
}

void AppPool::resolve_collisions()
{
    const float min_distance = pool::kBallRadius * 2.0f;
    const float min_distance_squared = min_distance * min_distance;

    for (std::size_t i = 0; i < pool::kBallPoolSize; ++i) {
        if (!_balls[i].live) continue;
        for (std::size_t j = i + 1; j < pool::kBallPoolSize; ++j) {
            if (!_balls[j].live) continue;

            float dx = _balls[j].x - _balls[i].x;
            float dy = _balls[j].y - _balls[i].y;
            float distance_sq = dx * dx + dy * dy;
            if (distance_sq >= min_distance_squared) continue;

            float distance = std::sqrt(distance_sq);
            if (distance < 0.001f) {
                dx = (j & 1U) == 0U ? 1.0f : -1.0f;
                dy = 0.0f;
                distance = 1.0f;
            }

            const float normal_x = dx / distance;
            const float normal_y = dy / distance;
            const float push = (min_distance - distance) * 0.5f;
            _balls[i].x -= normal_x * push;
            _balls[i].y -= normal_y * push;
            _balls[j].x += normal_x * push;
            _balls[j].y += normal_y * push;

            const float relative_vx = _balls[j].vx - _balls[i].vx;
            const float relative_vy = _balls[j].vy - _balls[i].vy;
            const float relative_normal_speed = relative_vx * normal_x + relative_vy * normal_y;
            if (relative_normal_speed >= 0.0f) continue;

            const float ball_i_normal_speed = _balls[i].vx * normal_x + _balls[i].vy * normal_y;
            const float ball_j_normal_speed = _balls[j].vx * normal_x + _balls[j].vy * normal_y;
            _balls[i].vx += (ball_j_normal_speed - ball_i_normal_speed) * normal_x;
            _balls[i].vy += (ball_j_normal_speed - ball_i_normal_speed) * normal_y;
            _balls[j].vx += (ball_i_normal_speed - ball_j_normal_speed) * normal_x;
            _balls[j].vy += (ball_i_normal_speed - ball_j_normal_speed) * normal_y;
        }
    }
}

void AppPool::handle_pockets()
{
    const float pocket_distance_squared = pool::kPocketRadius * pool::kPocketRadius;
    for (Ball& ball : _balls) {
        if (!ball.live) continue;

        for (const Pocket& pocket : _pockets) {
            if (distance_squared(ball.x, ball.y, pocket.x, pocket.y) > pocket_distance_squared) continue;

            ball.live = false;
            ball.vx = 0.0f;
            ball.vy = 0.0f;
            if (ball.is_cue) {
                _score = std::max<int32_t>(0, _score - kScratchPenalty);
                _cue_respawn_pending = true;
            } else {
                _score += kBallScore;
            }
            break;
        }
    }
}

void AppPool::settle_round()
{
    if (!all_balls_stopped()) return;

    for (Ball& ball : _balls) {
        if (ball.live) {
            ball.vx = 0.0f;
            ball.vy = 0.0f;
        }
    }

    if (all_objects_potted()) {
        finish_run();
        return;
    }
    if (_cue_respawn_pending) respawn_cue_ball();
    _state = GameState::Aiming;
}

void AppPool::fire_cue_ball()
{
    Ball& cue_ball = _balls[0];
    if (!cue_ball.live || !cue_ball.is_cue) return;

    cue_ball.vx = std::cos(_aim_angle) * kShotSpeed;
    cue_ball.vy = std::sin(_aim_angle) * kShotSpeed;
    _state = GameState::BallsMoving;
}

void AppPool::respawn_cue_ball()
{
    Ball& cue_ball = _balls[0];
    cue_ball.live = true;
    cue_ball.is_cue = true;
    cue_ball.x = (pool::kTableLeft + table_right()) * 0.5f;
    cue_ball.y = (pool::kTableTop + table_bottom()) * 0.5f;
    cue_ball.vx = 0.0f;
    cue_ball.vy = 0.0f;
    cue_ball.color = static_cast<uint16_t>(TFT_WHITE);
    _cue_respawn_pending = false;
}

void AppPool::finish_run()
{
    if (_state == GameState::Finished) return;

    _state = GameState::Finished;
    _new_record = _score > _best_score;
    if (_new_record) {
        _best_score = _score;
        GetHAL().getSettings().SetInt(kBestScoreSetting, _best_score);
        GetHAL().getSettings().Commit();
    }
    _finish_flash_remaining = 0.35f;
}

void AppPool::render()
{
    if (_state == GameState::Finished) {
        _renderer.renderFinished(_score, _best_score, _new_record, _finish_flash_remaining > 0.0f);
    } else {
        _renderer.render(_balls, _pockets, _score, _best_score, _aim_angle, _state == GameState::Aiming);
    }
    GetHAL().pushCanvas();
}

bool AppPool::all_balls_stopped() const
{
    for (const Ball& ball : _balls) {
        if (!ball.live) continue;
        if (std::sqrt(ball.vx * ball.vx + ball.vy * ball.vy) > kStopSpeed) return false;
    }
    return true;
}

bool AppPool::all_objects_potted() const
{
    for (std::size_t i = 1; i < pool::kBallPoolSize; ++i) {
        if (_balls[i].live) return false;
    }
    return true;
}

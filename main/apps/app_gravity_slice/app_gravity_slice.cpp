#include "app_gravity_slice.h"

#include "assets/gravity_slice_big.h"
#include "assets/gravity_slice_small.h"

#include <apps/utils/audio/audio.h>
#include <apps/utils/common.h>
#include <hal/hal.h>
#include <mooncake_log.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

using gravity_slice::Ball;
using gravity_slice::kHudHeight;
using gravity_slice::kSpawnRadius;
using gravity_slice::PowerUpKind;
using gravity_slice::SpawnKind;
using gravity_slice::SpawnObject;

constexpr uint32_t kRunDurationMs = 75000;
constexpr float kGravityPxS2 = 260.0f;
constexpr float kRestitution = 1.08f;   // pinball bumper: exit a little hotter than entry
constexpr float kMinBounceSpeed = 90.0f;  // floor so a soft tap still kicks back visibly
constexpr float kMaxBallSpeed = 420.0f;   // clamp so the >1.0 restitution can't run away
constexpr float kDamping = 0.995f;
constexpr float kBallRadius = 4.0f;
constexpr uint32_t kRenderIntervalMs = 33;
constexpr float kSpawnIntervalMs = 900.0f;
constexpr int32_t kFruitScore = 100;
constexpr int32_t kBombPenalty = 150;
constexpr float kPowerUpDurationS = 4.0f;
// NVS keys are capped at 15 bytes (ESP_ERR_NVS_KEY_TOO_LONG past that).
constexpr char kBestScoreSetting[] = "gslice_best";

constexpr float kImuPitchScale = 1.15f;
constexpr float kSpawnGravityPxS2 = 155.0f;
constexpr float kSpawnInitialSpeed = 105.0f;
constexpr float kBeamRadiusBonus = 2.5f;
constexpr float kSpiralFrequency = 11.0f;
constexpr float kSpiralAmplitude = 3.0f;
constexpr int kSpreadBallCount = 3;
constexpr float kSpreadAngle = 0.62f;

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

uint16_t object_color(SpawnKind kind, PowerUpKind power_up)
{
    if (kind == SpawnKind::Fruit) return static_cast<uint16_t>(TFT_ORANGE);
    if (kind == SpawnKind::Bomb) return static_cast<uint16_t>(TFT_RED);

    switch (power_up) {
    case PowerUpKind::Invincible:
        return static_cast<uint16_t>(TFT_YELLOW);
    case PowerUpKind::Spiral:
        return static_cast<uint16_t>(TFT_MAGENTA);
    case PowerUpKind::Spread:
        return static_cast<uint16_t>(TFT_GREEN);
    case PowerUpKind::Beam:
        return static_cast<uint16_t>(TFT_CYAN);
    case PowerUpKind::None:
        break;
    }
    return static_cast<uint16_t>(TFT_WHITE);
}

}  // namespace

AppGravitySlice::AppGravitySlice()
{
    setAppInfo().name = "Slice";
    setAppInfo().userData = new AppIcon_t(image_data_gravity_slice_big, image_data_gravity_slice_small);
}

AppGravitySlice::~AppGravitySlice()
{
    delete static_cast<AppIcon_t*>(getAppInfo().userData);
}

void AppGravitySlice::onOpen()
{
    mclog::tagInfo(getAppInfo().name, "on open");

    GetHAL().canvas.setTextWrap(false);
    reset_game();
    _best_score = GetHAL().getSettings().GetInt(kBestScoreSetting, 0);

    GetHAL().imu.begin();
    _last_update_ms = GetHAL().millis();
    _last_render_ms = 0;
    _key_slot_id = GetHAL().keyboard.onKeyEvent.connect(
        [this](const Keyboard::KeyEvent_t& key_event) { handle_key_event(key_event); });

    render();
}

void AppGravitySlice::onRunning()
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

    if (GetHAL().homeButton.wasClicked()) {
        if (!audio::is_quiet_mode()) audio::play_random_tone();
        close();
    }
}

void AppGravitySlice::onClose()
{
    mclog::tagInfo(getAppInfo().name, "on close");
    GetHAL().canvas.setTextWrap(true);
    if (_key_slot_id >= 0) {
        GetHAL().keyboard.onKeyEvent.disconnect(_key_slot_id);
        _key_slot_id = -1;
    }
}

void AppGravitySlice::handle_key_event(const Keyboard::KeyEvent_t& key_event)
{
    if (!key_event.state) return;

    if (_state == GameState::Finished || key_event.keyCode == KEY_ESC || key_event.keyCode == KEY_GRAVE) {
        close();
    }
}

void AppGravitySlice::reset_game()
{
    _imu_data = {};
    _gravity_x = 0.0f;
    _gravity_y = 0.0f;
    _imu_sample_available = false;
    _elapsed_seconds = 0.0f;
    _spawn_timer_ms = kSpawnIntervalMs * 0.5f;
    _random_state = GetHAL().millis() ^ 0x9e3779b9U;
    _next_spawn_slot = 0;
    _power_up = PowerUpKind::None;
    _power_up_remaining = 0.0f;
    _score = 0;
    _new_record = false;
    _finish_flash_remaining = 0.0f;
    _state = GameState::Playing;

    for (SpawnObject& object : _spawns) {
        object = SpawnObject{};
    }
    reset_balls();
}

void AppGravitySlice::reset_balls()
{
    for (Ball& ball : _balls) {
        ball = Ball{};
    }

    const float width = static_cast<float>(GetHAL().canvas.width());
    const float height = static_cast<float>(GetHAL().canvas.height());
    _balls[0].live = true;
    _balls[0].is_main = true;
    _balls[0].x = width * 0.5f;
    _balls[0].y = kHudHeight + (height - kHudHeight) * 0.5f;
}

void AppGravitySlice::update_imu()
{
    if (!GetHAL().imu.update()) return;

    _imu_data = GetHAL().imu.getImuData();

    // Keep AppRacer's verified Cardputer ADV convention exactly: accel.x is
    // converted through a 15-pixel offset and sign-flipped for display.
    int tilt_offset_x = std::clamp(static_cast<int>(_imu_data.accel.x * 15.0f), -15, 15);
    tilt_offset_x = -tilt_offset_x;
    _gravity_x = static_cast<float>(tilt_offset_x) / 15.0f * kGravityPxS2;

    // AppRacer uses this same y-axis scale and sign. Verify the physical
    // "down" direction on the first hardware pass before changing tuning.
    _gravity_y = std::clamp(_imu_data.accel.y * kImuPitchScale, -1.0f, 1.0f) * kGravityPxS2;
    _imu_sample_available = true;
}

void AppGravitySlice::update(float dt)
{
    // Keep sampling IMU once per frame, even while the results screen is up.
    update_imu();
    if (_state == GameState::Finished) return;

    update_balls(dt);
    separate_balls();
    maybe_spawn_object(dt);
    update_spawns(dt);
    handle_collisions();

    if (_power_up != PowerUpKind::None) {
        _power_up_remaining = std::max(0.0f, _power_up_remaining - dt);
        if (_power_up_remaining <= 0.0f) {
            if (_power_up == PowerUpKind::Spread) clear_extra_balls();
            _power_up = PowerUpKind::None;
        }
    }

    _elapsed_seconds += dt;
    if (_elapsed_seconds * 1000.0f >= static_cast<float>(kRunDurationMs)) finish_run();
}

void AppGravitySlice::update_balls(float dt)
{
    const float width = static_cast<float>(GetHAL().canvas.width());
    const float height = static_cast<float>(GetHAL().canvas.height());
    const float min_y = kHudHeight + kBallRadius;
    const float max_x = width - kBallRadius;
    const float max_y = height - kBallRadius;

    for (Ball& ball : _balls) {
        if (!ball.live) continue;

        ball.vx += _gravity_x * dt;
        ball.vy += _gravity_y * dt;
        ball.vx *= kDamping;
        ball.vy *= kDamping;
        ball.x += ball.vx * dt;
        ball.y += ball.vy * dt;

        if (ball.x < kBallRadius) {
            ball.x = kBallRadius;
            if (ball.vx < 0.0f) ball.vx = std::max(-ball.vx * kRestitution, kMinBounceSpeed);
        } else if (ball.x > max_x) {
            ball.x = max_x;
            if (ball.vx > 0.0f) ball.vx = -std::max(ball.vx * kRestitution, kMinBounceSpeed);
        }

        if (ball.y < min_y) {
            ball.y = min_y;
            if (ball.vy < 0.0f) ball.vy = std::max(-ball.vy * kRestitution, kMinBounceSpeed);
        } else if (ball.y > max_y) {
            ball.y = max_y;
            if (ball.vy > 0.0f) ball.vy = -std::max(ball.vy * kRestitution, kMinBounceSpeed);
        }

        // Cap overall speed every frame (not just on bounce) so the >1.0
        // restitution can't compound into a runaway ball in a corner.
        const float speed = std::sqrt(ball.vx * ball.vx + ball.vy * ball.vy);
        if (speed > kMaxBallSpeed) {
            const float scale = kMaxBallSpeed / speed;
            ball.vx *= scale;
            ball.vy *= scale;
        }
    }
}

void AppGravitySlice::separate_balls()
{
    int live_count = 0;
    for (const Ball& ball : _balls) {
        if (ball.live) ++live_count;
    }
    if (live_count < 2) return;

    const float min_distance = kBallRadius * 2.0f;
    const float min_distance_squared = min_distance * min_distance;
    for (std::size_t i = 0; i < gravity_slice::kBallPoolSize; ++i) {
        if (!_balls[i].live) continue;
        for (std::size_t j = i + 1; j < gravity_slice::kBallPoolSize; ++j) {
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

            const float push = (min_distance - distance) * 0.5f / distance;
            _balls[i].x -= dx * push;
            _balls[i].y -= dy * push;
            _balls[j].x += dx * push;
            _balls[j].y += dy * push;
        }
    }
}

void AppGravitySlice::maybe_spawn_object(float dt)
{
    _spawn_timer_ms += dt * 1000.0f;
    const float interval = spawn_interval_ms();
    if (_spawn_timer_ms < interval) return;

    _spawn_timer_ms -= interval;
    spawn_object();
}

void AppGravitySlice::spawn_object()
{
    std::size_t slot = _next_spawn_slot;
    for (std::size_t offset = 0; offset < gravity_slice::kSpawnPoolSize; ++offset) {
        const std::size_t candidate = (_next_spawn_slot + offset) % gravity_slice::kSpawnPoolSize;
        if (!_spawns[candidate].live) {
            slot = candidate;
            break;
        }
    }

    const SpawnKind kind = random_spawn_kind();
    const PowerUpKind power_up = kind == SpawnKind::PowerUp ? random_power_up() : PowerUpKind::None;
    SpawnObject& object = _spawns[slot];
    object = SpawnObject{};
    object.live = true;
    object.kind = kind;
    object.power_up_kind = power_up;
    object.x = 6.0f + random_unit() * (static_cast<float>(GetHAL().canvas.width()) - 12.0f);
    object.y = static_cast<float>(GetHAL().canvas.height()) - kSpawnRadius - 1.0f;
    object.vx = (random_unit() - 0.5f) * 54.0f;
    object.vy = -(kSpawnInitialSpeed + random_unit() * 70.0f);
    object.color = object_color(kind, power_up);
    _next_spawn_slot = (slot + 1) % gravity_slice::kSpawnPoolSize;
}

void AppGravitySlice::update_spawns(float dt)
{
    const float height = static_cast<float>(GetHAL().canvas.height());
    for (SpawnObject& object : _spawns) {
        if (!object.live) continue;

        object.vy += kSpawnGravityPxS2 * dt;
        object.x += object.vx * dt;
        object.y += object.vy * dt;
        if (object.y > height + kSpawnRadius) object.live = false;
    }
}

void AppGravitySlice::handle_collisions()
{
    const float collision_radius = _power_up == PowerUpKind::Beam ? kBallRadius + kBeamRadiusBonus : kBallRadius;
    const float hit_distance = collision_radius + kSpawnRadius;
    const float hit_distance_squared = hit_distance * hit_distance;

    for (SpawnObject& object : _spawns) {
        if (!object.live) continue;

        for (Ball& ball : _balls) {
            if (!ball.live) continue;

            float ball_x = ball.x;
            if (ball.is_main && _power_up == PowerUpKind::Spiral) ball_x += spiral_offset_x();
            if (distance_squared(ball_x, ball.y, object.x, object.y) > hit_distance_squared) continue;

            object.live = false;
            if (object.kind == SpawnKind::Fruit) {
                _score += kFruitScore;
                if (!audio::is_quiet_mode()) audio::play_tone(880, 0.05);
            } else if (object.kind == SpawnKind::Bomb) {
                if (_power_up == PowerUpKind::Invincible) {
                    if (!audio::is_quiet_mode()) audio::play_tone(280, 0.05);
                } else {
                    _score = std::max<int32_t>(0, _score - kBombPenalty);
                    if (!audio::is_quiet_mode()) audio::play_tone(180, 0.12);
                }
            } else {
                activate_power_up(object.power_up_kind);
            }
            break;
        }
    }
}

void AppGravitySlice::activate_power_up(PowerUpKind kind)
{
    if (_power_up == PowerUpKind::Spread) clear_extra_balls();
    _power_up = kind;
    _power_up_remaining = kPowerUpDurationS;

    if (kind == PowerUpKind::Spread) {
        int spawned = 0;
        const float main_vx = _balls[0].vx;
        const float main_vy = _balls[0].vy;
        const float base_angle = std::atan2(main_vy, main_vx);
        for (std::size_t i = 1; i < gravity_slice::kBallPoolSize && spawned < kSpreadBallCount; ++i) {
            const float angle = base_angle + (static_cast<float>(spawned) - 1.0f) * kSpreadAngle;
            _balls[i].live = true;
            _balls[i].is_main = false;
            _balls[i].x = _balls[0].x;
            _balls[i].y = _balls[0].y;
            _balls[i].vx = std::cos(angle) * 58.0f;
            _balls[i].vy = std::sin(angle) * 58.0f;
            ++spawned;
        }
    }

    if (!audio::is_quiet_mode()) {
        const int frequencies[] = {0, 720, 790, 860, 930};
        audio::play_tone(frequencies[static_cast<int>(kind)], 0.08);
    }
}

void AppGravitySlice::clear_extra_balls()
{
    for (std::size_t i = 1; i < gravity_slice::kBallPoolSize; ++i) {
        _balls[i] = Ball{};
    }
}

void AppGravitySlice::finish_run()
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
    if (!audio::is_quiet_mode()) audio::play_melody({72, 76, 79}, 0.07);
}

void AppGravitySlice::render()
{
    if (_state == GameState::Finished) {
        _renderer.renderResults(_score, _best_score, _new_record, _finish_flash_remaining > 0.0f);
    } else {
        const uint32_t elapsed_ms = static_cast<uint32_t>(_elapsed_seconds * 1000.0f);
        const uint32_t remaining_ms = elapsed_ms >= kRunDurationMs ? 0 : kRunDurationMs - elapsed_ms;
        const float radius = _power_up == PowerUpKind::Beam ? kBallRadius + kBeamRadiusBonus : kBallRadius;
        _renderer.render(_spawns, _balls, _score, remaining_ms, _power_up, _power_up_remaining,
                         spiral_offset_x(), radius);
    }
    GetHAL().pushCanvas();
}

uint32_t AppGravitySlice::next_random()
{
    _random_state += 0x6d2b79f5U;
    uint32_t value = _random_state;
    value = (value ^ (value >> 15)) * (value | 1U);
    value ^= value + ((value ^ (value >> 7)) * (value | 61U));
    return value ^ (value >> 14);
}

float AppGravitySlice::random_unit()
{
    return static_cast<float>(next_random()) / 4294967295.0f;
}

PowerUpKind AppGravitySlice::random_power_up()
{
    switch (next_random() % 4U) {
    case 0:
        return PowerUpKind::Invincible;
    case 1:
        return PowerUpKind::Spiral;
    case 2:
        return PowerUpKind::Spread;
    default:
        return PowerUpKind::Beam;
    }
}

SpawnKind AppGravitySlice::random_spawn_kind()
{
    const int bomb_weight =
        std::min(34, 18 + static_cast<int>(_elapsed_seconds / 12.0f) + static_cast<int>(_score / 900));
    constexpr int power_up_weight = 14;
    const int fruit_weight = 100 - bomb_weight - power_up_weight;
    const uint32_t roll = next_random() % 100U;
    if (roll < static_cast<uint32_t>(fruit_weight)) return SpawnKind::Fruit;
    if (roll < static_cast<uint32_t>(fruit_weight + bomb_weight)) return SpawnKind::Bomb;
    return SpawnKind::PowerUp;
}

float AppGravitySlice::spawn_interval_ms() const
{
    const float score_factor = std::min(0.48f, static_cast<float>(std::max<int32_t>(0, _score)) / 9000.0f);
    const float elapsed_factor = std::min(0.25f, _elapsed_seconds / 300.0f);
    return kSpawnIntervalMs * (1.0f - score_factor - elapsed_factor);
}

float AppGravitySlice::spiral_offset_x() const
{
    if (_power_up != PowerUpKind::Spiral) return 0.0f;
    return std::sin(_elapsed_seconds * kSpiralFrequency) * kSpiralAmplitude;
}

#pragma once

#include <cstddef>
#include <cstdint>

namespace gravity_slice {

constexpr std::size_t kSpawnPoolSize = 10;
constexpr std::size_t kBallPoolSize = 6;
constexpr float kHudHeight = 11.0f;
constexpr float kSpawnRadius = 3.0f;

enum class PowerUpKind {
    None,
    Invincible,
    Spiral,
    Spread,
    Beam,
};

enum class SpawnKind {
    Fruit,
    Bomb,
    PowerUp,
};

struct SpawnObject {
    bool live = false;
    SpawnKind kind = SpawnKind::Fruit;
    PowerUpKind power_up_kind = PowerUpKind::None;
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    uint16_t color = 0;
};

struct Ball {
    bool live = false;
    bool is_main = false;
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
};

class SliceRenderer {
public:
    void render(const SpawnObject (&spawns)[kSpawnPoolSize], const Ball (&balls)[kBallPoolSize], int32_t score,
                uint32_t remaining_ms, PowerUpKind active_power_up, float power_up_remaining,
                float spiral_offset_x, float ball_radius);
    void renderResults(int32_t score, int32_t best_score, bool new_record, bool flash);
};

}  // namespace gravity_slice

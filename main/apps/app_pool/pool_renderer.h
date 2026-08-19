#pragma once

#include <cstddef>
#include <cstdint>

namespace pool {

constexpr float kBallRadius = 3.0f;
constexpr float kPocketRadius = 6.0f;
constexpr std::size_t kBallPoolSize = 6;
constexpr std::size_t kPocketCount = 6;
constexpr float kHudHeight = 10.0f;
constexpr float kTableLeft = 4.0f;
constexpr float kTableTop = kHudHeight + 3.0f;
constexpr float kTableBottomInset = 4.0f;

struct Ball {
    float x = 0.0f;
    float y = 0.0f;
    float vx = 0.0f;
    float vy = 0.0f;
    bool live = true;
    bool is_cue = false;
    uint16_t color = 0;
};

struct Pocket {
    float x = 0.0f;
    float y = 0.0f;
};

class PoolRenderer {
public:
    void render(const Ball (&balls)[kBallPoolSize], const Pocket (&pockets)[kPocketCount], int32_t score,
                int32_t best_score, float aim_angle, bool show_aim);
    void renderFinished(int32_t score, int32_t best_score, bool new_record, bool flash);
};

}  // namespace pool

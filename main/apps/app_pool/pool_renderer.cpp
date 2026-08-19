#include "pool_renderer.h"

#include <hal/hal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace pool {

namespace {

constexpr uint16_t kBackground = 0x0841;
constexpr uint16_t kFelt = 0x0320;
constexpr uint16_t kRail = 0xA145;
constexpr uint16_t kRailHighlight = 0xD6B4;
constexpr uint16_t kPocket = 0x0000;

float table_right()
{
    return static_cast<float>(GetHAL().canvas.width()) - kTableLeft - 1.0f;
}

float table_bottom()
{
    return static_cast<float>(GetHAL().canvas.height()) - kTableBottomInset - 1.0f;
}

float distance_squared(float ax, float ay, float bx, float by)
{
    const float dx = ax - bx;
    const float dy = ay - by;
    return dx * dx + dy * dy;
}

void draw_table(const Pocket (&pockets)[kPocketCount])
{
    auto& canvas = GetHAL().canvas;
    const int left = static_cast<int>(std::lround(kTableLeft));
    const int top = static_cast<int>(std::lround(kTableTop));
    const int right = static_cast<int>(std::lround(table_right()));
    const int bottom = static_cast<int>(std::lround(table_bottom()));

    canvas.fillRect(left, top, right - left + 1, bottom - top + 1, kRail);
    canvas.fillRect(left + 2, top + 2, right - left - 3, bottom - top - 3, kFelt);
    canvas.drawRect(left, top, right - left + 1, bottom - top + 1, kRailHighlight);

    for (const Pocket& pocket : pockets) {
        const int x = static_cast<int>(std::lround(pocket.x));
        const int y = static_cast<int>(std::lround(pocket.y));
        canvas.fillCircle(x, y, static_cast<int>(kPocketRadius), kPocket);
        canvas.drawCircle(x, y, static_cast<int>(kPocketRadius), TFT_DARKGREY);
    }
}

void draw_aim_line(const Ball (&balls)[kBallPoolSize], float aim_angle)
{
    const Ball* cue_ball = nullptr;
    for (const Ball& ball : balls) {
        if (ball.live && ball.is_cue) {
            cue_ball = &ball;
            break;
        }
    }
    if (cue_ball == nullptr) return;

    auto& canvas = GetHAL().canvas;
    const float dx = std::cos(aim_angle);
    const float dy = std::sin(aim_angle);
    const float min_x = kTableLeft + kBallRadius;
    const float max_x = table_right() - kBallRadius;
    const float min_y = kTableTop + kBallRadius;
    const float max_y = table_bottom() - kBallRadius;
    constexpr float kStep = 2.0f;
    constexpr int kMaxSteps = 130;
    const float hit_distance_squared = (kBallRadius * 2.0f) * (kBallRadius * 2.0f);

    for (int step = 1; step <= kMaxSteps; ++step) {
        const float x = cue_ball->x + dx * static_cast<float>(step) * kStep;
        const float y = cue_ball->y + dy * static_cast<float>(step) * kStep;
        if (x < min_x || x > max_x || y < min_y || y > max_y) break;

        bool hits_ball = false;
        for (const Ball& ball : balls) {
            if (!ball.live || ball.is_cue) continue;
            if (distance_squared(x, y, ball.x, ball.y) < hit_distance_squared) {
                hits_ball = true;
                break;
            }
        }
        if (hits_ball) break;

        if ((step & 1) == 0) {
            canvas.fillCircle(static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)), 1, TFT_YELLOW);
        }
    }
}

void draw_ball(const Ball& ball)
{
    auto& canvas = GetHAL().canvas;
    const int x = static_cast<int>(std::lround(ball.x));
    const int y = static_cast<int>(std::lround(ball.y));
    const uint16_t outline = ball.is_cue ? static_cast<uint16_t>(TFT_DARKGREY) : static_cast<uint16_t>(TFT_WHITE);

    canvas.fillCircle(x, y, static_cast<int>(kBallRadius), ball.is_cue ? static_cast<uint16_t>(TFT_WHITE) : ball.color);
    canvas.drawCircle(x, y, static_cast<int>(kBallRadius), outline);
    canvas.fillCircle(x - 1, y - 1, 1, ball.is_cue ? static_cast<uint16_t>(TFT_WHITE)
                                                   : static_cast<uint16_t>(TFT_YELLOW));
}

}  // namespace

void PoolRenderer::render(const Ball (&balls)[kBallPoolSize], const Pocket (&pockets)[kPocketCount], int32_t score,
                          int32_t best_score, float aim_angle, bool show_aim)
{
    auto& canvas = GetHAL().canvas;
    const int width = canvas.width();

    canvas.fillScreen(kBackground);
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    canvas.setTextWrap(false);
    canvas.setTextColor(TFT_WHITE, kBackground);

    draw_table(pockets);
    if (show_aim) draw_aim_line(balls, aim_angle);
    for (const Ball& ball : balls) {
        if (ball.live) draw_ball(ball);
    }

    char score_text[20] = {};
    char best_text[20] = {};
    std::snprintf(score_text, sizeof(score_text), "S %ld", static_cast<long>(std::max<int32_t>(0, score)));
    std::snprintf(best_text, sizeof(best_text), "B %ld", static_cast<long>(std::max<int32_t>(0, best_score)));
    canvas.setTextColor(TFT_WHITE, kBackground);
    canvas.drawString(score_text, 3, 1);
    canvas.drawRightString(best_text, width - 3, 1);
    canvas.drawFastHLine(0, static_cast<int>(kHudHeight), width, TFT_DARKGREY);

}

void PoolRenderer::renderFinished(int32_t score, int32_t best_score, bool new_record, bool flash)
{
    auto& canvas = GetHAL().canvas;
    const int width = canvas.width();
    const uint16_t accent = flash ? static_cast<uint16_t>(TFT_YELLOW) : static_cast<uint16_t>(TFT_CYAN);

    canvas.fillScreen(kBackground);
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    canvas.setTextWrap(false);
    canvas.setTextColor(accent, kBackground);
    canvas.drawString("CLEAR!", 78, 10);
    canvas.drawRect(20, 25, width - 40, 46, flash ? TFT_YELLOW : TFT_DARKGREY);

    char score_text[24] = {};
    char best_text[24] = {};
    std::snprintf(score_text, sizeof(score_text), "SCORE %ld", static_cast<long>(std::max<int32_t>(0, score)));
    std::snprintf(best_text, sizeof(best_text), "BEST  %ld", static_cast<long>(std::max<int32_t>(0, best_score)));
    canvas.setTextColor(TFT_WHITE, kBackground);
    canvas.drawString(score_text, 63, 34);
    canvas.drawString(best_text, 63, 47);
    if (new_record) {
        canvas.setTextColor(TFT_YELLOW, kBackground);
        canvas.drawString("NEW BEST", 73, 63);
    }
    canvas.setTextColor(TFT_DARKGREY, kBackground);
    canvas.drawString("ANY KEY / HOME", 58, 96);
}

}  // namespace pool

#include "slice_renderer.h"

#include <hal/hal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace gravity_slice {

namespace {

constexpr uint16_t kBackground = 0x0841;

char power_up_badge(PowerUpKind kind)
{
    switch (kind) {
    case PowerUpKind::Invincible:
        return 'I';
    case PowerUpKind::Spiral:
        return 'S';
    case PowerUpKind::Spread:
        return '3';
    case PowerUpKind::Beam:
        return 'B';
    case PowerUpKind::None:
        break;
    }
    return ' ';
}

uint16_t dim_color(uint16_t color)
{
    const uint16_t red = (color >> 11) & 0x1f;
    const uint16_t green = (color >> 5) & 0x3f;
    const uint16_t blue = color & 0x1f;
    return static_cast<uint16_t>(((red / 2) << 11) | ((green / 2) << 5) | (blue / 2));
}

void draw_spawn_object(const SpawnObject& object)
{
    auto& canvas = GetHAL().canvas;
    const int x = static_cast<int>(std::lround(object.x));
    const int y = static_cast<int>(std::lround(object.y));
    if (object.kind == SpawnKind::Fruit) {
        canvas.fillCircle(x, y, static_cast<int>(kSpawnRadius), object.color);
        canvas.fillCircle(x - 1, y - 1, 1, TFT_YELLOW);
    } else if (object.kind == SpawnKind::Bomb) {
        canvas.fillCircle(x, y, static_cast<int>(kSpawnRadius), object.color);
        canvas.drawLine(x - 2, y - 2, x + 2, y + 2, TFT_BLACK);
        canvas.drawLine(x - 2, y + 2, x + 2, y - 2, TFT_BLACK);
    } else {
        canvas.fillCircle(x, y, static_cast<int>(kSpawnRadius) + 1, TFT_WHITE);
        canvas.fillCircle(x, y, static_cast<int>(kSpawnRadius), object.color);
        const char badge[2] = {power_up_badge(object.power_up_kind), '\0'};
        canvas.drawString(badge, x - 2, y - 4);
    }
}

}  // namespace

void SliceRenderer::render(const SpawnObject (&spawns)[kSpawnPoolSize], const Ball (&balls)[kBallPoolSize],
                           int32_t score, uint32_t remaining_ms, PowerUpKind active_power_up,
                           float power_up_remaining, float spiral_offset_x, float ball_radius)
{
    auto& canvas = GetHAL().canvas;
    const int width = canvas.width();
    const int height = canvas.height();
    const int hud_height = static_cast<int>(kHudHeight);

    canvas.fillScreen(kBackground);
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    canvas.setTextWrap(false);
    canvas.setTextColor(TFT_WHITE, kBackground);

    char score_text[20] = {};
    char time_text[12] = {};
    std::snprintf(score_text, sizeof(score_text), "%ld", static_cast<long>(std::max<int32_t>(0, score)));
    std::snprintf(time_text, sizeof(time_text), "%lus", static_cast<unsigned long>((remaining_ms + 999) / 1000));
    canvas.drawString(score_text, 3, 1);
    canvas.drawRightString(time_text, width - 3, 1);

    if (active_power_up != PowerUpKind::None) {
        char badge[8] = {};
        std::snprintf(badge, sizeof(badge), "%c%.0f", power_up_badge(active_power_up),
                      std::ceil(std::max(0.0f, power_up_remaining)));
        canvas.setTextColor(TFT_YELLOW, kBackground);
        canvas.drawString(badge, width / 2 - 5, 1);
    }

    canvas.drawFastHLine(0, hud_height, width, TFT_DARKGREY);
    canvas.drawRect(0, hud_height + 1, width - 1, height - hud_height - 2, TFT_DARKGREY);

    for (const SpawnObject& object : spawns) {
        if (object.live) draw_spawn_object(object);
    }

    for (const Ball& ball : balls) {
        if (!ball.live) continue;
        const float x = ball.x + (ball.is_main ? spiral_offset_x : 0.0f);
        const int draw_x = static_cast<int>(std::lround(x));
        const int draw_y = static_cast<int>(std::lround(ball.y));
        const uint16_t color = ball.is_main ? static_cast<uint16_t>(TFT_CYAN) : dim_color(TFT_CYAN);
        if (active_power_up == PowerUpKind::Beam) {
            canvas.drawCircle(draw_x, draw_y, static_cast<int>(ball_radius), TFT_WHITE);
        }
        canvas.fillCircle(draw_x, draw_y, static_cast<int>(std::lround(ball_radius)), color);
        canvas.fillCircle(draw_x - 1, draw_y - 1, 1, TFT_WHITE);
    }
}

void SliceRenderer::renderResults(int32_t score, int32_t best_score, bool new_record, bool flash)
{
    auto& canvas = GetHAL().canvas;
    const int width = canvas.width();
    // Stay on the same dark backdrop the gameplay view already used instead of
    // flipping the whole canvas white then black — that full-screen invert is
    // what reads as a jarring "black screen" pop on the transition into this
    // view. The "flash" is now just an accent-color pulse on the heading/border.
    const uint32_t accent = flash ? TFT_YELLOW : TFT_CYAN;

    canvas.fillScreen(kBackground);
    canvas.setFont(&fonts::Font0);
    canvas.setTextSize(1);
    canvas.setTextColor(accent, kBackground);
    canvas.drawString("RESULTS", 72, 5);
    canvas.drawRect(20, 20, width - 40, 49, flash ? TFT_YELLOW : TFT_DARKGREY);

    char score_text[24] = {};
    char best_text[24] = {};
    std::snprintf(score_text, sizeof(score_text), "SCORE %ld", static_cast<long>(std::max<int32_t>(0, score)));
    std::snprintf(best_text, sizeof(best_text), "BEST  %ld", static_cast<long>(std::max<int32_t>(0, best_score)));

    canvas.setTextColor(TFT_WHITE, kBackground);
    canvas.drawString(score_text, 61, 28);
    canvas.drawString(best_text, 61, 41);
    if (new_record) {
        canvas.setTextColor(TFT_YELLOW, kBackground);
        canvas.drawString("NEW BEST", 73, 60);
    }

    canvas.setTextColor(TFT_DARKGREY, kBackground);
    canvas.drawString("ANY KEY / HOME", 58, 96);
}

}  // namespace gravity_slice

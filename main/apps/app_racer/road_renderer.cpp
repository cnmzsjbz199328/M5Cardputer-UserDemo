#include "road_renderer.h"

#include <hal/hal.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

constexpr int kDrawDistance = 96;
constexpr float kCameraDepth = 0.86f;
constexpr float kCameraHeight = 1700.0f;
constexpr float kRoadHalfWidth = 2000.0f;
constexpr float kRumbleWidthRatio = 0.13f;
constexpr float kCarBottomMargin = 2.0f;

int clamp_int(int value, int low, int high)
{
    return std::max(low, std::min(value, high));
}

} // namespace

void RoadRenderer::render(const Track& track, float distance, float playerX, float speed)
{
    auto& canvas = GetHAL().canvas;
    const int width = canvas.width();
    const int height = canvas.height();
    const float horizon = static_cast<float>(height) * 0.32f;

    canvas.fillRect(0, 0, width, static_cast<int>(horizon), TFT_SKYBLUE);
    canvas.fillRect(0, static_cast<int>(horizon), width, height - static_cast<int>(horizon), TFT_DARKGREEN);

    Projection projections[kDrawDistance];
    const float segmentPosition = std::fmod(distance, Track::kSegmentLength);
    const std::size_t currentSegment =
        static_cast<std::size_t>(std::floor(distance / Track::kSegmentLength));

    float curveOffset = 0.0f;
    float curveVelocity = 0.0f;

    // Project the near-to-far road first so every band has both of its edges.
    for (int i = 0; i < kDrawDistance; ++i) {
        const std::size_t trackIndex = currentSegment + static_cast<std::size_t>(i);
        const Segment& segment = track.segment(trackIndex);

        curveVelocity += segment.curve;
        curveOffset += curveVelocity;

        const float z = std::max(1.0f, (static_cast<float>(i) + 1.0f) * Track::kSegmentLength - segmentPosition);
        const float scale = kCameraDepth / z;

        projections[i].y = std::min(static_cast<float>(height), horizon + kCameraHeight * scale);
        projections[i].center = static_cast<float>(width) * 0.5f + curveOffset * scale * kRoadHalfWidth;
        projections[i].roadHalfWidth = scale * kRoadHalfWidth;
        projections[i].trackIndex = trackIndex;
    }

    // Painter's algorithm: distant bands first, closest band last.
    for (int i = kDrawDistance - 1; i >= 0; --i) {
        const Projection& farEdge = projections[i];
        const Projection nearEdge = i == 0
                                         ? Projection{static_cast<float>(height),
                                                       static_cast<float>(width) * 0.5f,
                                                       projections[i].roadHalfWidth * 1.2f,
                                                       farEdge.trackIndex}
                                         : projections[i - 1];

        drawBand(farEdge, nearEdge, width, height, (farEdge.trackIndex & 1U) == 0U,
                 (farEdge.trackIndex & 1U) != 0U, (farEdge.trackIndex & 1U) == 0U);
    }

    drawPlayerCar(width, height, playerX);

    char speedLabel[20] = {};
    std::snprintf(speedLabel, sizeof(speedLabel), "SPD %03d", static_cast<int>(speed));
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    canvas.drawString(speedLabel, 3, 2);
}

void RoadRenderer::drawBand(const Projection& farEdge, const Projection& nearEdge, int width, int height,
                            bool centerLine, bool alternateGrass, bool alternateRumble)
{
    auto& canvas = GetHAL().canvas;

    const float top = std::max(0.0f, std::min(farEdge.y, static_cast<float>(height)));
    const float bottom = std::max(top, std::min(nearEdge.y, static_cast<float>(height)));
    const int topPixel = clamp_int(static_cast<int>(std::ceil(top)), 0, height - 1);
    const int bottomPixel = clamp_int(static_cast<int>(std::floor(bottom)), 0, height - 1);
    if (bottomPixel < topPixel) {
        return;
    }

    const uint32_t grassColor = alternateGrass ? TFT_GREEN : TFT_DARKGREEN;
    const uint32_t rumbleColor = alternateRumble ? TFT_WHITE : TFT_RED;
    canvas.fillRect(0, topPixel, width, bottomPixel - topPixel + 1, grassColor);

    const float span = std::max(0.001f, bottom - top);
    for (int y = topPixel; y <= bottomPixel; ++y) {
        const float t = std::clamp((static_cast<float>(y) - top) / span, 0.0f, 1.0f);
        const float center = farEdge.center + (nearEdge.center - farEdge.center) * t;
        const float roadHalfWidth = farEdge.roadHalfWidth +
                                     (nearEdge.roadHalfWidth - farEdge.roadHalfWidth) * t;
        const float rumbleHalfWidth = roadHalfWidth * (1.0f + kRumbleWidthRatio);

        const int rumbleLeft = clamp_int(static_cast<int>(std::lround(center - rumbleHalfWidth)), 0, width - 1);
        const int rumbleRight = clamp_int(static_cast<int>(std::lround(center + rumbleHalfWidth)), 0, width - 1);
        const int roadLeft = clamp_int(static_cast<int>(std::lround(center - roadHalfWidth)), 0, width - 1);
        const int roadRight = clamp_int(static_cast<int>(std::lround(center + roadHalfWidth)), 0, width - 1);

        if (rumbleRight >= rumbleLeft) {
            canvas.drawFastHLine(rumbleLeft, y, rumbleRight - rumbleLeft + 1, rumbleColor);
        }
        if (roadRight >= roadLeft) {
            canvas.drawFastHLine(roadLeft, y, roadRight - roadLeft + 1, TFT_DARKGREY);
        }

        if (centerLine) {
            const int lineHalfWidth = std::max(1, static_cast<int>(roadHalfWidth * 0.025f));
            const int lineLeft = clamp_int(static_cast<int>(std::lround(center)) - lineHalfWidth, 0, width - 1);
            const int lineRight = clamp_int(static_cast<int>(std::lround(center)) + lineHalfWidth, 0, width - 1);
            if (lineRight >= lineLeft) {
                canvas.drawFastHLine(lineLeft, y, lineRight - lineLeft + 1, TFT_YELLOW);
            }
        }
    }
}

void RoadRenderer::drawPlayerCar(int width, int height, float playerX)
{
    auto& canvas = GetHAL().canvas;
    const int centerX = static_cast<int>(std::lround(static_cast<float>(width) * 0.5f + playerX * 58.0f));
    const int bottomY = height - static_cast<int>(kCarBottomMargin);

    canvas.fillRect(centerX - 8, bottomY - 8, 16, 8, TFT_RED);
    canvas.fillRect(centerX - 5, bottomY - 11, 10, 4, TFT_ORANGE);
    canvas.fillRect(centerX - 4, bottomY - 9, 8, 2, TFT_CYAN);
    canvas.fillRect(centerX - 10, bottomY - 5, 3, 6, TFT_BLACK);
    canvas.fillRect(centerX + 7, bottomY - 5, 3, 6, TFT_BLACK);
    canvas.drawFastHLine(centerX - 6, bottomY - 1, 12, TFT_YELLOW);
}

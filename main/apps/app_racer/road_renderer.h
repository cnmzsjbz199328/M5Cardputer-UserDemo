#pragma once

#include "track.h"

#include <cstdint>
#include <vector>

struct TrafficCar {
    float distance = 0.0f;
    float lane = 0.0f;
    float speed = 0.0f;
    uint32_t color = 0;
    bool clean_overtake_eligible = false;
};

class RoadRenderer {
public:
    void render(const Track& track, float distance, float playerX, float speed,
                const std::vector<TrafficCar>& traffic, bool onRoad, int completedLaps, int32_t score);
    void renderResults(int32_t totalTimeMs, int32_t runBestLapMs, int32_t allTimeBestLapMs, int32_t score,
                       bool newRecord, bool flash);

private:
    struct Projection {
        float y = 0.0f;
        float center = 0.0f;
        float roadHalfWidth = 0.0f;
        std::size_t trackIndex = 0;
    };

    struct TrafficProjection {
        float z = 0.0f;
        float y = 0.0f;
        float center = 0.0f;
        float roadHalfWidth = 0.0f;
        float scale = 0.0f;
        int segmentSlot = 0;
        uint32_t color = 0;
    };

    void drawBand(const Projection& farEdge, const Projection& nearEdge, int width, int height,
                  bool centerLine, bool alternateGrass, bool alternateRumble, bool offRoad);
    void drawTrafficCar(const TrafficProjection& projection);
    void drawPlayerCar(int width, int height, float playerX);
};

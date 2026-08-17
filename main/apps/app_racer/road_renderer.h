#pragma once

#include "track.h"

class RoadRenderer {
public:
    void render(const Track& track, float distance, float playerX, float speed);

private:
    struct Projection {
        float y = 0.0f;
        float center = 0.0f;
        float roadHalfWidth = 0.0f;
        std::size_t trackIndex = 0;
    };

    void drawBand(const Projection& farEdge, const Projection& nearEdge, int width, int height,
                  bool centerLine, bool alternateGrass, bool alternateRumble);
    void drawPlayerCar(int width, int height, float playerX);
};

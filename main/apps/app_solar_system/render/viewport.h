#pragma once

#include <algorithm>
#include <cmath>

namespace solar {

constexpr int GRID_DIVISIONS = 12;
constexpr float SAFE_RADIUS_FRACTION = 0.90f;

struct Viewport {
    int   w;           // Display width in pixels
    int   h;           // Display height in pixels
    int   cx;          // Center X in pixels
    int   cy;          // Center Y in pixels
    float u;           // Base unit = min(w, h) / GRID_DIVISIONS
    float safeRadius;  // Largest circle fitting usable area

    static Viewport create(int width, int height) {
        Viewport vp;
        vp.w = width;
        vp.h = height;
        vp.cx = width / 2;
        vp.cy = height / 2;
        float minDim = static_cast<float>(std::min(width, height));
        vp.u = minDim / GRID_DIVISIONS;
        vp.safeRadius = (minDim / 2.0f) * SAFE_RADIUS_FRACTION;
        return vp;
    }

    /** Pixels per unit of u, rounded to at least one so nothing vanishes. */
    int units(float multiplesOfU) const {
        return std::max(1, static_cast<int>(std::lround(u * multiplesOfU)));
    }
};

/**
 * Scene units to pixels.
 *
 * Kept apart from `u` because the two answer different questions. `u` is the
 * typographic unit — margins, label offsets, ring gaps, everything whose job is
 * to look the same size relative to the panel. This is the astronomical one:
 * how much screen a scene unit buys, chosen so that at zoom 1 the reference
 * extent lands exactly on safeRadius.
 *
 * Deriving the scene scale from safeRadius is what makes the composition frame
 * identically on a 240x135 letterbox and a 240x240 square. Multiplying scene
 * units by `u` instead — which is what this code did — happens to work on
 * neither: at 240x135 it puts Neptune 124 px from the centre of a panel that is
 * 135 px tall, so the default view showed the inner system and cropped the rest
 * without any indication that it had.
 */
struct SceneTransform {
    float cx;
    float cy;
    float pixelsPerSceneUnit;
    float panX;  // scene units
    float panY;  // scene units

    static SceneTransform fit(const Viewport& vp, float extentScene, float zoom, float panX, float panY) {
        SceneTransform t;
        t.cx = static_cast<float>(vp.cx);
        t.cy = static_cast<float>(vp.cy);
        t.pixelsPerSceneUnit = (extentScene > 0 ? vp.safeRadius / extentScene : vp.u) * zoom;
        t.panX = panX;
        t.panY = panY;
        return t;
    }

    void toScreen(float sceneX, float sceneZ, int& outX, int& outY) const {
        outX = static_cast<int>(std::lround(cx + (sceneX + panX) * pixelsPerSceneUnit));
        // Screen y grows downward; the scene's does not.
        outY = static_cast<int>(std::lround(cy - (sceneZ + panY) * pixelsPerSceneUnit));
    }

    /** A scene-unit length in pixels, for body dots and other scene-sized art. */
    int lengthToPixels(float sceneLength) const {
        return static_cast<int>(std::lround(sceneLength * pixelsPerSceneUnit));
    }
};

} // namespace solar

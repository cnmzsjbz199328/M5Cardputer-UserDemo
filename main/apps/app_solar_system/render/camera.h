#pragma once

#include "intent.h"
#include "clock.h"
#include "bodies.h"
#include <algorithm>

namespace solar {

/** Index of the Sun, which is both body 0 and the heliocentric system's id. */
constexpr uint16_t SUN_INDEX = 0;

/**
 * Everything that orbits `systemIndex` and is currently drawn, plus the primary
 * itself. This is the one definition of "what is in this view"; the renderer
 * draws exactly this set and the selection cycles through exactly this set, so
 * the two cannot disagree about what is on screen.
 *
 * The heliocentric view holds the planets and the dwarf planets and no moons:
 * at that scale a moon is sub-pixel and sits on top of its primary, so 167 of
 * them cost a scene full of clutter and convey nothing. Moons live in their
 * primary's own view, where there is room to resolve them.
 *
 * Irregular families are held back even there. They were captured rather than
 * formed in place, and their eccentric inclined orbits cross everything else at
 * every angle — see the note on `Body::irregular`.
 */
inline bool body_in_system(uint16_t bodyIndex, uint16_t systemIndex) {
    if (bodyIndex >= BODY_COUNT || systemIndex >= BODY_COUNT) return false;
    if (bodyIndex == systemIndex) return true;

    const Body& body = BODIES[bodyIndex];
    if (systemIndex == SUN_INDEX) return body.kind == 1 || body.kind == 2;
    return body.kind == 3 && body.primary == systemIndex && body.irregular == 0;
}

/** Whether entering this body's system would show anything but the body. */
inline bool system_has_children(uint16_t bodyIndex) {
    if (bodyIndex >= BODY_COUNT) return false;
    for (uint16_t i = 0; i < BODY_COUNT; ++i) {
        if (i != bodyIndex && body_in_system(i, bodyIndex)) return true;
    }
    return false;
}

class Camera {
public:
    uint16_t selectedBodyIndex;
    // The primary whose system is on screen. SUN_INDEX is the heliocentric
    // view; anything else is that body's satellite view.
    uint16_t systemIndex;
    float panX;  // scene units, not pixels and not multiples of u
    float panY;
    float zoom;
    bool showLabels;
    bool showOrbits;

    Camera()
        : selectedBodyIndex(0), systemIndex(SUN_INDEX), panX(0.0f), panY(0.0f), zoom(1.0f),
          showLabels(true), showOrbits(true) {}

    void resetView() {
        panX = 0.0f;
        panY = 0.0f;
        zoom = 1.0f;
    }

    void handleInput(const InputEvent& evt, SimulationClock& clock) {
        switch (evt.intent) {
            // Stepping walks what is on screen, not the whole dataset. Cycling
            // through 185 bodies from a view that draws nine of them means most
            // presses select nothing visible, which reads as a broken key.
            case Intent::NextBody:
                selectedBodyIndex = stepSelection(1);
                break;
            case Intent::PrevBody:
                selectedBodyIndex = stepSelection(-1);
                break;
            case Intent::Zoom:
                zoom *= (1.0f + evt.value * ZOOM_STEP);
                // Not std::clamp: the Arduino ESP32 core appends its own
                // -std=gnu++11 after ours, and this header has to compile under
                // whichever standard wins.
                zoom = std::min(MAX_ZOOM, std::max(MIN_ZOOM, zoom));
                break;
            // Panning divides by zoom so that a drag moves the view by the same
            // apparent distance however far in it is — the alternative is a pan
            // that flies across the system when zoomed out and crawls when in.
            case Intent::PanX:
                panX += evt.value * PAN_STEP_SCENE / zoom;
                break;
            case Intent::PanY:
                panY += evt.value * PAN_STEP_SCENE / zoom;
                break;
            case Intent::ToggleLabels:
                showLabels = !showLabels;
                break;
            case Intent::ToggleOrbits:
                showOrbits = !showOrbits;
                break;
            case Intent::SpeedUp:
                clock.speedUp();
                break;
            case Intent::SpeedDown:
                clock.speedDown();
                break;
            case Intent::PauseToggle:
                clock.togglePause();
                break;
            // Select descends into the selected body's system; Back climbs out
            // of the current one. Only moons hang off a primary, so the tree is
            // two deep and climbing out always lands heliocentric — but going
            // through systemIndex rather than assuming that keeps the pair
            // symmetric if the dataset ever nests further.
            case Intent::Select:
                if (selectedBodyIndex != systemIndex && system_has_children(selectedBodyIndex)) {
                    systemIndex = selectedBodyIndex;
                    resetView();
                }
                break;
            case Intent::Back:
                if (systemIndex != SUN_INDEX) {
                    // Land on the body just left, not on the Sun: stepping out
                    // of Io should leave Jupiter selected, which is where the
                    // reader was looking.
                    selectedBodyIndex = systemIndex;
                    systemIndex = BODIES[systemIndex].kind == 3 ? BODIES[systemIndex].primary
                                                               : SUN_INDEX;
                } else {
                    selectedBodyIndex = SUN_INDEX;
                }
                resetView();
                break;
            case Intent::None:
            default:
                break;
        }
    }

private:
    /**
     * The next drawn body in `direction`, wrapping, or the current one if this
     * view somehow holds nothing else. Bounded by BODY_COUNT so a system with
     * no children cannot spin here; this runs on a key press, not per frame.
     */
    uint16_t stepSelection(int direction) const {
        for (uint16_t step = 1; step <= BODY_COUNT; ++step) {
            const int offset = static_cast<int>(step) * direction;
            int candidate = (static_cast<int>(selectedBodyIndex) + offset) % static_cast<int>(BODY_COUNT);
            if (candidate < 0) candidate += BODY_COUNT;
            if (body_in_system(static_cast<uint16_t>(candidate), systemIndex)) {
                return static_cast<uint16_t>(candidate);
            }
        }
        return selectedBodyIndex;
    }

    static constexpr float ZOOM_STEP = 0.15f;
    static constexpr float MIN_ZOOM = 0.1f;
    static constexpr float MAX_ZOOM = 50.0f;
    // One press slides the view by this many scene units at zoom 1 — a little
    // under a fifth of the inner system, so a held key crosses it in a beat.
    static constexpr float PAN_STEP_SCENE = 0.5f;
};

} // namespace solar

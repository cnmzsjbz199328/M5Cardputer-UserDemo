#pragma once

#include "viewport.h"
#include "camera.h"
#include "clock.h"
#include "bodies.h"
#include "kepler.h"
#include "scale.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace solar {

// Standard RGB565 colors
namespace Color {
    constexpr uint16_t Black       = 0x0000;
    constexpr uint16_t DeepSpace   = 0x0842; // Dark Navy
    constexpr uint16_t White       = 0xFFFF;
    constexpr uint16_t Gray        = 0x7BEF;
    constexpr uint16_t DimGray     = 0x39E7;
    constexpr uint16_t SunYellow   = 0xFFE0;
    constexpr uint16_t MercuryColor= 0x9CF3;
    constexpr uint16_t VenusColor  = 0xE650;
    constexpr uint16_t EarthBlue   = 0x3CFE;
    constexpr uint16_t MarsRed     = 0xF9A0;
    constexpr uint16_t JupiterCyan = 0xFEA0;
    constexpr uint16_t SaturnGold  = 0xF640;
    constexpr uint16_t UranusBlue  = 0x5DDF;
    constexpr uint16_t NeptuneBlue = 0x24BF;
    constexpr uint16_t SelectionCyan = 0x07FF;
}

/**
 * Everything positional in this file is either a multiple of Viewport::u or a
 * scene-unit length run through SceneTransform. There are no pixel literals,
 * and there is no room for one: the two panels' units differ by a factor of
 * 1.8, so a quantity tuned by eye on the square panel is wrong on the Cardputer
 * by nearly half, and a build that looks right is not evidence of anything.
 */
namespace Layout {
    constexpr float HUD_MARGIN_U = 0.5f;
    constexpr float HUD_LINE_GAP_U = 0.2f;
    constexpr float LABEL_GAP_U = 0.25f;
    constexpr float SELECTION_RING_GAP_U = 0.25f;
    constexpr float SELECTION_RING_MIN_U = 0.45f;
    // How far outside the panel a body may be before it stops being drawn.
    // Generous, because a dot's own radius and its selection ring both extend
    // past its centre.
    constexpr float CULL_MARGIN_U = 1.5f;
}

/**
 * The scene radius that fills safeRadius at zoom 1 — Neptune's orbit with a
 * little air around it.
 *
 * Framing on Neptune rather than on the outermost body in the dataset is a
 * choice about what the atlas is of: Eris sits at 15.6 scene units, so fitting
 * it would shrink the eight planets into the middle third of the panel to
 * accommodate one dwarf planet that is off in the dark most of its orbit. The
 * dwarf planets are still drawn and still reachable; they just start outside
 * the frame, which is where they are.
 */
constexpr float ATLAS_EXTENT_SCENE = 11.6f;

/**
 * The primary's drawn radius in a satellite view, in scene units.
 *
 * Fixed rather than derived from the body, so a system reads the same whether
 * its primary is Jupiter or Pluto — at true compressed scale Pluto's system
 * would be a speck with specks around it. In the heliocentric view the Sun
 * keeps the real scale, because there its size against the planets is itself
 * information.
 */
constexpr float SATELLITE_VIEW_PRIMARY_RADIUS = 0.8f;

/** A little air around the outermost drawn orbit, so labels have somewhere to go. */
constexpr float SATELLITE_VIEW_MARGIN = 1.12f;

/** Abstract canvas interface for board-independent 2D line-art drawing. */
class IGraphics {
public:
    virtual ~IGraphics() = default;
    virtual void clear(uint16_t color) = 0;
    virtual void drawPixel(int x, int y, uint16_t color) = 0;
    virtual void drawLine(int x0, int y0, int x1, int y1, uint16_t color) = 0;
    virtual void drawCircle(int x, int y, int radius, uint16_t color) = 0;
    virtual void fillCircle(int x, int y, int radius, uint16_t color) = 0;
    virtual void drawString(int x, int y, const char* str, uint16_t color) = 0;

    // Text metrics belong to whoever owns the font, which is the platform. The
    // renderer needs them to lay a HUD out without assuming a glyph size, which
    // is the same class of assumption as assuming a panel size.
    //
    // Not const: LovyanGFX measures text through the live device state, and
    // there is no const path to it.
    virtual int fontHeight() = 0;
    virtual int textWidth(const char* str) = 0;
};

class AtlasRenderer {
public:
    static constexpr size_t ORBIT_SEGMENTS = 64;

    AtlasRenderer() = default;

    void drawFrame(IGraphics& gfx, const Viewport& vp, const Camera& cam, const SimulationClock& clock) {
        gfx.clear(Color::DeepSpace);

        const bool heliocentric = cam.systemIndex == SUN_INDEX;
        const float extent = heliocentric ? ATLAS_EXTENT_SCENE : satelliteExtent(cam.systemIndex);

        const SceneTransform scene =
            SceneTransform::fit(vp, extent, cam.zoom, cam.panX, cam.panY);
        const scalar_t elapsedDays = clock.getElapsedDays();

        resolveColorsOnce();

        // Only this view's bodies get plotted now, so last frame's have to be
        // retired explicitly. Without this, leaving Jupiter's system would keep
        // drawing its moons at whatever screen positions they last held.
        for (BodyPlot& plot : plots_) plot.plotted = false;

        // The reference circle the composition is framed against.
        {
            int cx, cy;
            scene.toScreen(0, 0, cx, cy);
            gfx.drawCircle(cx, cy, static_cast<int>(std::lround(vp.safeRadius)), Color::DimGray);
        }

        if (heliocentric) {
            plotHeliocentric(gfx, vp, cam, scene, elapsedDays);
        } else {
            plotSatelliteSystem(gfx, vp, cam, scene, elapsedDays);
        }
        drawBodies(gfx, vp);
        drawSelection(gfx, vp, cam, clock);
    }

private:
    struct BodyPlot {
        float sceneX = 0;
        float sceneZ = 0;
        int x = 0;
        int y = 0;
        int radiusPx = 0;
        uint16_t color = Color::White;
        bool plotted = false;  // false wherever the dataset gave us nothing to draw
    };

    // Sized at compile time and owned by the renderer rather than built on the
    // stack each frame: 185 entries is several kilobytes, which is a large
    // fraction of an 8 KB task stack, and nothing in the frame loop allocates.
    std::array<BodyPlot, BODY_COUNT> plots_;
    std::array<Vec3, ORBIT_SEGMENTS> orbitPoints_;
    std::array<uint16_t, BODY_COUNT> colors_;
    bool colorsResolved_ = false;

    /** Body colours never change, and strcmp per body per frame is a waste. */
    void resolveColorsOnce() {
        if (colorsResolved_) return;
        for (uint16_t i = 0; i < BODY_COUNT; ++i) {
            colors_[i] = classifyColor(BODIES[i].id, BODIES[i].kind);
        }
        colorsResolved_ = true;
    }

    static OrbitalElementsRef elementsOf(const Body& body) {
        return OrbitalElementsRef{
            static_cast<scalar_t>(body.semi_major),
            static_cast<scalar_t>(body.eccentricity),
            static_cast<scalar_t>(body.inclination),
            static_cast<scalar_t>(body.period_days),
            static_cast<scalar_t>(body.peri),
            static_cast<scalar_t>(body.node),
            static_cast<scalar_t>(body.mean_anomaly),
            body.retrograde != 0
        };
    }

    void plotHeliocentric(IGraphics& gfx, const Viewport& vp, const Camera& cam,
                          const SceneTransform& scene, scalar_t elapsedDays) {
        for (uint16_t i = 0; i < BODY_COUNT; ++i) {
            const Body& body = BODIES[i];
            if (body.kind != 0 && body.kind != 1 && body.kind != 2) continue;

            BodyPlot& plot = plots_[i];
            plot.color = colors_[i];
            plot.radiusPx = std::max(1, scene.lengthToPixels(
                static_cast<float>(body_radius_to_scene(body.radius_km))));

            if (body.kind == 0) {
                plot.sceneX = 0;
                plot.sceneZ = 0;
            } else {
                const OrbitalElementsRef elem = elementsOf(body);
                const scalar_t sceneR = orbit_radius_au_to_scene(elem.semiMajorAxis);
                const Vec3 pos = position_in_orbit(elem, mean_anomaly_at(elem, elapsedDays), sceneR);
                plot.sceneX = static_cast<float>(pos.x);
                plot.sceneZ = static_cast<float>(pos.z);

                if (cam.showOrbits) {
                    drawOrbitPolyline(gfx, vp, scene, elem, sceneR, Color::DimGray);
                }
            }

            scene.toScreen(plot.sceneX, plot.sceneZ, plot.x, plot.y);
            plot.plotted = true;
        }
    }

    /** The moon's orbit radius in scene units, in its primary's own view. */
    static float moonSceneRadius(const Body& moon, const Body& primary) {
        return static_cast<float>(moon_orbit_radius_to_scene(
            moon.semi_major, primary.radius_km, SATELLITE_VIEW_PRIMARY_RADIUS));
    }

    /**
     * How far out the drawn moons reach, so the view frames its own contents.
     *
     * The heliocentric view can use a constant because its subject never
     * changes; a satellite view cannot, because Phobos sits a couple of Mars
     * radii out and Iapetus is sixty Saturn radii. Framing every system the
     * same way would put most of them in the middle sixth of the panel.
     */
    static float satelliteExtent(uint16_t systemIndex) {
        const Body& primary = BODIES[systemIndex];
        float extent = SATELLITE_VIEW_PRIMARY_RADIUS * 2.0f;

        for (uint16_t i = 0; i < BODY_COUNT; ++i) {
            if (!body_in_system(i, systemIndex) || i == systemIndex) continue;
            const Body& moon = BODIES[i];
            // Apoapsis, not the semi-major axis: an eccentric orbit spends its
            // slowest stretch beyond the latter, which is exactly when a moon
            // would slide off the panel.
            const float apoapsis = moonSceneRadius(moon, primary) * (1.0f + moon.eccentricity);
            extent = std::max(extent, apoapsis);
        }
        return extent * SATELLITE_VIEW_MARGIN;
    }

    /**
     * One primary and its regular moons, the primary at the origin.
     *
     * The primary is not solved for a position here — in its own view it is the
     * frame of reference, so it sits at the centre by definition and its
     * heliocentric motion is somebody else's view's business.
     */
    void plotSatelliteSystem(IGraphics& gfx, const Viewport& vp, const Camera& cam,
                             const SceneTransform& scene, scalar_t elapsedDays) {
        const Body& primary = BODIES[cam.systemIndex];

        BodyPlot& primaryPlot = plots_[cam.systemIndex];
        primaryPlot.sceneX = 0;
        primaryPlot.sceneZ = 0;
        primaryPlot.color = colors_[cam.systemIndex];
        primaryPlot.radiusPx =
            std::max(1, scene.lengthToPixels(SATELLITE_VIEW_PRIMARY_RADIUS));
        scene.toScreen(0, 0, primaryPlot.x, primaryPlot.y);
        primaryPlot.plotted = true;

        for (uint16_t i = 0; i < BODY_COUNT; ++i) {
            if (!body_in_system(i, cam.systemIndex) || i == cam.systemIndex) continue;

            const Body& moon = BODIES[i];
            const float sceneR = moonSceneRadius(moon, primary);

            OrbitalElementsRef elem = elementsOf(moon);
            elem.semiMajorAxis = static_cast<scalar_t>(sceneR);
            const Vec3 pos = position_in_orbit(elem, mean_anomaly_at(elem, elapsedDays), sceneR);

            if (cam.showOrbits) {
                drawOrbitPolyline(gfx, vp, scene, elem, static_cast<scalar_t>(sceneR), Color::DimGray);
            }

            BodyPlot& plot = plots_[i];
            plot.sceneX = static_cast<float>(pos.x);
            plot.sceneZ = static_cast<float>(pos.z);
            plot.color = colors_[i];
            plot.radiusPx = std::max(1, scene.lengthToPixels(
                static_cast<float>(body_radius_to_scene(moon.radius_km))));
            scene.toScreen(plot.sceneX, plot.sceneZ, plot.x, plot.y);
            plot.plotted = true;
        }
    }

    void drawBodies(IGraphics& gfx, const Viewport& vp) {
        const int margin = vp.units(Layout::CULL_MARGIN_U);
        for (uint16_t i = 0; i < BODY_COUNT; ++i) {
            const BodyPlot& plot = plots_[i];
            if (!plot.plotted) continue;
            if (!onScreen(plot.x, plot.y, vp, margin)) continue;
            gfx.fillCircle(plot.x, plot.y, plot.radiusPx, plot.color);
        }
    }

    void drawSelection(IGraphics& gfx, const Viewport& vp, const Camera& cam, const SimulationClock& clock) {
        const int margin = vp.units(Layout::CULL_MARGIN_U);
        const int hudMargin = vp.units(Layout::HUD_MARGIN_U);
        const int lineHeight = gfx.fontHeight() + vp.units(Layout::HUD_LINE_GAP_U);

        if (cam.selectedBodyIndex < BODY_COUNT && plots_[cam.selectedBodyIndex].plotted) {
            const BodyPlot& plot = plots_[cam.selectedBodyIndex];
            const Body& body = BODIES[cam.selectedBodyIndex];

            if (onScreen(plot.x, plot.y, vp, margin)) {
                const int ringRadius = std::max(vp.units(Layout::SELECTION_RING_MIN_U),
                                                plot.radiusPx + vp.units(Layout::SELECTION_RING_GAP_U));
                gfx.drawCircle(plot.x, plot.y, ringRadius, Color::SelectionCyan);

                if (cam.showLabels) {
                    const int gap = vp.units(Layout::LABEL_GAP_U);
                    const int labelWidth = gfx.textWidth(body.name_en);
                    // Flip the label to the other side rather than let it run
                    // off the panel. At 240x135 with the view panned, a body
                    // near the right edge is the normal case, not an edge one.
                    int labelX = plot.x + ringRadius + gap;
                    if (labelX + labelWidth > vp.w) {
                        labelX = plot.x - ringRadius - gap - labelWidth;
                    }
                    gfx.drawString(labelX, plot.y - gfx.fontHeight() / 2, body.name_en, Color::White);
                }
            }

            // The system, then the body, because "Io" alone does not say
            // whether you are looking at Jupiter's system or at the whole
            // atlas with Io somehow selected. Descending has to be visible.
            char hud[64];
            if (cam.systemIndex != SUN_INDEX && cam.selectedBodyIndex != cam.systemIndex) {
                std::snprintf(hud, sizeof(hud), "%s > %s",
                              BODIES[cam.systemIndex].name_en, body.name_en);
            } else {
                std::snprintf(hud, sizeof(hud), "%s", body.name_en);
            }
            gfx.drawString(hudMargin, hudMargin, hud, Color::White);

            std::snprintf(hud, sizeof(hud), "%s%s", clock.isPaused() ? "PAUSED " : "", clock.getSpeedName());
            gfx.drawString(hudMargin, hudMargin + lineHeight, hud, Color::Gray);
        }
    }

    static bool onScreen(int x, int y, const Viewport& vp, int margin) {
        return x >= -margin && x <= vp.w + margin && y >= -margin && y <= vp.h + margin;
    }

    void drawOrbitPolyline(IGraphics& gfx, const Viewport& vp, const SceneTransform& scene,
                           const OrbitalElementsRef& elem, scalar_t sceneR, uint16_t color) {
        sample_orbit_path(elem, orbitPoints_, ORBIT_SEGMENTS, sceneR);
        const int margin = vp.units(Layout::CULL_MARGIN_U);

        int prevX = 0, prevY = 0;
        for (size_t i = 0; i <= ORBIT_SEGMENTS; ++i) {
            const Vec3& pt = orbitPoints_[i % ORBIT_SEGMENTS];
            int currX, currY;
            scene.toScreen(static_cast<float>(pt.x), static_cast<float>(pt.z), currX, currY);

            if (i > 0 && (onScreen(prevX, prevY, vp, margin) || onScreen(currX, currY, vp, margin))) {
                gfx.drawLine(prevX, prevY, currX, currY, color);
            }
            prevX = currX;
            prevY = currY;
        }
    }

    static uint16_t classifyColor(const char* id, uint8_t kind) {
        if (std::strcmp(id, "mercury") == 0) return Color::MercuryColor;
        if (std::strcmp(id, "venus") == 0)   return Color::VenusColor;
        if (std::strcmp(id, "earth") == 0)   return Color::EarthBlue;
        if (std::strcmp(id, "mars") == 0)    return Color::MarsRed;
        if (std::strcmp(id, "jupiter") == 0) return Color::JupiterCyan;
        if (std::strcmp(id, "saturn") == 0)  return Color::SaturnGold;
        if (std::strcmp(id, "uranus") == 0)  return Color::UranusBlue;
        if (std::strcmp(id, "neptune") == 0) return Color::NeptuneBlue;
        if (kind == 0) return Color::SunYellow;
        if (kind == 2) return Color::Gray; // Dwarf planet
        if (kind == 3) return Color::Gray; // Moon
        return Color::White;
    }
};

} // namespace solar

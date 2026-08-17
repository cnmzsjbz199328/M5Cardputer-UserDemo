#pragma once

#include "constants.h"
#include <cmath>
#include <algorithm>

namespace solar {

constexpr scalar_t ORBIT_COEFFICIENT = 2.9;
constexpr scalar_t ORBIT_EXPONENT = 0.4;

constexpr scalar_t BODY_COEFFICIENT = 0.0094;
constexpr scalar_t BODY_EXPONENT = 1.0 / 3.0;
constexpr scalar_t MIN_BODY_RADIUS = 0.03;
constexpr scalar_t MAX_BODY_RADIUS = 0.85;

constexpr scalar_t MOON_ORBIT_EXPONENT = 0.45;
constexpr scalar_t MOON_ORBIT_SPREAD = 0.55;

inline scalar_t clamp(scalar_t value, scalar_t minVal, scalar_t maxVal) {
    return std::min(maxVal, std::max(minVal, value));
}

/** Convert a heliocentric semi-major axis in AU to a scene-unit orbit radius. */
inline scalar_t orbit_radius_au_to_scene(scalar_t semiMajorAxisAU) {
    if (semiMajorAxisAU <= 0) return 0;
    return ORBIT_COEFFICIENT * std::pow(semiMajorAxisAU, ORBIT_EXPONENT);
}

/** Convert a heliocentric semi-major axis in km to a scene-unit orbit radius. */
inline scalar_t orbit_radius_to_scene(scalar_t semiMajorAxisKm) {
    return orbit_radius_au_to_scene(semiMajorAxisKm / scalar::AU_KM);
}

/** Convert a body's mean radius in km to a scene-unit drawing radius. */
inline scalar_t body_radius_to_scene(scalar_t meanRadiusKm) {
    if (meanRadiusKm <= 0) return MIN_BODY_RADIUS;
    scalar_t raw = BODY_COEFFICIENT * std::pow(meanRadiusKm, BODY_EXPONENT);
    return clamp(raw, MIN_BODY_RADIUS, MAX_BODY_RADIUS);
}

/** Reports whether a body's drawn size is at the floor rather than real measurement. */
inline bool is_at_minimum_size(scalar_t meanRadiusKm) {
    scalar_t raw = BODY_COEFFICIENT * std::pow(std::max(meanRadiusKm, scalar::ZERO), BODY_EXPONENT);
    return raw < MIN_BODY_RADIUS;
}

/** Satellite orbit radius scaled relative to primary. */
inline scalar_t moon_orbit_radius_to_scene(scalar_t semiMajorAxisKm, scalar_t primaryRadiusKm, scalar_t primarySceneRadius) {
    if (primaryRadiusKm <= 0 || semiMajorAxisKm <= 0) return primarySceneRadius * scalar::TWO;

    scalar_t inPrimaryRadii = semiMajorAxisKm / primaryRadiusKm;
    scalar_t compressed = std::pow(inPrimaryRadii, MOON_ORBIT_EXPONENT);
    return primarySceneRadius * (scalar::ONE + compressed * MOON_ORBIT_SPREAD);
}

/** Orbital compression factor at a given semi-major axis. */
inline scalar_t orbit_compression_factor(scalar_t semiMajorAxisKm) {
    scalar_t au = semiMajorAxisKm / scalar::AU_KM;
    if (au <= 0) return scalar::ONE;
    scalar_t drawnRatio = std::pow(au, ORBIT_EXPONENT);
    return au / drawnRatio;
}

} // namespace solar

#pragma once

#include "constants.h"
#include <cmath>
#include <cstddef>
#include <limits>

namespace solar {

struct Vec3 {
    scalar_t x;
    scalar_t y;
    scalar_t z;
};

/** Orbital parameters passed to kepler functions. */
struct OrbitalElementsRef {
    scalar_t semiMajorAxis;  // AU (or km for moons)
    scalar_t eccentricity;
    scalar_t inclinationRad;
    scalar_t orbitalPeriodDays;
    scalar_t argumentOfPeriapsisRad;
    scalar_t longitudeOfAscendingNodeRad;
    scalar_t meanAnomalyAtEpochRad;
    bool isRetrograde;
};

/**
 * Where Newton stops. Expressed in units of scalar_t's own epsilon rather than
 * as a literal, because a literal that is right for double is a performance bug
 * in float: 1e-12 is below what a float can represent at these magnitudes, so
 * the loop can never satisfy it and every call runs to the iteration cap.
 */
constexpr scalar_t KEPLER_TOLERANCE = std::numeric_limits<scalar_t>::epsilon() * 16;

/** Wrap angle into [0, 2π). */
inline scalar_t wrap_angle(scalar_t radians) {
    scalar_t wrapped = std::fmod(radians, scalar::TAU);
    return wrapped < 0 ? wrapped + scalar::TAU : wrapped;
}

/**
 * Solve Kepler's equation M = E - e * sin(E) for eccentric anomaly E.
 *
 * The seed E0 = M + e * sin(M) is a first-order approximation rather than the
 * usual E0 = M, which buys roughly one Newton step for one sine. From it, four
 * iterations reach KEPLER_TOLERANCE for every eccentricity in the dataset;
 * maxIterations is a guard against a pathological input, not the normal path.
 */
inline scalar_t solve_kepler(scalar_t meanAnomaly, scalar_t eccentricity,
                             scalar_t tolerance = KEPLER_TOLERANCE, int maxIterations = 32) {
    if (!std::isfinite(meanAnomaly) || !std::isfinite(eccentricity)) {
        return 0;
    }
    if (eccentricity < 0 || eccentricity >= 1) {
        return 0;
    }

    scalar_t m = wrap_angle(meanAnomaly);

    // Circular orbit needs no iteration: E == M exactly.
    if (eccentricity == 0) return m;

    scalar_t e = m + eccentricity * std::sin(m);

    for (int i = 0; i < maxIterations; ++i) {
        scalar_t f = e - eccentricity * std::sin(e) - m;
        scalar_t fPrime = scalar::ONE - eccentricity * std::cos(e);
        scalar_t delta = f / fPrime;
        e -= delta;
        if (std::abs(delta) < tolerance) return e;
    }

    return e;
}

/** True anomaly from eccentric anomaly. */
inline scalar_t true_anomaly_from_eccentric(scalar_t eccentricAnomaly, scalar_t eccentricity) {
    scalar_t halfE = eccentricAnomaly / scalar::TWO;
    scalar_t factor = std::sqrt((scalar::ONE + eccentricity) / (scalar::ONE - eccentricity));
    return scalar::TWO * std::atan2(factor * std::sin(halfE), std::cos(halfE));
}

/** Distance from the focus at a given eccentric anomaly. */
inline scalar_t radius_at_eccentric_anomaly(scalar_t semiMajorAxis, scalar_t eccentricity, scalar_t eccentricAnomaly) {
    return semiMajorAxis * (scalar::ONE - eccentricity * std::cos(eccentricAnomaly));
}

/** Mean anomaly at a given elapsed time in days. */
inline scalar_t mean_anomaly_at(const OrbitalElementsRef& orbit, scalar_t elapsedDays) {
    scalar_t atEpoch = orbit.meanAnomalyAtEpochRad;
    scalar_t rate = scalar::TAU / orbit.orbitalPeriodDays;
    scalar_t direction = orbit.isRetrograde ? -scalar::ONE : scalar::ONE;
    return wrap_angle(atEpoch + direction * rate * elapsedDays);
}

/**
 * The six trig terms of the 3-1-3 rotation that carries in-plane coordinates
 * into the orbit's reference frame.
 *
 * They depend only on the orbit, not on where the body is along it, so a
 * 64-point polyline needs one of these and not sixty-four. Hoisting them out is
 * worth doing: the sines dominate the per-frame cost of drawing orbits, and
 * there are more orbit samples in a frame than there are bodies by two orders
 * of magnitude.
 */
struct FrameRotation {
    scalar_t cosW, sinW;  // argument of periapsis
    scalar_t cosI, sinI;  // inclination
    scalar_t cosO, sinO;  // longitude of ascending node

    static FrameRotation from(const OrbitalElementsRef& orbit) {
        return {
            std::cos(orbit.argumentOfPeriapsisRad),      std::sin(orbit.argumentOfPeriapsisRad),
            std::cos(orbit.inclinationRad),              std::sin(orbit.inclinationRad),
            std::cos(orbit.longitudeOfAscendingNodeRad), std::sin(orbit.longitudeOfAscendingNodeRad)
        };
    }
};

/** Rotate in-plane coordinates into the orbit's reference frame (3-1-3 sequence, Y-up). */
inline Vec3 orient_to_frame(const FrameRotation& r, scalar_t xPlane, scalar_t zPlane) {
    // 1. Rotate about Y by argument of periapsis.
    scalar_t x1 = xPlane * r.cosW - zPlane * r.sinW;
    scalar_t z1 = xPlane * r.sinW + zPlane * r.cosW;

    // 2. Tilt about X by inclination.
    scalar_t y2 = z1 * r.sinI;
    scalar_t z2 = z1 * r.cosI;

    // 3. Rotate about Y by longitude of ascending node.
    return {
        x1 * r.cosO - z2 * r.sinO,
        y2,
        x1 * r.sinO + z2 * r.cosO
    };
}

/** Convenience overload for a single point, where there is nothing to hoist. */
inline Vec3 orient_to_frame(const OrbitalElementsRef& orbit, scalar_t xPlane, scalar_t zPlane) {
    return orient_to_frame(FrameRotation::from(orbit), xPlane, zPlane);
}

/** Position of an orbiting body relative to its primary. */
inline Vec3 position_in_orbit(const OrbitalElementsRef& orbit, scalar_t meanAnomaly, scalar_t semiMajorAxisOverride = -1.0) {
    scalar_t a = (semiMajorAxisOverride > 0) ? semiMajorAxisOverride : orbit.semiMajorAxis;
    scalar_t e = orbit.eccentricity;
    scalar_t eccentricAnomaly = solve_kepler(meanAnomaly, e);

    scalar_t b = a * std::sqrt(scalar::ONE - e * e);
    scalar_t xPlane = a * (std::cos(eccentricAnomaly) - e);
    scalar_t zPlane = b * std::sin(eccentricAnomaly);

    return orient_to_frame(orbit, xPlane, zPlane);
}

/**
 * Sample orbit path into a fixed-capacity buffer or vector.
 *
 * Sampled uniformly in eccentric anomaly rather than in true anomaly or in
 * time: uniform E puts points evenly around the ellipse's circumference, which
 * is what a polyline wants. Uniform time would crowd them at apoapsis, where
 * the curve is straightest and needs them least.
 */
template <typename Container>
inline void sample_orbit_path(const OrbitalElementsRef& orbit, Container& outPoints, size_t segments, scalar_t semiMajorAxisOverride = -1.0) {
    if (segments < 3) return;

    scalar_t a = (semiMajorAxisOverride > 0) ? semiMajorAxisOverride : orbit.semiMajorAxis;
    scalar_t e = orbit.eccentricity;
    scalar_t b = a * std::sqrt(scalar::ONE - e * e);
    const FrameRotation rotation = FrameRotation::from(orbit);
    const scalar_t step = scalar::TAU / static_cast<scalar_t>(segments);

    for (size_t i = 0; i < segments; ++i) {
        scalar_t eccentricAnomaly = static_cast<scalar_t>(i) * step;
        scalar_t xPlane = a * (std::cos(eccentricAnomaly) - e);
        scalar_t zPlane = b * std::sin(eccentricAnomaly);
        outPoints[i] = orient_to_frame(rotation, xPlane, zPlane);
    }
}

/** Periapsis distance. */
inline scalar_t periapsis_distance(scalar_t semiMajorAxis, scalar_t eccentricity) {
    return semiMajorAxis * (scalar::ONE - eccentricity);
}

/** Apoapsis distance. */
inline scalar_t apoapsis_distance(scalar_t semiMajorAxis, scalar_t eccentricity) {
    return semiMajorAxis * (scalar::ONE + eccentricity);
}

/** Orbital speed via vis-viva equation (km/s). */
inline scalar_t orbital_speed(scalar_t distanceKm, scalar_t semiMajorAxisKm, scalar_t gravitationalParameter) {
    return std::sqrt(gravitationalParameter * (scalar::TWO / distanceKm - scalar::ONE / semiMajorAxisKm));
}

} // namespace solar

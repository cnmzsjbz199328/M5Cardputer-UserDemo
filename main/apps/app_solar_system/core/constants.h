#pragma once

#include <cstdint>
#include <cmath>

namespace solar {

// The host runs in double so that analytic identities can be asserted with
// headroom to spare; the device runs in float because the ESP32-S3's FPU is
// single-precision. SOLAR_FORCE_FLOAT builds the host in float anyway — the
// `native_float` env uses it to run the same test suite against the precision
// that actually ships. See docs/PORTING.md.
#if defined(TARGET_NATIVE) && !defined(SOLAR_FORCE_FLOAT)
using scalar_t = double;
#else
using scalar_t = float;
#endif

/** Astronomical unit, kilometres. IAU 2012 exact definition. */
constexpr double AU_KM = 149597870.7;

/** Days in a Julian year, exact by definition. */
constexpr double DAYS_PER_JULIAN_YEAR = 365.25;

/** Earth's sidereal orbital period, days. Used as the unit of "one year" in the UI. */
constexpr double EARTH_ORBITAL_PERIOD_DAYS = 365.256363004;

/** Earth's equatorial radius, kilometres — the unit planetary sizes are compared in. */
constexpr double EARTH_RADIUS_KM = 6378.137;

/** Earth's mass, kilograms. */
constexpr double EARTH_MASS_KG = 5.972168e24;

/** Solar radius, kilometres. IAU nominal. */
constexpr double SUN_RADIUS_KM = 695700.0;

/** Solar mass, kilograms. */
constexpr double SUN_MASS_KG = 1.98841e30;

/** Seconds in a day. */
constexpr double SECONDS_PER_DAY = 86400.0;

/** Absolute zero offset, for kelvin <-> celsius display conversion. */
constexpr double KELVIN_OFFSET = 273.15;

/** Degrees to radians factor. */
constexpr double DEG_TO_RAD = 3.1415926535897932384626433832795 / 180.0;

/** Radians to degrees factor. */
constexpr double RAD_TO_DEG = 180.0 / 3.1415926535897932384626433832795;

/** Full turn in radians. */
constexpr double TAU = 6.283185307179586476925286766559;

constexpr double PI = 3.1415926535897932384626433832795;

/**
 * The constants above are `double` because that is how they are defined, and a
 * definition should not lose digits to the platform reading it. But mixing one
 * into float arithmetic promotes the whole expression: `someFloat / AU_KM` is a
 * double divide, and on the ESP32-S3 a double divide leaves the FPU for a
 * software routine costing roughly an order of magnitude.
 *
 * So anything that appears in per-frame arithmetic gets a scalar_t twin here,
 * and core/ uses these rather than the double originals.
 */
namespace scalar {

constexpr scalar_t AU_KM = static_cast<scalar_t>(solar::AU_KM);
constexpr scalar_t TAU = static_cast<scalar_t>(solar::TAU);
constexpr scalar_t PI = static_cast<scalar_t>(solar::PI);
constexpr scalar_t DEG_TO_RAD = static_cast<scalar_t>(solar::DEG_TO_RAD);
constexpr scalar_t RAD_TO_DEG = static_cast<scalar_t>(solar::RAD_TO_DEG);

constexpr scalar_t ZERO = static_cast<scalar_t>(0.0);
constexpr scalar_t ONE = static_cast<scalar_t>(1.0);
constexpr scalar_t TWO = static_cast<scalar_t>(2.0);

} // namespace scalar

} // namespace solar

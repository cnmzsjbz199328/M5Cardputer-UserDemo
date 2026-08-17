#include "track.h"

#include <cmath>

Track::Track()
{
    reset();
}

void Track::reset()
{
    _segments.clear();
    _segments.reserve(256);
    _current_elevation = 0.0f;

    // Keep the loop symmetric enough that the transition from the end back
    // to the beginning does not feel like a sudden steering correction.
    // The elevation deltas create two gentle hills and two curve/hill combos.
    addStretch(20, 0.0f, 0.0f);
    addStretch(24, 0.004f, 8.0f);
    addStretch(10, 0.0f, 0.0f);
    addStretch(24, 0.008f, -8.0f);
    addStretch(18, 0.0f, 0.0f);
    addStretch(28, -0.004f, -6.0f);
    addStretch(12, 0.0f, 0.0f);
    addStretch(28, -0.010f, 6.0f);
    addStretch(20, 0.0f, 0.0f);
    addStretch(24, 0.005f, 12.0f);
    addStretch(12, 0.0f, 0.0f);
    addStretch(24, -0.008f, -12.0f);
    addStretch(26, 0.0f, 0.0f);
    addStretch(20, 0.0f, 0.0f);
}

void Track::addStretch(std::size_t count, float curve, float elevationStep)
{
    for (std::size_t i = 0; i < count; ++i) {
        _segments.push_back(Segment{curve, _current_elevation});
        _current_elevation += elevationStep;
    }
}

const Segment& Track::segment(std::size_t index) const
{
    return _segments[index % _segments.size()];
}

const Segment& Track::segmentAtDistance(float distance) const
{
    float wrapped_distance = std::fmod(distance, length());
    if (wrapped_distance < 0.0f) {
        wrapped_distance += length();
    }

    const auto index = static_cast<std::size_t>(wrapped_distance / kSegmentLength);
    return segment(index);
}

float Track::heightAtDistance(float distance) const
{
    float wrapped_distance = std::fmod(distance, length());
    if (wrapped_distance < 0.0f) {
        wrapped_distance += length();
    }

    const auto index = static_cast<std::size_t>(wrapped_distance / kSegmentLength);
    const float fraction = std::fmod(wrapped_distance, kSegmentLength) / kSegmentLength;
    const Segment& current = segment(index);
    const Segment& next = segment(index + 1);
    return current.y + (next.y - current.y) * fraction;
}

float Track::forwardDistance(float from, float to) const
{
    float distance = std::fmod(to - from, length());
    if (distance < 0.0f) {
        distance += length();
    }
    return distance;
}

std::size_t Track::size() const
{
    return _segments.size();
}

float Track::length() const
{
    return static_cast<float>(_segments.size()) * kSegmentLength;
}

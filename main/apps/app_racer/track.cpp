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

    // Keep the loop symmetric enough that the transition from the end back
    // to the beginning does not feel like a sudden steering correction.
    addStretch(20, 0.0f);
    addStretch(26, 0.004f);
    addStretch(14, 0.0f);
    addStretch(20, 0.010f);
    addStretch(22, 0.0f);
    addStretch(30, -0.004f);
    addStretch(16, 0.0f);
    addStretch(22, -0.010f);
    addStretch(24, 0.0f);
    addStretch(30, 0.005f);
    addStretch(18, 0.0f);
    addStretch(20, -0.008f);
    addStretch(26, 0.0f);
    addStretch(20, 0.0f);
}

void Track::addStretch(std::size_t count, float curve)
{
    for (std::size_t i = 0; i < count; ++i) {
        _segments.push_back(Segment{curve});
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

std::size_t Track::size() const
{
    return _segments.size();
}

float Track::length() const
{
    return static_cast<float>(_segments.size()) * kSegmentLength;
}

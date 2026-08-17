#pragma once

#include <cstddef>
#include <vector>

struct Segment {
    float curve = 0.0f;
    float y = 0.0f;
};

class Track {
public:
    static constexpr float kSegmentLength = 20.0f;

    Track();

    void reset();

    const Segment& segment(std::size_t index) const;
    const Segment& segmentAtDistance(float distance) const;
    float heightAtDistance(float distance) const;
    float forwardDistance(float from, float to) const;

    std::size_t size() const;
    float length() const;

private:
    std::vector<Segment> _segments;
    float _current_elevation = 0.0f;

    void addStretch(std::size_t count, float curve, float elevationStep);
};

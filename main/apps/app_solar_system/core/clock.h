#pragma once

#include "constants.h"

namespace solar {

// Days since J2000.0 at default epoch (2026-01-01 00:00 UTC = 9496.5 days)
constexpr scalar_t DEFAULT_BUILD_EPOCH_DAYS = 9496.5;

struct TimeScalePreset {
    const char* name;
    scalar_t daysPerRealSecond;
};

// Speed presets: Default is Earth completing an orbit in ~6 seconds (60.87 days/sec)
constexpr TimeScalePreset TIME_SCALES[] = {
    { "Realtime", static_cast<scalar_t>(1.0 / 86400.0) },
    { "1 hr/s",   static_cast<scalar_t>(3600.0 / 86400.0) },
    { "1 day/s",  static_cast<scalar_t>(1.0) },
    { "1 wk/s",   static_cast<scalar_t>(7.0) },
    { "1 mo/s",   static_cast<scalar_t>(30.4375) },
    { "Atlas Default (6s/yr)", static_cast<scalar_t>(365.25 / 6.0) },
    { "1 yr/s",   static_cast<scalar_t>(365.25) }
};

constexpr size_t TIME_SCALE_COUNT = sizeof(TIME_SCALES) / sizeof(TIME_SCALES[0]);
constexpr size_t DEFAULT_TIME_SCALE_INDEX = 5; // 6s per year

class SimulationClock {
public:
    explicit SimulationClock(scalar_t epochDays = DEFAULT_BUILD_EPOCH_DAYS)
        : epochDays_(epochDays), elapsedDays_(0), speedIndex_(DEFAULT_TIME_SCALE_INDEX), isPaused_(false) {}

    void setEpoch(scalar_t epochDays) { epochDays_ = epochDays; }
    scalar_t getEpoch() const { return epochDays_; }

    void setSpeedIndex(size_t index) {
        if (index < TIME_SCALE_COUNT) {
            speedIndex_ = index;
        }
    }
    size_t getSpeedIndex() const { return speedIndex_; }
    void speedUp() {
        if (speedIndex_ + 1 < TIME_SCALE_COUNT) ++speedIndex_;
    }
    void speedDown() {
        if (speedIndex_ > 0) --speedIndex_;
    }

    void togglePause() { isPaused_ = !isPaused_; }
    void setPaused(bool paused) { isPaused_ = paused; }
    bool isPaused() const { return isPaused_; }

    void update(scalar_t deltaSeconds) {
        if (!isPaused_) {
            elapsedDays_ += deltaSeconds * TIME_SCALES[speedIndex_].daysPerRealSecond;
        }
    }

    scalar_t getElapsedDays() const { return elapsedDays_; }
    scalar_t getAbsoluteDays() const { return epochDays_ + elapsedDays_; }

    const char* getSpeedName() const { return TIME_SCALES[speedIndex_].name; }

private:
    scalar_t epochDays_;
    scalar_t elapsedDays_;
    size_t speedIndex_;
    bool isPaused_;
};

} // namespace solar

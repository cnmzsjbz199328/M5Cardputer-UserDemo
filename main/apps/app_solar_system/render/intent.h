#pragma once

#include <cstdint>

namespace solar {

enum class Intent : uint8_t {
    None,
    Select,
    Back,
    PanX,
    PanY,
    Zoom,
    NextBody,
    PrevBody,
    ToggleLabels,
    ToggleOrbits,
    SpeedUp,
    SpeedDown,
    PauseToggle,
};

struct InputEvent {
    Intent intent;
    float  value; // Signed magnitude for Pan/Zoom; unused otherwise
};

} // namespace solar

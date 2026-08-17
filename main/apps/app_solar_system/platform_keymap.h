#pragma once

#include "render/intent.h"
#include <hal/keyboard/keyboard.h>

namespace solar {

inline InputEvent cardputerIntent(const Keyboard::KeyEvent_t& keyEvent)
{
    switch (keyEvent.keyCode) {
        case KEY_COMMA:      return {Intent::PrevBody, 0.0f};
        case KEY_SLASH:      return {Intent::NextBody, 0.0f};
        case KEY_SEMICOLON:  return {Intent::PanY, 1.0f};
        case KEY_DOT:        return {Intent::PanY, -1.0f};

        case KEY_EQUAL:      return {Intent::Zoom, 1.0f};
        case KEY_MINUS:      return {Intent::Zoom, -1.0f};

        case KEY_A:          return {Intent::PanX, -1.0f};
        case KEY_D:          return {Intent::PanX, 1.0f};

        case KEY_SPACE:      return {Intent::PauseToggle, 0.0f};
        case KEY_L:          return {Intent::ToggleLabels, 0.0f};
        case KEY_O:          return {Intent::ToggleOrbits, 0.0f};
        case KEY_LEFTBRACE:  return {Intent::SpeedDown, 0.0f};
        case KEY_RIGHTBRACE: return {Intent::SpeedUp, 0.0f};
        case KEY_ENTER:      return {Intent::Select, 0.0f};
        case KEY_BACKSPACE:  return {Intent::Back, 0.0f};
        default:             return {Intent::None, 0.0f};
    }
}

} // namespace solar

/*
 * SPDX-License-Identifier: MIT
 */

#include "panel_keys.h"

#include "lvgl.h"

namespace screen {

bool to_panel_input(uint32_t key, app::PanelInput& input) {
    switch (key) {
        case LV_KEY_UP:
        case LV_KEY_PREV:
            input = app::PanelInput::Up;
            return true;
        case LV_KEY_DOWN:
        case LV_KEY_NEXT:
            input = app::PanelInput::Down;
            return true;
        case LV_KEY_LEFT:
            input = app::PanelInput::Left;
            return true;
        case LV_KEY_RIGHT:
            input = app::PanelInput::Right;
            return true;
        case LV_KEY_ENTER:
            input = app::PanelInput::Enter;
            return true;
        case LV_KEY_ESC:
            input = app::PanelInput::Escape;
            return true;
        case LV_KEY_BACKSPACE:
        case LV_KEY_DEL:
            input = app::PanelInput::Backspace;
            return true;
        default:
            if (key >= '0' && key <= '9') {
                input = app::PanelInput::Digit;
                return true;
            }
            return false;
    }
}

} // namespace screen

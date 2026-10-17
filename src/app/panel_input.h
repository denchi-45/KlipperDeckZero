/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

namespace app {

// Keys the page panels react to, independent of LVGL key codes.
enum class PanelInput {
    Up,
    Down,
    Left,
    Right,
    Enter,
    Escape,
    Backspace,
    Digit,
};

} // namespace app

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "panel_input.h"
#include "printer_state.h"

#include <cstdint>

namespace app {

enum class ControlButton {
    PauseResume,
    Cancel,
    EmergencyStop,
};

// True while a print is running or paused, so it can be paused, resumed or cancelled.
bool has_active_print(model::PrintState state);

// Keyboard control of the running print, independent of LVGL.
// Left/Right pick a button. Enter pauses or resumes at once; cancelling needs a second Enter
// within kConfirmWindowMs; the emergency stop needs Enter held down (a long press).
class ControlPanel {
public:
    enum class Result {
        Ignored,       // not used by the panel; the caller may treat it as a shortcut
        Handled,
        Pause,
        Resume,
        NoPrint,       // the button needs a running or paused print
        ArmCancel,     // ask for a second Enter
        Cancel,
        HoldToStop,    // a short Enter on the emergency stop: explain the long press
        EmergencyStop,
    };

    static constexpr uint32_t kConfirmWindowMs = 3000;

    Result handle(PanelInput input, bool long_pressed, model::PrintState state, uint32_t now_ms);

    ControlButton focused() const;
    // True when a cancel was armed and its window has not passed.
    bool cancel_armed(uint32_t now_ms) const;
    void disarm();

private:
    ControlButton focused_{ControlButton::PauseResume};
    bool cancel_armed_{false};
    uint32_t armed_at_{0};
};

} // namespace app

/*
 * SPDX-License-Identifier: MIT
 */

#include "control_panel.h"

#include <algorithm>

namespace app {

bool has_active_print(model::PrintState state) {
    return state == model::PrintState::Printing || state == model::PrintState::Paused;
}

ControlPanel::Result ControlPanel::handle(PanelInput input,
                                          bool long_pressed,
                                          model::PrintState state,
                                          uint32_t now_ms) {
    if (input == PanelInput::Left || input == PanelInput::Right) {
        if (long_pressed) {
            return Result::Handled;
        }
        const int step = input == PanelInput::Right ? 1 : -1;
        const int last = static_cast<int>(ControlButton::EmergencyStop);
        focused_ = static_cast<ControlButton>(std::clamp(static_cast<int>(focused_) + step, 0, last));
        disarm();
        return Result::Handled;
    }
    if (input != PanelInput::Enter) {
        // Any other key abandons a pending cancel.
        disarm();
        return Result::Ignored;
    }

    if (focused_ == ControlButton::EmergencyStop) {
        return long_pressed ? Result::EmergencyStop : Result::HoldToStop;
    }
    if (long_pressed) {
        // The short press already acted; holding Enter must not repeat it.
        return Result::Handled;
    }
    if (!has_active_print(state)) {
        disarm();
        return Result::NoPrint;
    }
    if (focused_ == ControlButton::PauseResume) {
        return state == model::PrintState::Paused ? Result::Resume : Result::Pause;
    }

    // Unsigned subtraction keeps working when the millisecond tick wraps.
    if (cancel_armed(now_ms)) {
        disarm();
        return Result::Cancel;
    }
    cancel_armed_ = true;
    armed_at_ = now_ms;
    return Result::ArmCancel;
}

ControlButton ControlPanel::focused() const {
    return focused_;
}

bool ControlPanel::cancel_armed(uint32_t now_ms) const {
    return cancel_armed_ && now_ms - armed_at_ <= kConfirmWindowMs;
}

void ControlPanel::disarm() {
    cancel_armed_ = false;
}

} // namespace app

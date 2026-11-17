/*
 * SPDX-License-Identifier: MIT
 */

#include "control_panel.h"
#include "test_framework.h"

namespace {

using app::ControlButton;
using app::ControlPanel;
using app::PanelInput;
using model::PrintState;
using Result = ControlPanel::Result;

Result enter(ControlPanel& panel, PrintState state, uint32_t now_ms = 0, bool long_pressed = false) {
    return panel.handle(PanelInput::Enter, long_pressed, state, now_ms);
}

void focus(ControlPanel& panel, ControlButton button) {
    for (int i = 0; i < 3; ++i) {
        panel.handle(PanelInput::Left, false, PrintState::Printing, 0);
    }
    for (int i = 0; i < static_cast<int>(button); ++i) {
        panel.handle(PanelInput::Right, false, PrintState::Printing, 0);
    }
}

} // namespace

TEST_CASE("control: enter pauses a running print and resumes a paused one") {
    ControlPanel panel;
    CHECK(panel.focused() == ControlButton::PauseResume);
    CHECK(enter(panel, PrintState::Printing) == Result::Pause);
    CHECK(enter(panel, PrintState::Paused) == Result::Resume);
    CHECK(enter(panel, PrintState::Standby) == Result::NoPrint);
    CHECK(enter(panel, PrintState::Complete) == Result::NoPrint);
}

TEST_CASE("control: cancelling needs a second enter within the window") {
    ControlPanel panel;
    focus(panel, ControlButton::Cancel);
    CHECK(enter(panel, PrintState::Printing, 1000) == Result::ArmCancel);
    CHECK(panel.cancel_armed(1000 + ControlPanel::kConfirmWindowMs));
    CHECK(enter(panel, PrintState::Printing, 1000 + ControlPanel::kConfirmWindowMs) == Result::Cancel);
    CHECK(!panel.cancel_armed(1000 + ControlPanel::kConfirmWindowMs));

    // Too late: the second press only arms again.
    CHECK(enter(panel, PrintState::Paused, 10000) == Result::ArmCancel);
    CHECK(enter(panel, PrintState::Paused, 10001 + ControlPanel::kConfirmWindowMs) == Result::ArmCancel);
}

TEST_CASE("control: moving or another key abandons a pending cancel") {
    ControlPanel panel;
    focus(panel, ControlButton::Cancel);
    CHECK(enter(panel, PrintState::Printing, 0) == Result::ArmCancel);
    CHECK(panel.handle(PanelInput::Escape, false, PrintState::Printing, 10) == Result::Ignored);
    CHECK(enter(panel, PrintState::Printing, 20) == Result::ArmCancel);

    panel.handle(PanelInput::Right, false, PrintState::Printing, 30);
    panel.handle(PanelInput::Left, false, PrintState::Printing, 40);
    CHECK(panel.focused() == ControlButton::Cancel);
    CHECK(enter(panel, PrintState::Printing, 50) == Result::ArmCancel);
}

TEST_CASE("control: nothing to cancel without a print") {
    ControlPanel panel;
    focus(panel, ControlButton::Cancel);
    CHECK(enter(panel, PrintState::Standby, 0) == Result::NoPrint);
    CHECK(!panel.cancel_armed(0));
}

TEST_CASE("control: the emergency stop needs a long press, with or without a print") {
    ControlPanel panel;
    focus(panel, ControlButton::EmergencyStop);
    CHECK(enter(panel, PrintState::Standby, 0, false) == Result::HoldToStop);
    CHECK(enter(panel, PrintState::Standby, 700, true) == Result::EmergencyStop);
    CHECK(enter(panel, PrintState::Printing, 0, true) == Result::EmergencyStop);
}

TEST_CASE("control: holding enter elsewhere does not repeat the action") {
    ControlPanel panel;
    CHECK(enter(panel, PrintState::Printing, 0, false) == Result::Pause);
    CHECK(enter(panel, PrintState::Printing, 700, true) == Result::Handled);
}

TEST_CASE("control: left and right stop at the ends; other keys are shortcuts") {
    ControlPanel panel;
    panel.handle(PanelInput::Left, false, PrintState::Printing, 0);
    CHECK(panel.focused() == ControlButton::PauseResume);
    for (int i = 0; i < 5; ++i) {
        panel.handle(PanelInput::Right, false, PrintState::Printing, 0);
    }
    CHECK(panel.focused() == ControlButton::EmergencyStop);
    CHECK(panel.handle(PanelInput::Up, false, PrintState::Printing, 0) == Result::Ignored);
    CHECK(panel.handle(PanelInput::Digit, false, PrintState::Printing, 0) == Result::Ignored);
    CHECK(panel.handle(PanelInput::Escape, false, PrintState::Printing, 0) == Result::Ignored);
}

/*
 * SPDX-License-Identifier: MIT
 */

#include "gcode.h"
#include "move_panel.h"
#include "test_framework.h"

#include <string>

namespace {

using app::MoveAxis;
using app::MoveBlock;
using app::MovePanel;
using app::MoveRow;
using app::PanelInput;
using Result = MovePanel::Result;

model::PrinterState homed_idle_printer() {
    model::PrinterState printer;
    printer.print_state = model::PrintState::Standby;
    printer.homed_axes = "xyz";
    return printer;
}

void focus(MovePanel& panel, MoveRow row) {
    for (int i = 0; i < 10; ++i) {
        panel.handle(PanelInput::Up, nullptr);
    }
    for (int i = 0; i < static_cast<int>(row); ++i) {
        panel.handle(PanelInput::Down, nullptr);
    }
}

} // namespace

TEST_CASE("gcode: homing all axes or one") {
    CHECK_EQ(app::gcode::home_axes(""), std::string("G28"));
    CHECK_EQ(app::gcode::home_axes("X"), std::string("G28 X"));
    CHECK_EQ(app::gcode::home_axes("XY"), std::string("G28 X Y"));
    CHECK_EQ(app::gcode::motors_off(), std::string("M84"));
}

TEST_CASE("gcode: jogs keep the printer's positioning mode") {
    CHECK(app::gcode::jog_axis('X', 10.0, 3000, "kdz_jog_7") ==
          "SAVE_GCODE_STATE NAME=kdz_jog_7\n"
          "G91\n"
          "G1 X10 F3000\n"
          "RESTORE_GCODE_STATE NAME=kdz_jog_7");
    CHECK(app::gcode::jog_axis('Z', -0.1, 600, app::gcode::kJogState).find("G1 Z-0.1 F600\n") != std::string::npos);
    CHECK(app::gcode::jog_axis('Y', -50.0, 3000, app::gcode::kJogState).find("G1 Y-50 F3000\n") != std::string::npos);
}

TEST_CASE("gcode: a failed jog or filament move restores its state on its own") {
    CHECK_EQ(app::gcode::restore_gcode_state(app::gcode::kJogState), std::string("RESTORE_GCODE_STATE NAME=kdz_jog"));
    CHECK_EQ(app::gcode::restore_gcode_state(app::gcode::kExtruderState),
             std::string("RESTORE_GCODE_STATE NAME=kdz_extruder"));
    // The scripts end with the same restore when nothing fails.
    const auto jog = app::gcode::jog_axis('X', 10.0, 3000, "kdz_jog_7");
    CHECK(jog.find("SAVE_GCODE_STATE NAME=kdz_jog_7\n") == 0);
    CHECK(jog.substr(jog.rfind('\n') + 1) == app::gcode::restore_gcode_state("kdz_jog_7"));
    const auto extrude = app::gcode::move_extruder(10, 5, "kdz_extruder_8");
    CHECK(extrude.substr(extrude.rfind('\n') + 1) == app::gcode::restore_gcode_state("kdz_extruder_8"));
}

TEST_CASE("gcode: distances drop trailing zeros") {
    CHECK_EQ(app::gcode::format_distance(0.1), std::string("0.1"));
    CHECK_EQ(app::gcode::format_distance(1.0), std::string("1"));
    CHECK_EQ(app::gcode::format_distance(10.0), std::string("10"));
    CHECK_EQ(app::gcode::format_distance(-50.0), std::string("-50"));
    CHECK_EQ(app::gcode::format_distance(1.25), std::string("1.25"));
    CHECK_EQ(app::gcode::format_distance(-0.0001), std::string("0"));
}

TEST_CASE("move: defaults to the X axis and a 10 mm step") {
    MovePanel panel;
    CHECK(panel.focused() == MoveRow::Axis);
    CHECK(panel.axis() == MoveAxis::X);
    CHECK_EQ(panel.step(), 10.0);
}

TEST_CASE("move: up and down walk the rows and stop at the ends") {
    MovePanel panel;
    CHECK(panel.handle(PanelInput::Up, nullptr) == Result::Handled);
    CHECK(panel.focused() == MoveRow::Axis);
    const MoveRow order[] = {MoveRow::Step,     MoveRow::JogMinus, MoveRow::JogPlus,  MoveRow::HomeAxis,
                             MoveRow::HomeAll,  MoveRow::MotorsOff, MoveRow::MotorsOff};
    for (const auto row : order) {
        CHECK(panel.handle(PanelInput::Down, nullptr) == Result::Handled);
        CHECK(panel.focused() == row);
    }
    panel.handle(PanelInput::Up, nullptr);
    CHECK(panel.focused() == MoveRow::HomeAll);
}

TEST_CASE("move: left and right cycle the axis and the step on their rows") {
    MovePanel panel;
    panel.handle(PanelInput::Right, nullptr);
    CHECK(panel.axis() == MoveAxis::Y);
    panel.handle(PanelInput::Right, nullptr);
    CHECK(panel.axis() == MoveAxis::Z);
    panel.handle(PanelInput::Right, nullptr);
    CHECK(panel.axis() == MoveAxis::X);
    panel.handle(PanelInput::Left, nullptr);
    CHECK(panel.axis() == MoveAxis::Z);

    panel.handle(PanelInput::Down, nullptr);
    panel.handle(PanelInput::Right, nullptr);
    CHECK_EQ(panel.step(), 50.0);
    panel.handle(PanelInput::Right, nullptr);
    CHECK_EQ(panel.step(), 0.1);
    panel.handle(PanelInput::Left, nullptr);
    CHECK_EQ(panel.step(), 50.0);
    // The axis did not change while the step row had focus.
    CHECK(panel.axis() == MoveAxis::Z);

    // Left/Right do nothing on the action rows.
    focus(panel, MoveRow::JogPlus);
    CHECK(panel.handle(PanelInput::Right, nullptr) == Result::Handled);
    CHECK(panel.axis() == MoveAxis::Z);
    CHECK_EQ(panel.step(), 50.0);
}

TEST_CASE("move: jog rows request a signed move with the feed of the axis") {
    const auto printer = homed_idle_printer();
    MovePanel panel;

    focus(panel, MoveRow::JogPlus);
    CHECK(panel.handle(PanelInput::Enter, &printer) == Result::Jog);
    auto request = panel.request();
    CHECK(request.axis == MoveAxis::X);
    CHECK_EQ(request.distance_mm, 10.0);
    CHECK_EQ(request.feed_mm_min, 3000);
    CHECK_EQ(app::describe(request), std::string("Moved X +10 mm"));

    // Z, 0.1 mm, downwards.
    focus(panel, MoveRow::Axis);
    panel.handle(PanelInput::Left, nullptr);
    panel.handle(PanelInput::Down, nullptr);
    panel.handle(PanelInput::Left, nullptr);
    panel.handle(PanelInput::Left, nullptr);
    focus(panel, MoveRow::JogMinus);
    CHECK(panel.handle(PanelInput::Enter, &printer) == Result::Jog);
    request = panel.request();
    CHECK(request.axis == MoveAxis::Z);
    CHECK_EQ(request.distance_mm, -0.1);
    CHECK_EQ(request.feed_mm_min, 600);
    CHECK_EQ(app::describe(request), std::string("Moved Z -0.1 mm"));

    CHECK_EQ(app::jog_feed(MoveAxis::Y), 3000);
}

TEST_CASE("move: home and motors-off rows; other keys are shortcuts") {
    const auto printer = homed_idle_printer();
    MovePanel panel;
    CHECK(panel.handle(PanelInput::Enter, &printer) == Result::Handled);
    focus(panel, MoveRow::HomeAxis);
    CHECK(panel.handle(PanelInput::Enter, &printer) == Result::HomeAxis);
    focus(panel, MoveRow::HomeAll);
    CHECK(panel.handle(PanelInput::Enter, &printer) == Result::HomeAll);
    focus(panel, MoveRow::MotorsOff);
    CHECK(panel.handle(PanelInput::Enter, &printer) == Result::MotorsOff);

    CHECK(panel.handle(PanelInput::Escape, &printer) == Result::Ignored);
    CHECK(panel.handle(PanelInput::Digit, &printer) == Result::Ignored);
    CHECK(panel.handle(PanelInput::Backspace, &printer) == Result::Ignored);
}

TEST_CASE("move: everything is refused while printing") {
    auto printer = homed_idle_printer();
    printer.print_state = model::PrintState::Printing;
    MovePanel panel;
    for (const auto row : {MoveRow::JogMinus, MoveRow::JogPlus, MoveRow::HomeAxis, MoveRow::HomeAll,
                           MoveRow::MotorsOff}) {
        focus(panel, row);
        CHECK(panel.handle(PanelInput::Enter, &printer) == Result::Blocked);
        CHECK(panel.block() == MoveBlock::Printing);
        CHECK_EQ(std::string(app::move_block_reason(panel.block())), std::string("Not available while printing"));
    }
}

TEST_CASE("move: while paused jogging works but homing and motors off do not") {
    auto printer = homed_idle_printer();
    printer.print_state = model::PrintState::Paused;
    MovePanel panel;
    focus(panel, MoveRow::JogMinus);
    CHECK(panel.handle(PanelInput::Enter, &printer) == Result::Jog);
    for (const auto row : {MoveRow::HomeAxis, MoveRow::HomeAll, MoveRow::MotorsOff}) {
        focus(panel, row);
        CHECK(panel.handle(PanelInput::Enter, &printer) == Result::Blocked);
        CHECK(panel.block() == MoveBlock::Paused);
    }
}

TEST_CASE("move: an unhomed axis must be homed before it jogs") {
    auto printer = homed_idle_printer();
    printer.homed_axes = "xy";
    MovePanel panel;
    panel.handle(PanelInput::Left, nullptr); // Z
    CHECK(panel.axis() == MoveAxis::Z);
    focus(panel, MoveRow::JogPlus);
    CHECK(panel.focused_block(printer) == MoveBlock::NotHomed);
    CHECK(panel.handle(PanelInput::Enter, &printer) == Result::Blocked);
    CHECK(panel.block() == MoveBlock::NotHomed);
    CHECK_EQ(std::string(app::move_block_reason(panel.block())), std::string("Home the axis first"));

    // Homing itself is allowed.
    focus(panel, MoveRow::HomeAxis);
    CHECK(panel.focused_block(printer) == MoveBlock::None);
    CHECK(panel.handle(PanelInput::Enter, &printer) == Result::HomeAxis);
    CHECK(panel.axis() == MoveAxis::Z);

    // X is homed, in either case.
    CHECK(app::axis_homed("XY", MoveAxis::X));
    CHECK(!app::axis_homed("", MoveAxis::X));
    CHECK(app::move_block_reason(MoveBlock::None) == nullptr);
}

TEST_CASE("move: without a ready printer the request passes on to the commands") {
    MovePanel panel;
    focus(panel, MoveRow::JogPlus);
    CHECK(panel.handle(PanelInput::Enter, nullptr) == Result::Jog);
    focus(panel, MoveRow::HomeAll);
    CHECK(panel.handle(PanelInput::Enter, nullptr) == Result::HomeAll);
}

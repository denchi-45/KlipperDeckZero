/*
 * SPDX-License-Identifier: MIT
 */

#include "extruder_panel.h"
#include "gcode.h"
#include "test_framework.h"

#include <string>

namespace {

using app::ExtruderDirection;
using app::ExtruderPanel;
using app::ExtruderRow;
using app::PanelInput;
using Result = ExtruderPanel::Result;

model::PrinterState hot_idle_printer() {
    model::PrinterState printer;
    printer.print_state = model::PrintState::Standby;
    printer.extruder.can_extrude = true;
    return printer;
}

} // namespace

TEST_CASE("gcode: filament moves keep the printer's extrusion mode") {
    CHECK(app::gcode::move_extruder(10, 5) ==
          "SAVE_GCODE_STATE NAME=kdz_extruder\n"
          "M83\n"
          "G1 E10 F300\n"
          "RESTORE_GCODE_STATE NAME=kdz_extruder");
    CHECK(app::gcode::move_extruder(-25, 10).find("G1 E-25 F600\n") != std::string::npos);
}

TEST_CASE("extruder: defaults to extruding 10 mm at 5 mm/s") {
    ExtruderPanel panel;
    CHECK(panel.focused() == ExtruderRow::Length);
    CHECK(panel.direction() == ExtruderDirection::Extrude);
    const auto request = panel.request();
    CHECK(request.length_mm == 10);
    CHECK(request.speed_mm_s == 5);
    CHECK(app::describe(request) == "Extruded 10 mm");
}

TEST_CASE("extruder: left and right change the focused row and stop at the ends") {
    ExtruderPanel panel;
    for (int i = 0; i < 10; ++i) {
        panel.handle(PanelInput::Right);
    }
    CHECK(panel.request().length_mm == ExtruderPanel::lengths().back());
    for (int i = 0; i < 10; ++i) {
        panel.handle(PanelInput::Left);
    }
    CHECK(panel.request().length_mm == ExtruderPanel::lengths().front());

    panel.handle(PanelInput::Down);
    CHECK(panel.focused() == ExtruderRow::Speed);
    panel.handle(PanelInput::Right);
    CHECK(panel.request().speed_mm_s == ExtruderPanel::speeds().back());
    // The length did not change while the speed row had focus.
    CHECK(panel.request().length_mm == ExtruderPanel::lengths().front());
}

TEST_CASE("extruder: the direction row turns the move into a retract") {
    ExtruderPanel panel;
    panel.handle(PanelInput::Down);
    panel.handle(PanelInput::Down);
    panel.handle(PanelInput::Down);
    CHECK(panel.focused() == ExtruderRow::Direction);
    panel.handle(PanelInput::Left);
    CHECK(panel.direction() == ExtruderDirection::Retract);
    CHECK(panel.request().length_mm == -10);
    CHECK(app::describe(panel.request()) == "Retracted 10 mm");
    panel.handle(PanelInput::Right);
    CHECK(panel.request().length_mm == 10);
}

TEST_CASE("extruder: enter runs the move from any row; other keys are shortcuts") {
    ExtruderPanel panel;
    CHECK(panel.handle(PanelInput::Enter) == Result::Apply);
    panel.handle(PanelInput::Down);
    CHECK(panel.handle(PanelInput::Enter) == Result::Apply);
    CHECK(panel.handle(PanelInput::Escape) == Result::Ignored);
    CHECK(panel.handle(PanelInput::Digit) == Result::Ignored);
    CHECK(panel.handle(PanelInput::Backspace) == Result::Ignored);
}

TEST_CASE("extruder: moves are refused while printing or when the nozzle is cold") {
    auto printer = hot_idle_printer();
    CHECK(app::extrude_block_reason(printer) == nullptr);

    printer.print_state = model::PrintState::Paused;
    CHECK(app::extrude_block_reason(printer) == nullptr);

    printer.print_state = model::PrintState::Printing;
    CHECK(app::extrude_block_reason(printer) != nullptr);

    auto cold = hot_idle_printer();
    cold.extruder.can_extrude = false;
    CHECK(app::extrude_block_reason(cold) != nullptr);
}

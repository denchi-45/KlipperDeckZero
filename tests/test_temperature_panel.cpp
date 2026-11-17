/*
 * SPDX-License-Identifier: MIT
 */

#include "gcode.h"
#include "temperature_panel.h"
#include "test_framework.h"

#include <string>

namespace {

using app::HeaterTargets;
using app::PanelInput;
using app::TemperaturePanel;
using app::TemperatureRow;
using Result = TemperaturePanel::Result;

Result press(TemperaturePanel& panel, PanelInput input, HeaterTargets current = {}) {
    return panel.handle(input, 0, current);
}

void type(TemperaturePanel& panel, const std::string& digits) {
    for (const char digit : digits) {
        panel.handle(PanelInput::Digit, digit, {});
    }
}

} // namespace

TEST_CASE("gcode: heater targets use SET_HEATER_TEMPERATURE") {
    CHECK(app::gcode::set_heater_temperature(app::gcode::Heater::Nozzle, 215) ==
          "SET_HEATER_TEMPERATURE HEATER=extruder TARGET=215");
    CHECK(app::gcode::set_heater_temperature(app::gcode::Heater::Bed, 0) ==
          "SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=0");
    CHECK(app::gcode::set_heater_temperature(app::gcode::Heater::Bed, -5) ==
          "SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=0");
}

TEST_CASE("temperature: request script and description") {
    app::TemperatureRequest both;
    both.nozzle = 200;
    both.bed = 60;
    CHECK(app::temperature_script(both) ==
          "SET_HEATER_TEMPERATURE HEATER=extruder TARGET=200\n"
          "SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=60");
    CHECK(app::describe(both) == "Nozzle 200, Bed 60");

    app::TemperatureRequest bed_off;
    bed_off.bed = 0;
    CHECK(app::temperature_script(bed_off) == "SET_HEATER_TEMPERATURE HEATER=heater_bed TARGET=0");
    CHECK(app::describe(bed_off) == "Bed off");
    CHECK(app::temperature_script({}).empty());
}

TEST_CASE("temperature: up and down move between rows and stop at the ends") {
    TemperaturePanel panel;
    CHECK(panel.focused() == TemperatureRow::Nozzle);
    CHECK(press(panel, PanelInput::Up) == Result::Handled);
    CHECK(panel.focused() == TemperatureRow::Nozzle);
    press(panel, PanelInput::Down);
    CHECK(panel.focused() == TemperatureRow::Bed);
    press(panel, PanelInput::Down);
    press(panel, PanelInput::Down);
    CHECK(panel.focused() == TemperatureRow::Presets);
}

TEST_CASE("temperature: arrows step from the current target on the 5 degree grid") {
    TemperaturePanel panel;
    CHECK(press(panel, PanelInput::Right, {212, 0}) == Result::Handled);
    CHECK(panel.editing());
    CHECK(panel.draft_text() == "215");
    press(panel, PanelInput::Right);
    CHECK(panel.draft_text() == "220");
    press(panel, PanelInput::Left);
    press(panel, PanelInput::Left);
    CHECK(panel.draft_text() == "210");

    CHECK(press(panel, PanelInput::Enter) == Result::Apply);
    CHECK(!panel.editing());
    CHECK(panel.request().nozzle.has_value());
    CHECK(*panel.request().nozzle == 210);
    CHECK(!panel.request().bed.has_value());
}

TEST_CASE("temperature: stepping stays between 0 and the heater maximum") {
    TemperaturePanel panel;
    press(panel, PanelInput::Down);
    press(panel, PanelInput::Left, {0, 0});
    CHECK(panel.draft_text() == "0");
    press(panel, PanelInput::Escape);
    press(panel, PanelInput::Right, {0, TemperaturePanel::kMaxBed});
    CHECK(panel.draft_text() == std::to_string(TemperaturePanel::kMaxBed));
}

TEST_CASE("temperature: enter starts typing and the first digit replaces the target") {
    TemperaturePanel panel;
    CHECK(press(panel, PanelInput::Enter, {200, 60}) == Result::Handled);
    CHECK(panel.draft_text() == "200");
    type(panel, "2155");
    CHECK(panel.draft_text() == "215");
    panel.handle(PanelInput::Backspace, 0, {});
    CHECK(panel.draft_text() == "21");
    CHECK(press(panel, PanelInput::Enter) == Result::Apply);
    CHECK(*panel.request().nozzle == 21);
}

TEST_CASE("temperature: an empty draft turns the heater off") {
    TemperaturePanel panel;
    press(panel, PanelInput::Down);
    press(panel, PanelInput::Enter, {0, 60});
    press(panel, PanelInput::Backspace);
    CHECK(panel.draft_text().empty());
    CHECK(press(panel, PanelInput::Enter) == Result::Apply);
    CHECK(*panel.request().bed == 0);
    CHECK(!panel.request().nozzle.has_value());
}

TEST_CASE("temperature: a target above the maximum is refused") {
    TemperaturePanel panel;
    press(panel, PanelInput::Down);
    press(panel, PanelInput::Enter, {0, 0});
    type(panel, "200");
    CHECK(press(panel, PanelInput::Enter) == Result::Handled);
    CHECK(panel.editing());
    CHECK(!panel.error().empty());
    type(panel, "");
    panel.handle(PanelInput::Backspace, 0, {});
    CHECK(panel.error().empty());
}

TEST_CASE("temperature: escape cancels an edit, and is a shortcut otherwise") {
    TemperaturePanel panel;
    press(panel, PanelInput::Right, {200, 0});
    CHECK(press(panel, PanelInput::Escape) == Result::Handled);
    CHECK(!panel.editing());
    CHECK(press(panel, PanelInput::Escape) == Result::Ignored);
    // Digits switch pages unless a target is being typed.
    CHECK(panel.handle(PanelInput::Digit, '5', {}) == Result::Ignored);
}

TEST_CASE("temperature: up and down leave an edit without applying it") {
    TemperaturePanel panel;
    press(panel, PanelInput::Right, {200, 0});
    CHECK(press(panel, PanelInput::Down) == Result::Handled);
    CHECK(!panel.editing());
    CHECK(panel.focused() == TemperatureRow::Bed);
}

TEST_CASE("temperature: presets apply both heaters, or only the nozzle without a bed") {
    TemperaturePanel panel;
    press(panel, PanelInput::Down);
    press(panel, PanelInput::Down);
    CHECK(panel.preset() == 0);
    press(panel, PanelInput::Right);
    CHECK(panel.preset() == 1);
    CHECK(press(panel, PanelInput::Enter) == Result::Apply);
    const auto& petg = app::temperature_presets()[1];
    CHECK(*panel.request().nozzle == petg.nozzle);
    CHECK(*panel.request().bed == petg.bed);

    // Left from the first preset wraps to the last one.
    press(panel, PanelInput::Left);
    press(panel, PanelInput::Left);
    CHECK(panel.preset() == app::temperature_presets().size() - 1);

    panel.set_has_bed(false);
    CHECK(panel.focused() == TemperatureRow::Presets);
    CHECK(press(panel, PanelInput::Enter) == Result::Apply);
    CHECK(panel.request().nozzle.has_value());
    CHECK(!panel.request().bed.has_value());
}

TEST_CASE("temperature: losing the bed while editing it ends the edit") {
    TemperaturePanel panel;
    press(panel, PanelInput::Down);
    press(panel, PanelInput::Right, {0, 60});
    CHECK(panel.editing());
    panel.set_has_bed(false);
    CHECK(!panel.editing());
    CHECK(panel.focused() == TemperatureRow::Nozzle);
    CHECK(panel.rows().size() == 2);
}

TEST_CASE("temperature: the sensor line lists other sensors, then this device") {
    model::SensorState mcu;
    mcu.name = "Micro4";
    mcu.temperature = 52.4;
    mcu.known = true;
    model::SensorState chamber;
    chamber.name = "chamber";

    CHECK(app::sensor_line({mcu, chamber}, 59.6) == "Micro4 52   chamber --   CardputerZero 60");
    // Without a device reading (e.g. the simulator) only the printer's sensors are shown.
    CHECK(app::sensor_line({mcu}, -1.0) == "Micro4 52");
    CHECK(app::sensor_line({}, 41.0) == "CardputerZero 41");
    CHECK(app::sensor_line({}, -1.0).empty());
}

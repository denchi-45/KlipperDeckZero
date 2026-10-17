/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>

// G-code scripts sent through printer.gcode.script. Free of LVGL so it can be unit tested.
namespace app::gcode {

enum class Heater {
    Nozzle, // Klipper `extruder`
    Bed,    // Klipper `heater_bed`
};

// Klipper object name of the heater ("extruder" / "heater_bed").
const char* heater_object(Heater heater);

// "SET_HEATER_TEMPERATURE HEATER=extruder TARGET=215"; a target of 0 turns the heater off.
std::string set_heater_temperature(Heater heater, int celsius);

} // namespace app::gcode

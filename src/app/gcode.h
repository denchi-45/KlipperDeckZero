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

// Names of the SAVE_GCODE_STATE slots used by move_extruder() and jog_axis().
extern const char* const kExtruderState; // "kdz_extruder"
extern const char* const kJogState;      // "kdz_jog"

// "RESTORE_GCODE_STATE NAME=kdz_jog". Sent on its own after a guarded move failed: Klipper
// stops a script at the first error, so the RESTORE at the end of the move never ran.
std::string restore_gcode_state(const std::string& name);

// Moves only the filament: length_mm > 0 extrudes, < 0 retracts, at speed_mm_s.
// Uses relative extrusion inside SAVE/RESTORE_GCODE_STATE so the printer's own
// extrusion mode is left as it was.
std::string move_extruder(int length_mm, int speed_mm_s);

// "G28" when axes is empty (all axes), else "G28 X" / "G28 X Y" for the given axis letters.
std::string home_axes(const std::string& axes);

// Moves one axis (X, Y or Z) by distance_mm (negative moves towards the minimum) at feed_mm_min.
// Uses relative positioning inside SAVE/RESTORE_GCODE_STATE, like move_extruder().
std::string jog_axis(char axis, double distance_mm, int feed_mm_min);

// "M84": disables all stepper motors, so the axes have to be homed again.
std::string motors_off();

// "0.1", "10", "-50": a distance without trailing zeros, rounded to 0.001 mm.
std::string format_distance(double mm);

} // namespace app::gcode

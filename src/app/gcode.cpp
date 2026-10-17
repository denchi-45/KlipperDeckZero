/*
 * SPDX-License-Identifier: MIT
 */

#include "gcode.h"

namespace app::gcode {

const char* heater_object(Heater heater) {
    return heater == Heater::Bed ? "heater_bed" : "extruder";
}

std::string set_heater_temperature(Heater heater, int celsius) {
    if (celsius < 0) {
        celsius = 0;
    }
    return std::string("SET_HEATER_TEMPERATURE HEATER=") + heater_object(heater) +
           " TARGET=" + std::to_string(celsius);
}

std::string move_extruder(int length_mm, int speed_mm_s) {
    const int feed_mm_min = (speed_mm_s > 0 ? speed_mm_s : 1) * 60;
    return "SAVE_GCODE_STATE NAME=kdz_extruder\n"
           "M83\n"
           "G1 E" + std::to_string(length_mm) + " F" + std::to_string(feed_mm_min) + "\n"
           "RESTORE_GCODE_STATE NAME=kdz_extruder";
}

} // namespace app::gcode

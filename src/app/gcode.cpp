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

} // namespace app::gcode

/*
 * SPDX-License-Identifier: MIT
 */

#include "gcode.h"

#include <cmath>
#include <cstdio>

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

std::string home_axes(const std::string& axes) {
    std::string script = "G28";
    for (const char axis : axes) {
        script += ' ';
        script += axis;
    }
    return script;
}

std::string jog_axis(char axis, double distance_mm, int feed_mm_min) {
    return "SAVE_GCODE_STATE NAME=kdz_jog\n"
           "G91\n"
           "G1 " + std::string(1, axis) + format_distance(distance_mm) + " F" +
           std::to_string(feed_mm_min > 0 ? feed_mm_min : 1) + "\n"
           "RESTORE_GCODE_STATE NAME=kdz_jog";
}

std::string motors_off() {
    return "M84";
}

std::string format_distance(double mm) {
    // Rounded first so that -0.0001 does not print as "-0".
    const double rounded = std::round(mm * 1000.0) / 1000.0;
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.3f", rounded == 0.0 ? 0.0 : rounded);
    std::string text = buffer;
    text.erase(text.find_last_not_of('0') + 1);
    if (!text.empty() && text.back() == '.') {
        text.pop_back();
    }
    return text;
}

} // namespace app::gcode

/*
 * SPDX-License-Identifier: MIT
 */

#include "gcode_files.h"

#include "printer_format.h"

#include <cstdio>

namespace app {

std::string format_size(std::uint64_t bytes) {
    char buffer[24];
    if (bytes < 1024) {
        std::snprintf(buffer, sizeof(buffer), "%u B", static_cast<unsigned>(bytes));
    }
    else if (bytes < 1024 * 1024) {
        std::snprintf(buffer, sizeof(buffer), "%.0f KB", static_cast<double>(bytes) / 1024.0);
    }
    else {
        std::snprintf(buffer, sizeof(buffer), "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    }
    return buffer;
}

std::string format_filament(double mm) {
    if (mm < 0.0) {
        return "--";
    }
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%.2f m", mm / 1000.0);
    return buffer;
}

std::string format_duration(double seconds) {
    return viewmodel::format::duration(seconds);
}

std::string format_millimeters(double mm) {
    if (mm < 0.0) {
        return "--";
    }
    char buffer[24];
    std::snprintf(buffer, sizeof(buffer), "%.2f mm", mm);
    return buffer;
}

} // namespace app

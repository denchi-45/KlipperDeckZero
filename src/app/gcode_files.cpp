/*
 * SPDX-License-Identifier: MIT
 */

#include "gcode_files.h"

#include "printer_format.h"

#include <algorithm>
#include <cstdio>

namespace app {

const GcodeThumbnail* pick_thumbnail(const std::vector<GcodeThumbnail>& thumbnails, int box_size) {
    const GcodeThumbnail* fitting = nullptr;
    const GcodeThumbnail* largest = nullptr;
    for (const auto& thumbnail : thumbnails) {
        if (thumbnail.width <= 0 || thumbnail.height <= 0 || thumbnail.relative_path.empty()) {
            continue;
        }
        const int side = std::min(thumbnail.width, thumbnail.height);
        if (side >= box_size && (!fitting || side < std::min(fitting->width, fitting->height))) {
            fitting = &thumbnail;
        }
        if (!largest || side > std::min(largest->width, largest->height)) {
            largest = &thumbnail;
        }
    }
    return fitting ? fitting : largest;
}

std::string thumbnail_path(const std::string& gcode_path, const std::string& relative_path) {
    const auto slash = gcode_path.rfind('/');
    return slash == std::string::npos ? relative_path : gcode_path.substr(0, slash + 1) + relative_path;
}

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

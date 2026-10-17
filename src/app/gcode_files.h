/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>

namespace app {

// One entry of `server.files.list` for the "gcodes" root.
struct GcodeFile {
    std::string path; // relative to the gcodes root, e.g. "parts/bracket.gcode"
    double modified{0.0}; // Unix time
    std::uint64_t size{0};
};

// Fields of `server.files.metadata` shown before printing; negative / empty when unknown.
struct GcodeMetadata {
    double estimated_time{-1.0}; // seconds
    double filament_total{-1.0}; // mm
    double object_height{-1.0};  // mm
    double layer_height{-1.0};   // mm
    double nozzle_temperature{-1.0}; // first layer
    double bed_temperature{-1.0};    // first layer
    std::string slicer;
};

// "512 B", "850 KB", "12.3 MB"
std::string format_size(std::uint64_t bytes);
// "4.52 m", or "--" when unknown
std::string format_filament(double mm);
// "0.20 mm", or "--" when unknown
std::string format_millimeters(double mm);
// "1:02:03" / "02:03", or "--:--" when unknown; same as the status page.
std::string format_duration(double seconds);

} // namespace app

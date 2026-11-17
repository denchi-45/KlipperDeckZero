/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace app {

// One entry of `server.files.list` for the "gcodes" root.
struct GcodeFile {
    std::string path; // relative to the gcodes root, e.g. "parts/bracket.gcode"
    double modified{0.0}; // Unix time
    std::uint64_t size{0};
};

// One PNG preview the slicer embedded in the file.
struct GcodeThumbnail {
    int width{0};
    int height{0};
    std::string relative_path; // relative to the G-code file's directory, e.g. ".thumbs/a-300x300.png"
};

// Fields of `server.files.metadata` shown before printing; negative when unknown.
struct GcodeMetadata {
    double estimated_time{-1.0}; // seconds
    double filament_total{-1.0}; // mm
    double object_height{-1.0};  // mm
    double layer_height{-1.0};   // mm
    double nozzle_temperature{-1.0}; // first layer
    double bed_temperature{-1.0};    // first layer
    std::vector<GcodeThumbnail> thumbnails;
};

// The smallest thumbnail that fills a box of box_size pixels, or else the largest one.
// nullptr when there is none.
const GcodeThumbnail* pick_thumbnail(const std::vector<GcodeThumbnail>& thumbnails, int box_size);
// Path of a thumbnail relative to the gcodes root: "parts/a.gcode" + ".thumbs/a.png"
// gives "parts/.thumbs/a.png".
std::string thumbnail_path(const std::string& gcode_path, const std::string& relative_path);
// True when data is a PNG file the libpng decoder can open: the signature and IHDR width and
// height are there, libpng reads the header up to the image data, and neither side exceeds
// 1024 pixels. The decoder checks only the signature before reading the size (a shorter file
// would be read past its end) and finds a header libpng refuses only when drawing.
bool has_png_header(const std::string& data);

// "512 B", "850 KB", "12.3 MB"
std::string format_size(std::uint64_t bytes);
// "4.52 m", or "--" when unknown
std::string format_filament(double mm);
// "0.20 mm", or "--" when unknown
std::string format_millimeters(double mm);
// "1:02:03" / "02:03", or "--:--" when unknown; same as the status page.
std::string format_duration(double seconds);

} // namespace app

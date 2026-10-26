/*
 * SPDX-License-Identifier: MIT
 */

#include "gcode_files.h"

#include "printer_format.h"

#include <png.h>

#include <algorithm>
#include <cstdio>

namespace app {
namespace {

constexpr char kPngSignature[] = "\x89PNG\r\n\x1a\n";
// Signature, IHDR length and type, width and height.
constexpr std::size_t kPngHeaderBytes = 24;
// Slicers write 300x300 and smaller. The decoder reads only 16 bits of each side for the image
// header, and a bigger image could not be decoded into the LVGL heap anyway.
constexpr png_uint_32 kMaxPngSide = 1024;

// The path comes from file metadata and is read from disk over a Unix socket connection,
// so it must stay below the G-code file's directory.
bool is_safe_relative_path(const std::string& path) {
    if (path.empty() || path.front() == '/') {
        return false;
    }
    std::size_t start = 0;
    while (start <= path.size()) {
        const auto slash = std::min(path.find('/', start), path.size());
        if (path.compare(start, slash - start, "..") == 0) {
            return false;
        }
        start = slash + 1;
    }
    return true;
}

} // namespace

const GcodeThumbnail* pick_thumbnail(const std::vector<GcodeThumbnail>& thumbnails, int box_size) {
    const GcodeThumbnail* fitting = nullptr;
    const GcodeThumbnail* largest = nullptr;
    for (const auto& thumbnail : thumbnails) {
        if (thumbnail.width <= 0 || thumbnail.height <= 0 || !is_safe_relative_path(thumbnail.relative_path)) {
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

bool has_png_header(const std::string& data) {
    if (data.size() < kPngHeaderBytes || data.compare(0, sizeof(kPngSignature) - 1, kPngSignature) != 0) {
        return false;
    }
    // The same first step as the decoder's open: it rejects e.g. a broken IHDR, which the
    // signature check alone lets through to an empty image.
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, data.data(), data.size())) {
        return false;
    }
    const bool fits = image.width <= kMaxPngSide && image.height <= kMaxPngSide;
    png_image_free(&image);
    return fits;
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

/*
 * SPDX-License-Identifier: MIT
 */

#include "file_list_parser.h"

#include <algorithm>

namespace app {
namespace {

double number_or(const moonraker::Json& object, const char* key, double fallback) {
    const auto it = object.find(key);
    return it != object.end() && it->is_number() ? it->get<double>() : fallback;
}

std::string string_or(const moonraker::Json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

} // namespace

std::vector<GcodeFile> parse_file_list(const moonraker::Json& result) {
    std::vector<GcodeFile> files;
    if (!result.is_array()) {
        return files;
    }
    for (const auto& entry : result) {
        if (!entry.is_object()) {
            continue;
        }
        GcodeFile file;
        file.path = string_or(entry, "path");
        if (file.path.empty()) {
            continue;
        }
        file.modified = number_or(entry, "modified", 0.0);
        const double size = number_or(entry, "size", 0.0);
        file.size = size > 0.0 ? static_cast<std::uint64_t>(size) : 0;
        files.push_back(std::move(file));
    }
    std::stable_sort(files.begin(), files.end(), [](const GcodeFile& a, const GcodeFile& b) {
        return a.modified > b.modified;
    });
    return files;
}

GcodeMetadata parse_metadata(const moonraker::Json& result) {
    GcodeMetadata metadata;
    if (!result.is_object()) {
        return metadata;
    }
    metadata.estimated_time = number_or(result, "estimated_time", -1.0);
    metadata.filament_total = number_or(result, "filament_total", -1.0);
    metadata.object_height = number_or(result, "object_height", -1.0);
    metadata.layer_height = number_or(result, "layer_height", -1.0);
    metadata.nozzle_temperature = number_or(result, "first_layer_extr_temp", -1.0);
    metadata.bed_temperature = number_or(result, "first_layer_bed_temp", -1.0);
    if (const auto it = result.find("thumbnails"); it != result.end() && it->is_array()) {
        for (const auto& entry : *it) {
            if (!entry.is_object()) {
                continue;
            }
            GcodeThumbnail thumbnail;
            thumbnail.width = static_cast<int>(number_or(entry, "width", 0.0));
            thumbnail.height = static_cast<int>(number_or(entry, "height", 0.0));
            thumbnail.relative_path = string_or(entry, "relative_path");
            if (!thumbnail.relative_path.empty()) {
                metadata.thumbnails.push_back(std::move(thumbnail));
            }
        }
    }
    return metadata;
}

} // namespace app

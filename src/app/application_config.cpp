/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "application_config.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace app {
namespace {

std::string trim(std::string value) {
    const auto is_not_space = [](unsigned char character) {
        return !std::isspace(character);
    };

    value.erase(value.begin(), std::find_if(value.begin(), value.end(), is_not_space));
    value.erase(std::find_if(value.rbegin(), value.rend(), is_not_space).base(), value.end());
    return value;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return value;
}

bool parse_bool(const std::string& value, bool& result) {
    const auto normalized = lowercase(trim(value));
    if (normalized == "yes" || normalized == "true" || normalized == "1") {
        result = true;
        return true;
    }
    if (normalized == "no" || normalized == "false" || normalized == "0") {
        result = false;
        return true;
    }
    return false;
}

bool is_section(const std::string& line, const std::string& expected) {
    const auto normalized = trim(line);
    return normalized.size() >= 2 && normalized.front() == '[' && normalized.back() == ']' &&
           lowercase(trim(normalized.substr(1, normalized.size() - 2))) == expected;
}

bool is_key(const std::string& line, const std::string& expected) {
    const auto separator = line.find('=');
    return separator != std::string::npos &&
           lowercase(trim(line.substr(0, separator))) == expected;
}

// Replaces target with source. std::filesystem::rename cannot overwrite an existing file with
// MinGW's libstdc++, so Windows goes through MoveFileExW.
void replace_file(const std::filesystem::path& source,
                  const std::filesystem::path& target,
                  std::error_code& error) {
#ifdef _WIN32
    if (MoveFileExW(source.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        error.clear();
    } else {
        error.assign(static_cast<int>(GetLastError()), std::system_category());
    }
#else
    std::filesystem::rename(source, target, error);
#endif
}

} // namespace

bool load_application_config(const std::string& path,
                             ApplicationConfig& config,
                             std::string& error) {
    error.clear();
    std::ifstream input(path);
    if (!input) {
        error = "could not open file";
        return false;
    }

    std::string section;
    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        line = trim(line);
        if (line.empty() || line.front() == '#' || line.front() == ';') {
            continue;
        }

        if (line.front() == '[' && line.back() == ']') {
            section = lowercase(trim(line.substr(1, line.size() - 2)));
            continue;
        }

        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        const auto key = lowercase(trim(line.substr(0, separator)));
        if (section == "application" && key == "dark_mode") {
            if (!parse_bool(line.substr(separator + 1), config.dark_mode)) {
                error = "invalid dark_mode value at line " + std::to_string(line_number);
                return false;
            }
        }
    }

    return true;
}

bool save_application_config(const std::string& path,
                             const ApplicationConfig& config,
                             std::string& error) {
    error.clear();

    const std::filesystem::path config_path(path);
    const auto parent = config_path.parent_path();
    if (!parent.empty()) {
        std::error_code filesystem_error;
        std::filesystem::create_directories(parent, filesystem_error);
        if (filesystem_error) {
            error = "could not create parent directory: " + filesystem_error.message();
            return false;
        }
    }

    std::vector<std::string> lines;
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        lines.push_back(line);
    }

    std::vector<std::string> updated;
    bool in_application_section = false;
    bool found_application_section = false;
    bool wrote_dark_mode = false;
    const std::string dark_mode_line = config.dark_mode ? "dark_mode=yes" : "dark_mode=no";

    for (const auto& current_line : lines) {
        const auto normalized = trim(current_line);
        const bool section_header = normalized.size() >= 2 && normalized.front() == '[' &&
                                    normalized.back() == ']';
        if (section_header) {
            if (in_application_section && !wrote_dark_mode) {
                updated.push_back(dark_mode_line);
                wrote_dark_mode = true;
            }
            in_application_section = is_section(current_line, "application");
            found_application_section = found_application_section || in_application_section;
        }

        if (in_application_section && is_key(current_line, "dark_mode")) {
            if (!wrote_dark_mode) {
                updated.push_back(dark_mode_line);
                wrote_dark_mode = true;
            }
            continue;
        }
        updated.push_back(current_line);
    }

    if (!wrote_dark_mode) {
        if (!updated.empty() && !updated.back().empty()) {
            updated.emplace_back();
        }
        if (!found_application_section) {
            updated.emplace_back("[application]");
        }
        updated.push_back(dark_mode_line);
    }

    // Write a sibling temp file and rename it over the target so a power loss never leaves a
    // truncated config behind.
    const std::string temp_path = path + ".tmp";
    {
        std::ofstream output(temp_path, std::ios::trunc);
        if (!output) {
            error = "could not open file for writing";
            return false;
        }
        for (const auto& updated_line : updated) {
            output << updated_line << '\n';
        }
        output.flush();
        if (!output) {
            error = "failed while writing file";
            std::error_code remove_error;
            std::filesystem::remove(temp_path, remove_error);
            return false;
        }
    }

    std::error_code rename_error;
    replace_file(temp_path, path, rename_error);
    if (rename_error) {
        error = "could not replace file: " + rename_error.message();
        std::error_code remove_error;
        std::filesystem::remove(temp_path, remove_error);
        return false;
    }

    return true;
}

} // namespace app

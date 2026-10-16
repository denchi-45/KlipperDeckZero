/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#include "application_config.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>

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

bool parse_port(const std::string& value, int& result) {
    if (value.empty() || value.size() > 5 ||
        !std::all_of(value.begin(), value.end(), [](unsigned char character) {
            return std::isdigit(character);
        })) {
        return false;
    }
    const int port = std::atoi(value.c_str());
    if (port < 1 || port > 65535) {
        return false;
    }
    result = port;
    return true;
}

const std::vector<SettingDescriptor> kSettings = {
    {Setting::DarkMode, "application", "dark_mode",
     [](const std::string& value, ApplicationConfig& config, std::string& error) {
         if (!parse_bool(value, config.dark_mode)) {
             error = "expected yes or no";
             return false;
         }
         return true;
     },
     [](const ApplicationConfig& config) -> std::string {
         return config.dark_mode ? "yes" : "no";
     }},
    {Setting::MoonrakerTransport, "moonraker", "transport",
     [](const std::string& value, ApplicationConfig& config, std::string& error) {
         const auto normalized = lowercase(value);
         if (normalized == "unix") {
             config.moonraker.transport = MoonrakerTransportKind::UnixSocket;
         }
         else if (normalized == "websocket") {
             config.moonraker.transport = MoonrakerTransportKind::WebSocket;
         }
         else {
             error = "expected unix or websocket";
             return false;
         }
         return true;
     },
     [](const ApplicationConfig& config) -> std::string {
         return config.moonraker.transport == MoonrakerTransportKind::UnixSocket ? "unix"
                                                                                  : "websocket";
     }},
    {Setting::MoonrakerSocketPath, "moonraker", "socket_path",
     [](const std::string& value, ApplicationConfig& config, std::string& error) {
         if (value.empty()) {
             error = "expected a file path";
             return false;
         }
         config.moonraker.socket_path = value;
         return true;
     },
     [](const ApplicationConfig& config) { return config.moonraker.socket_path; }},
    {Setting::MoonrakerHost, "moonraker", "host",
     [](const std::string& value, ApplicationConfig& config, std::string& error) {
         if (value.empty() || value.find_first_of(" \t/") != std::string::npos) {
             error = "expected an IP address or host name";
             return false;
         }
         config.moonraker.host = value;
         return true;
     },
     [](const ApplicationConfig& config) { return config.moonraker.host; }},
    {Setting::MoonrakerPort, "moonraker", "port",
     [](const std::string& value, ApplicationConfig& config, std::string& error) {
         if (!parse_port(value, config.moonraker.port)) {
             error = "expected a number from 1 to 65535";
             return false;
         }
         return true;
     },
     [](const ApplicationConfig& config) { return std::to_string(config.moonraker.port); }},
    {Setting::MoonrakerApiKey, "moonraker", "api_key",
     [](const std::string& value, ApplicationConfig& config, std::string&) {
         config.moonraker.api_key = value;
         return true;
     },
     [](const ApplicationConfig& config) { return config.moonraker.api_key; }},
};

const SettingDescriptor* find_setting(const std::string& section, const std::string& key) {
    for (const auto& descriptor : kSettings) {
        if (section == descriptor.section && key == descriptor.key) {
            return &descriptor;
        }
    }
    return nullptr;
}

bool is_comment(const std::string& trimmed_line) {
    return !trimmed_line.empty() && (trimmed_line.front() == '#' || trimmed_line.front() == ';');
}

bool is_section_header(const std::string& trimmed_line) {
    return trimmed_line.size() >= 2 && trimmed_line.front() == '[' && trimmed_line.back() == ']';
}

std::string section_name(const std::string& trimmed_line) {
    return lowercase(trim(trimmed_line.substr(1, trimmed_line.size() - 2)));
}

// Returns the lowercased key of a "key=value" line, or an empty string for any other line.
std::string key_name(const std::string& trimmed_line) {
    if (trimmed_line.empty() || is_comment(trimmed_line) || is_section_header(trimmed_line)) {
        return {};
    }
    const auto separator = trimmed_line.find('=');
    if (separator == std::string::npos) {
        return {};
    }
    return lowercase(trim(trimmed_line.substr(0, separator)));
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

std::string MoonrakerConfig::websocket_url() const {
    // An IPv6 address must be bracketed in a URL, or its colons are read as the port separator.
    const bool bracketed = host.size() >= 2 && host.front() == '[' && host.back() == ']';
    const bool needs_brackets = !bracketed && host.find(':') != std::string::npos;
    const auto url_host = needs_brackets ? "[" + host + "]" : host;
    return "ws://" + url_host + ":" + std::to_string(port) + "/websocket";
}

const std::vector<SettingDescriptor>& setting_descriptors() {
    return kSettings;
}

const SettingDescriptor& setting_descriptor(Setting id) {
    const auto found = std::find_if(kSettings.begin(), kSettings.end(),
                                    [id](const SettingDescriptor& descriptor) {
                                        return descriptor.id == id;
                                    });
    // Every Setting has a row; a missing one is a programming error caught by the tests.
    return found != kSettings.end() ? *found : kSettings.front();
}

std::string expand_user_path(const std::string& path) {
    if (path.size() < 2 || path[0] != '~' || (path[1] != '/' && path[1] != '\\')) {
        return path;
    }
    const char* home = std::getenv("HOME");
    if (!home || !*home) {
        return path;
    }
    return std::string(home) + path.substr(1);
}

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
        if (is_section_header(line)) {
            section = section_name(line);
            continue;
        }

        const auto key = key_name(line);
        if (key.empty()) {
            continue;
        }
        const auto* descriptor = find_setting(section, key);
        if (!descriptor) {
            continue;
        }

        const auto value = trim(line.substr(line.find('=') + 1));
        std::string reason;
        if (!descriptor->parse(value, config, reason)) {
            error = "invalid " + key + " value at line " + std::to_string(line_number);
            if (!reason.empty()) {
                error += " (" + reason + ")";
            }
            return false;
        }
    }

    return true;
}

bool save_application_config(const std::string& path,
                             const ApplicationConfig& config,
                             std::initializer_list<Setting> settings,
                             std::string& error) {
    return save_application_config(path, config, std::vector<Setting>(settings), error);
}

bool save_application_config(const std::string& path,
                             const ApplicationConfig& config,
                             const std::vector<Setting>& settings,
                             std::string& error) {
    error.clear();

    std::vector<const SettingDescriptor*> pending;
    for (const auto id : settings) {
        const auto* descriptor = &setting_descriptor(id);
        if (std::find(pending.begin(), pending.end(), descriptor) == pending.end()) {
            pending.push_back(descriptor);
        }
    }
    std::vector<bool> written(pending.size(), false);
    const auto setting_line = [&config](const SettingDescriptor& descriptor) {
        return std::string(descriptor.key) + "=" + descriptor.format(config);
    };

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
    {
        // Closed before the replace below; Windows refuses to replace a file that is still open.
        std::ifstream input(path);
        std::string line;
        while (std::getline(input, line)) {
            lines.push_back(line);
        }
    }

    std::vector<std::string> updated;
    std::string section;
    std::size_t section_start = 0;

    // Adds the settings of the current section that the file did not have yet, after its last
    // non-blank line.
    const auto finish_section = [&] {
        auto position = updated.size();
        while (position > section_start && trim(updated[position - 1]).empty()) {
            --position;
        }
        std::vector<std::string> additions;
        for (std::size_t i = 0; i < pending.size(); ++i) {
            if (!written[i] && section == pending[i]->section) {
                additions.push_back(setting_line(*pending[i]));
                written[i] = true;
            }
        }
        updated.insert(updated.begin() + static_cast<std::ptrdiff_t>(position),
                       additions.begin(),
                       additions.end());
    };

    for (const auto& current_line : lines) {
        const auto normalized = trim(current_line);
        if (is_section_header(normalized)) {
            finish_section();
            section = section_name(normalized);
            updated.push_back(current_line);
            section_start = updated.size();
            continue;
        }

        const auto key = key_name(normalized);
        const auto match = std::find_if(pending.begin(), pending.end(),
                                        [&](const SettingDescriptor* descriptor) {
                                            return !key.empty() && section == descriptor->section &&
                                                   key == descriptor->key;
                                        });
        if (match != pending.end()) {
            // The first occurrence takes the new value; later duplicates are dropped.
            const auto index = static_cast<std::size_t>(match - pending.begin());
            if (!written[index]) {
                updated.push_back(setting_line(**match));
                written[index] = true;
            }
            continue;
        }
        updated.push_back(current_line);
    }
    finish_section();

    // Settings whose section is not in the file yet go into new sections at the end.
    for (std::size_t i = 0; i < pending.size(); ++i) {
        if (written[i]) {
            continue;
        }
        if (!updated.empty() && !trim(updated.back()).empty()) {
            updated.emplace_back();
        }
        section = pending[i]->section;
        updated.push_back("[" + section + "]");
        section_start = updated.size();
        finish_section();
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

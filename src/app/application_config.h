/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 denchi-45
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <initializer_list>
#include <string>
#include <vector>

namespace app {

enum class MoonrakerTransportKind {
    UnixSocket, // Klipper/Moonraker running on this device
    WebSocket,  // Klipper/Moonraker on another host in the network
};

struct MoonrakerConfig {
    MoonrakerTransportKind transport = default_transport();
    std::string socket_path = "~/printer_data/comms/moonraker.sock";
    std::string host = "127.0.0.1";
    int port = 7125;
    std::string api_key;

    // ws://<host>:<port>/websocket
    std::string websocket_url() const;

    // Defaults to the Klipper on this device: the unix socket on the device, and the local
    // WebSocket in the desktop simulator, which cannot use unix sockets on Windows.
    static MoonrakerTransportKind default_transport() {
#if defined(USE_DESKTOP) && USE_DESKTOP
        return MoonrakerTransportKind::WebSocket;
#else
        return MoonrakerTransportKind::UnixSocket;
#endif
    }
};

struct ApplicationConfig {
    bool dark_mode = false;
    MoonrakerConfig moonraker;
};

// Every setting stored in the config file. To add one: add the field to ApplicationConfig, a
// value here, and a row in the table in application_config.cpp. Loading and saving pick it up
// from the table.
enum class Setting {
    DarkMode,
    MoonrakerTransport,
    MoonrakerSocketPath,
    MoonrakerHost,
    MoonrakerPort,
    MoonrakerApiKey,
};

struct SettingDescriptor {
    Setting id;
    const char* section;
    const char* key;
    // Stores value into config; returns false with a reason when the value is invalid.
    bool (*parse)(const std::string& value, ApplicationConfig& config, std::string& error);
    std::string (*format)(const ApplicationConfig& config);
};

const std::vector<SettingDescriptor>& setting_descriptors();
const SettingDescriptor& setting_descriptor(Setting id);

// Expands a leading "~/" using $HOME.
std::string expand_user_path(const std::string& path);

// Overrides only the settings present in the file; unknown sections and keys are ignored.
bool load_application_config(const std::string& path,
                             ApplicationConfig& config,
                             std::string& error);

// Writes the given settings into the file and keeps everything else (other keys, comments,
// unknown sections) as is. Settings left out are not written, so a user config only overrides
// what the user actually changed.
bool save_application_config(const std::string& path,
                             const ApplicationConfig& config,
                             std::initializer_list<Setting> settings,
                             std::string& error);
bool save_application_config(const std::string& path,
                             const ApplicationConfig& config,
                             const std::vector<Setting>& settings,
                             std::string& error);

} // namespace app

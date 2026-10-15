/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>

namespace app {

enum class MoonrakerTransportKind {
    UnixSocket,
    WebSocket,
};

struct MoonrakerConfig {
    MoonrakerTransportKind transport = default_transport();
    std::string socket_path = "~/printer_data/comms/moonraker.sock";
    std::string url = "ws://127.0.0.1:7125/websocket";
    std::string api_key;

    // Unix socket on the device, WebSocket in the desktop simulator.
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

// Expands a leading "~/" using $HOME.
std::string expand_user_path(const std::string& path);

bool load_application_config(const std::string& path,
                             ApplicationConfig& config,
                             std::string& error);
bool save_application_config(const std::string& path,
                             const ApplicationConfig& config,
                             std::string& error);

} // namespace app

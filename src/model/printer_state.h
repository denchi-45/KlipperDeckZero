/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <array>
#include <string>

namespace model {

// Klippy host state as reported by Moonraker `server.info` / `webhooks.state`.
enum class KlippyState {
    Unknown = 0,
    Disconnected,
    Startup,
    Ready,
    Error,
    Shutdown,
};

// `print_stats.state`
enum class PrintState {
    Unknown = 0,
    Standby,
    Printing,
    Paused,
    Complete,
    Cancelled,
    Error,
};

struct HeaterState {
    double temperature{0.0};
    double target{0.0};
    double power{0.0};
};

struct ExtruderState : HeaterState {
    bool can_extrude{false};
};

// Snapshot of the subscribed Klipper printer objects.
// Updated by merging Moonraker `notify_status_update` diffs.
struct PrinterState {
    KlippyState klippy_state{KlippyState::Unknown};
    std::string state_message;

    // print_stats
    PrintState print_state{PrintState::Unknown};
    std::string filename;
    double print_duration{0.0};
    double total_duration{0.0};
    double filament_used{0.0};
    std::string print_message;
    int current_layer{-1};
    int total_layer{-1};

    // virtual_sdcard
    double sdcard_progress{0.0};
    bool sdcard_active{false};

    // display_status (M73 / M117)
    double display_progress{0.0};
    std::string display_message;

    // heaters
    ExtruderState extruder;
    bool has_heater_bed{false};
    HeaterState heater_bed;

    // toolhead
    std::array<double, 4> position{};
    std::string homed_axes;

    // gcode_move
    double speed_factor{1.0};
    double extrude_factor{1.0};

    // fan
    double fan_speed{0.0};

    // idle_timeout ("Idle" / "Ready" / "Printing")
    std::string idle_state;

    // Best available progress in [0, 1]: M73 progress if present, else file progress.
    double progress() const {
        return display_progress > 0.0 ? display_progress : sdcard_progress;
    }

    // Remaining seconds estimated from elapsed print time and progress; < 0 if unknown.
    double estimated_remaining() const {
        const double p = progress();
        if (p <= 0.0 || print_duration <= 0.0) {
            return -1.0;
        }
        return print_duration / p - print_duration;
    }
};

inline const char* to_string(KlippyState state) {
    switch (state) {
        case KlippyState::Disconnected:
            return "disconnected";
        case KlippyState::Startup:
            return "startup";
        case KlippyState::Ready:
            return "ready";
        case KlippyState::Error:
            return "error";
        case KlippyState::Shutdown:
            return "shutdown";
        case KlippyState::Unknown:
            break;
    }
    return "unknown";
}

inline const char* to_string(PrintState state) {
    switch (state) {
        case PrintState::Standby:
            return "standby";
        case PrintState::Printing:
            return "printing";
        case PrintState::Paused:
            return "paused";
        case PrintState::Complete:
            return "complete";
        case PrintState::Cancelled:
            return "cancelled";
        case PrintState::Error:
            return "error";
        case PrintState::Unknown:
            break;
    }
    return "unknown";
}

inline KlippyState parse_klippy_state(const std::string& value) {
    if (value == "ready") {
        return KlippyState::Ready;
    }
    if (value == "startup") {
        return KlippyState::Startup;
    }
    if (value == "error") {
        return KlippyState::Error;
    }
    if (value == "shutdown") {
        return KlippyState::Shutdown;
    }
    if (value == "disconnected") {
        return KlippyState::Disconnected;
    }
    return KlippyState::Unknown;
}

inline PrintState parse_print_state(const std::string& value) {
    if (value == "standby") {
        return PrintState::Standby;
    }
    if (value == "printing") {
        return PrintState::Printing;
    }
    if (value == "paused") {
        return PrintState::Paused;
    }
    if (value == "complete") {
        return PrintState::Complete;
    }
    if (value == "cancelled") {
        return PrintState::Cancelled;
    }
    if (value == "error") {
        return PrintState::Error;
    }
    return PrintState::Unknown;
}

} // namespace model

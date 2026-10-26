/*
 * SPDX-License-Identifier: MIT
 */

#include "printer_format.h"

#include <cctype>
#include <cmath>
#include <cstdio>

namespace viewmodel::format {
namespace {

std::string printf_string(const char* format, double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), format, value);
    return buffer;
}

int round_percent(double fraction) {
    return static_cast<int>(std::lround(fraction * 100.0));
}

} // namespace

std::string duration(double seconds) {
    if (seconds < 0.0 || !std::isfinite(seconds)) {
        return "--:--";
    }
    const auto total = static_cast<long>(seconds + 0.5);
    const long hours = total / 3600;
    const long minutes = (total / 60) % 60;
    const long secs = total % 60;
    char buffer[24];
    if (hours > 0) {
        std::snprintf(buffer, sizeof(buffer), "%ld:%02ld:%02ld", hours, minutes, secs);
    }
    else {
        std::snprintf(buffer, sizeof(buffer), "%02ld:%02ld", minutes, secs);
    }
    return buffer;
}

std::string temperature(double celsius) {
    return printf_string("%.1f", celsius);
}

std::string target(double celsius) {
    if (celsius <= 0.0) {
        return "off";
    }
    return printf_string("/ %.0f", celsius);
}

std::string percent(double fraction) {
    if (fraction < 0.0) {
        fraction = 0.0;
    }
    if (fraction > 1.0) {
        fraction = 1.0;
    }
    return std::to_string(round_percent(fraction)) + "%";
}

std::string layers(int current, int total) {
    if (current < 0 || total <= 0) {
        return "--";
    }
    return std::to_string(current) + " / " + std::to_string(total);
}

std::string factors(const model::PrinterState& printer) {
    return "Speed " + std::to_string(round_percent(printer.speed_factor)) + "%  Flow " +
           std::to_string(round_percent(printer.extrude_factor)) + "%  Fan " +
           std::to_string(round_percent(printer.fan_speed)) + "%";
}

std::string toolhead_position(const model::PrinterState& printer) {
    return "X " + printf_string("%.1f", printer.position[0]) + "  Y " + printf_string("%.1f", printer.position[1]) +
           "  Z " + printf_string("%.2f", printer.position[2]);
}

std::string homed_axes(const std::string& homed_axes) {
    std::string text;
    for (const char axis : homed_axes) {
        text += static_cast<char>(std::toupper(static_cast<unsigned char>(axis)));
    }
    return text;
}

std::string job_name(const std::string& filename) {
    if (filename.empty()) {
        return "No active print";
    }
    const auto slash = filename.find_last_of('/');
    return slash == std::string::npos ? filename : filename.substr(slash + 1);
}

bool FileRevision::update(const std::string& filename) {
    if (filename == filename_) {
        return false;
    }
    filename_ = filename;
    ++revision_;
    return true;
}

StatusBadge status_badge(moonraker::SessionState session, const model::PrinterState& printer) {
    switch (session) {
        case moonraker::SessionState::Stopped:
        case moonraker::SessionState::Connecting:
        case moonraker::SessionState::Initializing:
            return {"OFFLINE", Tone::Neutral};
        case moonraker::SessionState::WaitingKlippy:
            return {"STARTING", Tone::Warning};
        case moonraker::SessionState::KlippyError:
            return {printer.klippy_state == model::KlippyState::Shutdown ? "SHUTDOWN" : "ERROR", Tone::Error};
        case moonraker::SessionState::Ready:
            break;
    }

    switch (printer.print_state) {
        case model::PrintState::Printing:
            return {"PRINTING", Tone::Info};
        case model::PrintState::Paused:
            return {"PAUSED", Tone::Warning};
        case model::PrintState::Complete:
            return {"COMPLETE", Tone::Success};
        case model::PrintState::Cancelled:
            return {"CANCELLED", Tone::Warning};
        case model::PrintState::Error:
            return {"ERROR", Tone::Error};
        case model::PrintState::Standby:
        case model::PrintState::Unknown:
            break;
    }
    return {"READY", Tone::Success};
}

ConnectionOverlay connection_overlay(moonraker::SessionState session,
                                     const model::PrinterState& printer,
                                     const std::string& endpoint,
                                     const std::string& last_error) {
    // Moonraker is reachable, so Klipper itself can be restarted from here.
    constexpr const char* kRestartHint = "R: restart  F: firmware restart  S: settings";

    switch (session) {
        case moonraker::SessionState::Ready:
            return {};
        case moonraker::SessionState::Stopped:
            return {true, "Offline", endpoint, Tone::Neutral};
        case moonraker::SessionState::Connecting:
            return {true,
                    "Connecting to Moonraker...",
                    last_error.empty() ? endpoint : endpoint + "\n" + last_error,
                    last_error.empty() ? Tone::Neutral : Tone::Warning};
        case moonraker::SessionState::Initializing:
            return {true, "Connecting to Moonraker...", endpoint, Tone::Neutral};
        case moonraker::SessionState::WaitingKlippy:
            return {true,
                    "Waiting for Klipper...",
                    std::string("Klippy state: ") + model::to_string(printer.klippy_state),
                    Tone::Warning,
                    kRestartHint};
        case moonraker::SessionState::KlippyError:
            return {true,
                    printer.klippy_state == model::KlippyState::Shutdown ? "Klipper shutdown" : "Klipper error",
                    printer.state_message.empty() ? std::string("No details reported") : printer.state_message,
                    Tone::Error,
                    kRestartHint};
    }
    return {};
}

} // namespace viewmodel::format

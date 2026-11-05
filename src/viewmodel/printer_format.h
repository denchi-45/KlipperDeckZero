/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "moonraker_session.h"
#include "printer_state.h"

#include <cstdint>
#include <string>

// Text formatting for printer data shown on screen. Free of LVGL so it can be unit tested.
namespace viewmodel::format {

enum class Tone {
    Neutral = 0,
    Info,
    Success,
    Warning,
    Error,
};

struct StatusBadge {
    std::string text;
    Tone tone{Tone::Neutral};
};

struct ConnectionOverlay {
    bool visible{false};
    std::string title;
    std::string detail;
    Tone tone{Tone::Neutral};
    // Keys that help in this state.
    std::string hint{"S: connection settings"};
};

// "1:02:03" for >= 1 hour, "02:03" otherwise, "--:--" when negative (unknown).
std::string duration(double seconds);
// "209.8"
std::string temperature(double celsius);
// "/ 210" or "off"
std::string target(double celsius);
// "42%" (fraction in [0, 1])
std::string percent(double fraction);
// "12 / 120", or "--" when unknown
std::string layers(int current, int total);
// "Speed 100%  Flow 95%  Fan 50%"
std::string factors(const model::PrinterState& printer);
// "X 120.0  Y 80.5  Z 10.20" from toolhead.position.
std::string toolhead_position(const model::PrinterState& printer);
// toolhead.homed_axes in upper case ("XYZ"); empty when no axis is homed.
std::string homed_axes(const std::string& homed_axes);
// Basename of a G-code path; "No active print" when empty.
std::string job_name(const std::string& filename);

// Counts changes of the full G-code path, so a switch between same-named files in
// different directories is noticed even though job_name() stays the same.
class FileRevision {
public:
    // Returns true (and advances the revision) only when the path differs from the last one.
    bool update(const std::string& filename);
    int32_t revision() const { return revision_; }

private:
    std::string filename_;
    int32_t revision_{0};
};

StatusBadge status_badge(moonraker::SessionState session, const model::PrinterState& printer);
ConnectionOverlay connection_overlay(moonraker::SessionState session,
                                     const model::PrinterState& printer,
                                     const std::string& endpoint,
                                     const std::string& last_error,
                                     bool klippy_unresponsive = false);

} // namespace viewmodel::format

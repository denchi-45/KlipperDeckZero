/*
 * SPDX-License-Identifier: MIT
 */

#include "temperature_panel.h"

#include "gcode.h"

#include <algorithm>
#include <cmath>

namespace app {
namespace {

const std::vector<TemperaturePreset> kPresets = {
    {"PLA", 200, 60},
    {"PETG", 235, 80},
    {"Off", 0, 0},
};

std::string describe_target(const char* name, int target) {
    return std::string(name) + " " + (target > 0 ? std::to_string(target) : std::string("off"));
}

} // namespace

const std::vector<TemperaturePreset>& temperature_presets() {
    return kPresets;
}

std::string sensor_line(const std::vector<model::SensorState>& sensors, double device_celsius) {
    std::string line;
    const auto add = [&line](const std::string& name, bool known, double celsius) {
        if (!line.empty()) {
            line += "   ";
        }
        line += name + " " + (known ? std::to_string(static_cast<int>(std::lround(celsius))) : std::string("--"));
    };
    for (const auto& sensor : sensors) {
        add(sensor.name, sensor.known, sensor.temperature);
    }
    if (device_celsius >= 0.0) {
        add("CardputerZero", true, device_celsius);
    }
    return line;
}

std::string temperature_script(const TemperatureRequest& request) {
    std::string script;
    if (request.nozzle) {
        script = gcode::set_heater_temperature(gcode::Heater::Nozzle, *request.nozzle);
    }
    if (request.bed) {
        if (!script.empty()) {
            script += "\n";
        }
        script += gcode::set_heater_temperature(gcode::Heater::Bed, *request.bed);
    }
    return script;
}

std::string describe(const TemperatureRequest& request) {
    std::string text;
    if (request.nozzle) {
        text = describe_target("Nozzle", *request.nozzle);
    }
    if (request.bed) {
        if (!text.empty()) {
            text += ", ";
        }
        text += describe_target("Bed", *request.bed);
    }
    return text;
}

TemperaturePanel::TemperaturePanel(bool has_bed) : has_bed_(has_bed) {}

void TemperaturePanel::set_has_bed(bool has_bed) {
    if (has_bed_ == has_bed) {
        return;
    }
    const auto row = focused();
    has_bed_ = has_bed;
    if (row == TemperatureRow::Bed) {
        editing_ = false;
        error_.clear();
    }
    const auto visible = rows();
    const auto it = std::find(visible.begin(), visible.end(), row);
    focus_ = it == visible.end() ? 0 : static_cast<std::size_t>(it - visible.begin());
}

TemperaturePanel::Result TemperaturePanel::handle(PanelInput input, char digit, const HeaterTargets& current) {
    if (editing_) {
        return handle_edit(input, digit);
    }

    const auto row = focused();
    switch (input) {
        case PanelInput::Up:
            move_focus(-1);
            return Result::Handled;
        case PanelInput::Down:
            move_focus(1);
            return Result::Handled;
        case PanelInput::Left:
        case PanelInput::Right: {
            const int direction = input == PanelInput::Right ? 1 : -1;
            if (row == TemperatureRow::Presets) {
                const auto count = static_cast<int>(kPresets.size());
                preset_ = static_cast<std::size_t>((static_cast<int>(preset_) + direction + count) % count);
                return Result::Handled;
            }
            begin_edit(row == TemperatureRow::Nozzle ? current.nozzle : current.bed);
            step_draft(direction);
            return Result::Handled;
        }
        case PanelInput::Enter:
            if (row == TemperatureRow::Presets) {
                const auto& preset = kPresets[preset_];
                request_ = {};
                request_.nozzle = preset.nozzle;
                if (has_bed_) {
                    request_.bed = preset.bed;
                }
                return Result::Apply;
            }
            begin_edit(row == TemperatureRow::Nozzle ? current.nozzle : current.bed);
            return Result::Handled;
        case PanelInput::Escape:
        case PanelInput::Backspace:
        case PanelInput::Digit:
            return Result::Ignored;
    }
    return Result::Ignored;
}

TemperaturePanel::Result TemperaturePanel::handle_edit(PanelInput input, char digit) {
    switch (input) {
        case PanelInput::Digit:
            if (digit < '0' || digit > '9') {
                return Result::Handled;
            }
            if (!typed_ || buffer_ == "0") {
                buffer_.clear();
                typed_ = true;
            }
            if (buffer_.size() < kMaxDigits) {
                buffer_.push_back(digit);
            }
            error_.clear();
            return Result::Handled;
        case PanelInput::Backspace:
            if (!typed_) {
                buffer_.clear();
                typed_ = true;
            }
            else if (!buffer_.empty()) {
                buffer_.pop_back();
            }
            error_.clear();
            return Result::Handled;
        case PanelInput::Left:
        case PanelInput::Right:
            step_draft(input == PanelInput::Right ? 1 : -1);
            error_.clear();
            return Result::Handled;
        case PanelInput::Enter: {
            const int value = draft_value();
            if (value > max_target()) {
                error_ = "Max " + std::to_string(max_target());
                return Result::Handled;
            }
            request_ = {};
            if (focused() == TemperatureRow::Nozzle) {
                request_.nozzle = value;
            }
            else {
                request_.bed = value;
            }
            editing_ = false;
            error_.clear();
            return Result::Apply;
        }
        case PanelInput::Escape:
            editing_ = false;
            error_.clear();
            return Result::Handled;
        case PanelInput::Up:
        case PanelInput::Down:
            editing_ = false;
            error_.clear();
            move_focus(input == PanelInput::Down ? 1 : -1);
            return Result::Handled;
    }
    return Result::Handled;
}

std::vector<TemperatureRow> TemperaturePanel::rows() const {
    if (has_bed_) {
        return {TemperatureRow::Nozzle, TemperatureRow::Bed, TemperatureRow::Presets};
    }
    return {TemperatureRow::Nozzle, TemperatureRow::Presets};
}

TemperatureRow TemperaturePanel::focused() const {
    const auto visible = rows();
    return visible[std::min(focus_, visible.size() - 1)];
}

bool TemperaturePanel::editing() const {
    return editing_;
}

std::size_t TemperaturePanel::preset() const {
    return preset_;
}

const std::string& TemperaturePanel::draft_text() const {
    return buffer_;
}

const TemperatureRequest& TemperaturePanel::request() const {
    return request_;
}

const std::string& TemperaturePanel::error() const {
    return error_;
}

void TemperaturePanel::move_focus(int step) {
    const auto count = static_cast<int>(rows().size());
    focus_ = static_cast<std::size_t>(std::clamp(static_cast<int>(focus_) + step, 0, count - 1));
}

void TemperaturePanel::begin_edit(int target) {
    editing_ = true;
    typed_ = false;
    buffer_ = std::to_string(std::clamp(target, 0, max_target()));
    error_.clear();
}

void TemperaturePanel::step_draft(int direction) {
    const int value = draft_value();
    // Snap to the step grid so 212 goes to 215 / 210 rather than 217 / 207.
    int next = direction > 0 ? (value / kStep + 1) * kStep
                             : (value % kStep != 0 ? value - value % kStep : value - kStep);
    next = std::clamp(next, 0, max_target());
    buffer_ = std::to_string(next);
    typed_ = false;
}

int TemperaturePanel::draft_value() const {
    return buffer_.empty() ? 0 : std::stoi(buffer_);
}

int TemperaturePanel::max_target() const {
    return focused() == TemperatureRow::Bed ? kMaxBed : kMaxNozzle;
}

} // namespace app

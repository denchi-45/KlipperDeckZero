/*
 * SPDX-License-Identifier: MIT
 */

#include "extruder_panel.h"

#include <algorithm>
#include <cstdlib>

namespace app {
namespace {

const std::vector<int> kLengths = {1, 5, 10, 25, 50};
const std::vector<int> kSpeeds = {2, 5, 10};
constexpr std::size_t kDefaultLength = 2; // 10 mm
constexpr std::size_t kDefaultSpeed = 1;  // 5 mm/s
constexpr int kRowCount = 3;

std::size_t step_index(std::size_t index, int step, std::size_t count) {
    return static_cast<std::size_t>(
        std::clamp(static_cast<int>(index) + step, 0, static_cast<int>(count) - 1));
}

} // namespace

const char* extrude_block_reason(const model::PrinterState& printer) {
    if (printer.print_state == model::PrintState::Printing) {
        return "Not available while printing";
    }
    if (!printer.extruder.can_extrude) {
        return "Nozzle too cold to extrude";
    }
    return nullptr;
}

std::string describe(const ExtruderRequest& request) {
    return std::string(request.length_mm < 0 ? "Retracted " : "Extruded ") +
           std::to_string(std::abs(request.length_mm)) + " mm";
}

const std::vector<int>& ExtruderPanel::lengths() {
    return kLengths;
}

const std::vector<int>& ExtruderPanel::speeds() {
    return kSpeeds;
}

ExtruderPanel::ExtruderPanel() : length_index_(kDefaultLength), speed_index_(kDefaultSpeed) {}

ExtruderPanel::Result ExtruderPanel::handle(PanelInput input) {
    switch (input) {
        case PanelInput::Up:
        case PanelInput::Down:
            focus_ = static_cast<std::size_t>(
                std::clamp(static_cast<int>(focus_) + (input == PanelInput::Down ? 1 : -1), 0, kRowCount - 1));
            return Result::Handled;
        case PanelInput::Left:
        case PanelInput::Right: {
            const int step = input == PanelInput::Right ? 1 : -1;
            switch (focused()) {
                case ExtruderRow::Length:
                    length_index_ = step_index(length_index_, step, kLengths.size());
                    break;
                case ExtruderRow::Speed:
                    speed_index_ = step_index(speed_index_, step, kSpeeds.size());
                    break;
                case ExtruderRow::Direction:
                    direction_ = step > 0 ? ExtruderDirection::Extrude : ExtruderDirection::Retract;
                    break;
            }
            return Result::Handled;
        }
        case PanelInput::Enter:
            return Result::Apply;
        case PanelInput::Escape:
        case PanelInput::Backspace:
        case PanelInput::Digit:
            return Result::Ignored;
    }
    return Result::Ignored;
}

ExtruderRow ExtruderPanel::focused() const {
    return static_cast<ExtruderRow>(focus_);
}

std::size_t ExtruderPanel::length_index() const {
    return length_index_;
}

std::size_t ExtruderPanel::speed_index() const {
    return speed_index_;
}

ExtruderDirection ExtruderPanel::direction() const {
    return direction_;
}

ExtruderRequest ExtruderPanel::request() const {
    const int length = kLengths[length_index_];
    return {direction_ == ExtruderDirection::Retract ? -length : length, kSpeeds[speed_index_]};
}

} // namespace app

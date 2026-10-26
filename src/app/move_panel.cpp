/*
 * SPDX-License-Identifier: MIT
 */

#include "move_panel.h"

#include "gcode.h"

#include <algorithm>
#include <cctype>

namespace app {
namespace {

const std::vector<double> kSteps = {0.1, 1.0, 10.0, 50.0};
constexpr std::size_t kDefaultStep = 2; // 10 mm
constexpr int kRowCount = 7;
constexpr int kAxisCount = 3;
constexpr int kXyFeedMmMin = 3000;
constexpr int kZFeedMmMin = 600;

// Steps index by one and wraps around at both ends.
std::size_t cycle_index(std::size_t index, int step, std::size_t count) {
    const int size = static_cast<int>(count);
    return static_cast<std::size_t>((static_cast<int>(index) + step + size) % size);
}

} // namespace

char axis_letter(MoveAxis axis) {
    switch (axis) {
        case MoveAxis::X:
            return 'X';
        case MoveAxis::Y:
            return 'Y';
        case MoveAxis::Z:
            return 'Z';
    }
    return 'X';
}

int jog_feed(MoveAxis axis) {
    return axis == MoveAxis::Z ? kZFeedMmMin : kXyFeedMmMin;
}

bool axis_homed(const std::string& homed_axes, MoveAxis axis) {
    const char letter = static_cast<char>(std::tolower(static_cast<unsigned char>(axis_letter(axis))));
    return std::any_of(homed_axes.begin(), homed_axes.end(), [letter](char c) {
        return std::tolower(static_cast<unsigned char>(c)) == letter;
    });
}

MoveBlock move_block(const model::PrinterState& printer, MoveAction action, MoveAxis axis) {
    if (printer.print_state == model::PrintState::Printing) {
        return MoveBlock::Printing;
    }
    if (action == MoveAction::Jog) {
        // Klipper refuses moves of an unhomed axis ("Must home axis first").
        return axis_homed(printer.homed_axes, axis) ? MoveBlock::None : MoveBlock::NotHomed;
    }
    // Jogging is fine while paused (e.g. to clear the nozzle); RESUME moves back.
    if (printer.print_state == model::PrintState::Paused) {
        return MoveBlock::Paused;
    }
    return MoveBlock::None;
}

const char* move_block_reason(MoveBlock block) {
    switch (block) {
        case MoveBlock::Printing:
            return "Not available while printing";
        case MoveBlock::Paused:
            return "Not available while paused";
        case MoveBlock::NotHomed:
            return "Home the axis first";
        case MoveBlock::None:
            break;
    }
    return nullptr;
}

std::string describe(const JogRequest& request) {
    return std::string("Moved ") + axis_letter(request.axis) + " " + (request.distance_mm < 0 ? "" : "+") +
           gcode::format_distance(request.distance_mm) + " mm";
}

const std::vector<double>& MovePanel::steps() {
    return kSteps;
}

MovePanel::MovePanel() : step_index_(kDefaultStep) {}

MovePanel::Result MovePanel::handle(PanelInput input, const model::PrinterState* printer) {
    switch (input) {
        case PanelInput::Up:
        case PanelInput::Down:
            focus_ = static_cast<std::size_t>(
                std::clamp(static_cast<int>(focus_) + (input == PanelInput::Down ? 1 : -1), 0, kRowCount - 1));
            return Result::Handled;
        case PanelInput::Left:
        case PanelInput::Right: {
            const int step = input == PanelInput::Right ? 1 : -1;
            if (focused() == MoveRow::Axis) {
                axis_ = static_cast<MoveAxis>(cycle_index(static_cast<std::size_t>(axis_), step, kAxisCount));
            }
            else if (focused() == MoveRow::Step) {
                step_index_ = cycle_index(step_index_, step, kSteps.size());
            }
            return Result::Handled;
        }
        case PanelInput::Enter:
            break;
        case PanelInput::Escape:
        case PanelInput::Backspace:
        case PanelInput::Digit:
            return Result::Ignored;
    }

    block_ = printer ? focused_block(*printer) : MoveBlock::None;
    if (block_ != MoveBlock::None) {
        return Result::Blocked;
    }
    switch (focused()) {
        case MoveRow::Axis:
        case MoveRow::Step:
            return Result::Handled;
        case MoveRow::JogMinus:
        case MoveRow::JogPlus:
            return Result::Jog;
        case MoveRow::HomeAxis:
            return Result::HomeAxis;
        case MoveRow::HomeAll:
            return Result::HomeAll;
        case MoveRow::MotorsOff:
            return Result::MotorsOff;
    }
    return Result::Handled;
}

MoveRow MovePanel::focused() const {
    return static_cast<MoveRow>(focus_);
}

MoveAxis MovePanel::axis() const {
    return axis_;
}

std::size_t MovePanel::step_index() const {
    return step_index_;
}

double MovePanel::step() const {
    return kSteps[step_index_];
}

JogRequest MovePanel::request() const {
    const double distance = focused() == MoveRow::JogMinus ? -step() : step();
    return {axis_, distance, jog_feed(axis_)};
}

MoveBlock MovePanel::focused_block(const model::PrinterState& printer) const {
    switch (focused()) {
        case MoveRow::Axis:
        case MoveRow::Step:
            return MoveBlock::None;
        case MoveRow::JogMinus:
        case MoveRow::JogPlus:
            return move_block(printer, MoveAction::Jog, axis_);
        case MoveRow::HomeAxis:
        case MoveRow::HomeAll:
            return move_block(printer, MoveAction::Home, axis_);
        case MoveRow::MotorsOff:
            return move_block(printer, MoveAction::MotorsOff, axis_);
    }
    return MoveBlock::None;
}

MoveBlock MovePanel::block() const {
    return block_;
}

} // namespace app

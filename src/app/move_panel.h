/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "panel_input.h"
#include "printer_state.h"

#include <cstddef>
#include <string>
#include <vector>

namespace app {

enum class MoveRow {
    Axis,
    Step,
    JogMinus,
    JogPlus,
    HomeAxis,
    HomeAll,
    MotorsOff,
};

enum class MoveAxis {
    X,
    Y,
    Z,
};

// What a row asks the printer to do; the block rules differ between them.
enum class MoveAction {
    Jog,
    Home,
    MotorsOff,
};

// Why the printer can't run a move action now.
enum class MoveBlock {
    None,
    Printing, // every action
    Paused,   // homing and motors off: they would lose the paused position
    NotHomed, // jogging an axis Klipper has not homed
};

// One relative move of a single axis. distance_mm is negative towards the axis minimum.
struct JogRequest {
    MoveAxis axis{MoveAxis::X};
    double distance_mm{0.0};
    int feed_mm_min{0};
};

// 'X' / 'Y' / 'Z'
char axis_letter(MoveAxis axis);
// Feed rate of a jog: 3000 mm/min for X and Y, 600 mm/min for Z.
int jog_feed(MoveAxis axis);
// True when toolhead.homed_axes (e.g. "xyz") contains the axis.
bool axis_homed(const std::string& homed_axes, MoveAxis axis);
MoveBlock move_block(const model::PrinterState& printer, MoveAction action, MoveAxis axis);
// Toast text for a block, or nullptr for MoveBlock::None.
const char* move_block_reason(MoveBlock block);
// "Moved X +10 mm" / "Moved Z -0.1 mm": confirmation text for the request.
std::string describe(const JogRequest& request);

// Keyboard control of the axes, independent of LVGL.
// Up/Down pick a row. Left/Right cycle the axis or the step on their rows; Enter runs the
// focused action row (jog -, jog +, home the axis, home all, motors off) on the selected axis.
class MovePanel {
public:
    enum class Result {
        Ignored,   // not used by the panel; the caller may treat it as a shortcut
        Handled,
        Jog,       // request() holds the move
        HomeAxis,  // home axis() only
        HomeAll,
        MotorsOff,
        Blocked,   // block() tells why the focused action was refused
    };

    static const std::vector<double>& steps();

    MovePanel();

    // printer is nullptr while it is not ready; actions then pass through so the commands can
    // report the connection.
    Result handle(PanelInput input, const model::PrinterState* printer);

    MoveRow focused() const;
    MoveAxis axis() const;
    std::size_t step_index() const;
    double step() const;
    // The jog of the focused row: negative on Jog -, positive otherwise.
    JogRequest request() const;
    // The block of the focused action row on this printer; None on the axis and step rows.
    MoveBlock focused_block(const model::PrinterState& printer) const;
    // Why the last Enter returned Blocked.
    MoveBlock block() const;

private:
    std::size_t focus_{0};
    MoveAxis axis_{MoveAxis::X};
    std::size_t step_index_;
    MoveBlock block_{MoveBlock::None};
};

} // namespace app

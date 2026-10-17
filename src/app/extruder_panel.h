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

enum class ExtruderRow {
    Length,
    Speed,
    Direction,
};

enum class ExtruderDirection {
    Retract,
    Extrude,
};

// One filament move. length_mm is negative for a retract.
struct ExtruderRequest {
    int length_mm{0};
    int speed_mm_s{0};
};

// Why the printer can't move the filament now, or nullptr when it can.
const char* extrude_block_reason(const model::PrinterState& printer);
// "Extruded 10 mm" / "Retracted 5 mm": confirmation text for the request.
std::string describe(const ExtruderRequest& request);

// Keyboard selection of a filament move, independent of LVGL.
// Up/Down pick a row and Left/Right change it. Enter runs the move from any row.
class ExtruderPanel {
public:
    enum class Result {
        Ignored, // not used by the panel; the caller may treat it as a shortcut
        Handled,
        Apply,   // request() holds the move to run
    };

    static const std::vector<int>& lengths();
    static const std::vector<int>& speeds();

    ExtruderPanel();

    Result handle(PanelInput input);

    ExtruderRow focused() const;
    std::size_t length_index() const;
    std::size_t speed_index() const;
    ExtruderDirection direction() const;
    ExtruderRequest request() const;

private:
    std::size_t focus_{0};
    std::size_t length_index_;
    std::size_t speed_index_;
    ExtruderDirection direction_{ExtruderDirection::Extrude};
};

} // namespace app

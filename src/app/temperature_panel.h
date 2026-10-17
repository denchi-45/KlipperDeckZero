/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "panel_input.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace app {

enum class TemperatureRow {
    Nozzle,
    Bed,
    Presets,
};

struct TemperaturePreset {
    const char* label;
    int nozzle;
    int bed;
};

// Presets offered on the temperature page, in display order.
const std::vector<TemperaturePreset>& temperature_presets();

// Targets to send when the panel returns Apply. A heater without a value is left as it is.
struct TemperatureRequest {
    std::optional<int> nozzle;
    std::optional<int> bed;
};

// G-code that applies the request, one SET_HEATER_TEMPERATURE line per heater.
std::string temperature_script(const TemperatureRequest& request);
// "Nozzle 215", "Bed off", "Nozzle 200, Bed 60": confirmation text for the request.
std::string describe(const TemperatureRequest& request);

// The printer's current targets, used as the starting value of an edit.
struct HeaterTargets {
    int nozzle{0};
    int bed{0};
};

// Keyboard editing of the heater targets, independent of LVGL.
// Up/Down pick a row. On a heater row, Left/Right step the target and Enter starts typing it;
// a second Enter applies it. On the presets row, Left/Right pick a preset and Enter applies it.
class TemperaturePanel {
public:
    enum class Result {
        Ignored, // not used by the panel; the caller may treat it as a shortcut
        Handled,
        Apply,   // request() holds the targets to send
    };

    static constexpr int kStep = 5;
    static constexpr int kMaxNozzle = 350;
    static constexpr int kMaxBed = 150;
    static constexpr std::size_t kMaxDigits = 3;

    explicit TemperaturePanel(bool has_bed = true);

    // Hides the bed row when the printer has no heated bed.
    void set_has_bed(bool has_bed);
    Result handle(PanelInput input, char digit, const HeaterTargets& current);

    std::vector<TemperatureRow> rows() const;
    TemperatureRow focused() const;
    bool editing() const;
    std::size_t preset() const;
    // The target being edited, as typed or stepped; empty when every digit was deleted.
    const std::string& draft_text() const;
    // Valid after handle() returned Apply.
    const TemperatureRequest& request() const;
    // Validation error of the last edit, empty when none.
    const std::string& error() const;

private:
    Result handle_edit(PanelInput input, char digit);
    void move_focus(int step);
    void begin_edit(int target);
    void step_draft(int direction);
    int draft_value() const;
    int max_target() const;

    bool has_bed_;
    std::size_t focus_{0};
    std::size_t preset_{0};
    bool editing_{false};
    // True once a digit or Backspace replaced the starting value.
    bool typed_{false};
    std::string buffer_;
    TemperatureRequest request_;
    std::string error_;
};

} // namespace app

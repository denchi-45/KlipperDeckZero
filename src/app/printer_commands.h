/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "extruder_panel.h"
#include "printer_state.h"
#include "temperature_panel.h"

#include <string>

namespace moonraker {
class MoonrakerSession;
}

namespace view::widgets {
class Toast;
}

namespace app {

// Printer operations started from the pages. Reports the outcome of each one with a toast.
class PrinterCommands {
public:
    explicit PrinterCommands(view::widgets::Toast& toast);

    PrinterCommands(const PrinterCommands&) = delete;
    PrinterCommands& operator=(const PrinterCommands&) = delete;

    // The session commands go to; nullptr while it is being replaced.
    void set_session(moonraker::MoonrakerSession* session);
    // Live printer state, or nullptr unless the session is ready.
    const model::PrinterState* printer() const;

    void set_temperatures(const TemperatureRequest& request);
    // Refused while printing or while the nozzle is too cold.
    void move_extruder(const ExtruderRequest& request);

private:
    // Shows a warning and returns false unless commands can be sent now.
    bool require_ready();
    void run_script(const std::string& script, const std::string& done_message);

    view::widgets::Toast& toast_;
    moonraker::MoonrakerSession* session_{nullptr};
};

} // namespace app

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "extruder_panel.h"
#include "gcode_files.h"
#include "printer_state.h"
#include "temperature_panel.h"

#include <functional>
#include <string>
#include <vector>

namespace moonraker {
class MoonrakerSession;
}

namespace view::widgets {
class Toast;
}

namespace viewmodel {
class BaseViewModel;
}

namespace app {

// Printer operations started from the pages. Reports the outcome of each one with a toast.
class PrinterCommands {
public:
    // error is empty on success.
    using FilesHandler = std::function<void(std::vector<GcodeFile> files, const std::string& error)>;
    // metadata is nullptr when the file has none.
    using MetadataHandler = std::function<void(const GcodeMetadata* metadata)>;

    PrinterCommands(view::widgets::Toast& toast, viewmodel::BaseViewModel& view_model);

    PrinterCommands(const PrinterCommands&) = delete;
    PrinterCommands& operator=(const PrinterCommands&) = delete;

    // The session commands go to; nullptr while it is being replaced.
    void set_session(moonraker::MoonrakerSession* session);
    // Live printer state, or nullptr unless the session is ready.
    const model::PrinterState* printer() const;

    void set_temperatures(const TemperatureRequest& request);
    // Refused while printing or while the nozzle is too cold.
    void move_extruder(const ExtruderRequest& request);

    // G-code files, newest first. Works while Klipper is not ready, as long as Moonraker is up.
    void list_files(FilesHandler handler);
    void file_metadata(const std::string& path, MetadataHandler handler);
    // Refused while a print is running or paused. Shows the status page once it started.
    void start_print(const std::string& path);

private:
    // Shows a warning and returns false unless commands can be sent now.
    bool require_ready();
    void run_script(const std::string& script, const std::string& done_message);

    view::widgets::Toast& toast_;
    viewmodel::BaseViewModel& view_model_;
    moonraker::MoonrakerSession* session_{nullptr};
};

} // namespace app

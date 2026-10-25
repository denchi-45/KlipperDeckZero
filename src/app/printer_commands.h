/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "extruder_panel.h"
#include "gcode_files.h"
#include "gcode_state_guard.h"
#include "move_panel.h"
#include "printer_state.h"
#include "temperature_panel.h"

#include <functional>
#include <string>
#include <vector>

namespace moonraker {
class MoonrakerSession;
struct RpcResponse;
}

namespace view::widgets {
class Toast;
}

namespace viewmodel {
class BaseViewModel;
}

namespace app {

class ThumbnailSource;

// Printer operations started from the pages. Reports the outcome of each one with a toast.
class PrinterCommands {
public:
    // error is empty on success.
    using FilesHandler = std::function<void(std::vector<GcodeFile> files, const std::string& error)>;
    // metadata is nullptr when the file has none.
    using MetadataHandler = std::function<void(const GcodeMetadata* metadata)>;
    // png is empty when the thumbnail could not be read.
    using ThumbnailHandler = std::function<void(std::string png)>;

    PrinterCommands(view::widgets::Toast& toast, viewmodel::BaseViewModel& view_model);

    PrinterCommands(const PrinterCommands&) = delete;
    PrinterCommands& operator=(const PrinterCommands&) = delete;

    // The session commands go to; nullptr while it is being replaced.
    void set_session(moonraker::MoonrakerSession* session);
    // Where thumbnails come from; nullptr while the session is being replaced.
    void set_thumbnail_source(ThumbnailSource* thumbnails);
    // Live printer state, or nullptr unless the session is ready.
    const model::PrinterState* printer() const;
    // Called after the session state changed; a Klipper restart drops a pending G-code state restore.
    void on_session_changed();

    void set_temperatures(const TemperatureRequest& request);
    // Refused while printing or while the nozzle is too cold. Like homing and jogging, also
    // refused until a failed move has restored the G-code state (see GcodeStateGuard).
    void move_extruder(const ExtruderRequest& request);

    // Axis moves. The page checks the block rules (move_block()) before calling these.
    void home_all();
    void home_axis(MoveAxis axis);
    void jog(const JogRequest& request);
    void motors_off();
    // A warning toast for an operation the page refused, e.g. move_block_reason().
    void show_warning(const std::string& message);

    // G-code files, newest first. Works while Klipper is not ready, as long as Moonraker is up.
    void list_files(FilesHandler handler);
    void file_metadata(const std::string& path, MetadataHandler handler);
    // path is relative to the gcodes root (see thumbnail_path()).
    void file_thumbnail(const std::string& path, ThumbnailHandler handler);
    // Refused while a print is running or paused. Shows the status page once it started.
    void start_print(const std::string& path);

    void pause_print();
    void resume_print();
    void cancel_print();
    // printer.emergency_stop: Klipper shuts down at once; FIRMWARE_RESTART recovers it.
    void emergency_stop();

private:
    // Shows a warning and returns false unless commands can be sent now.
    bool require_ready();
    // Shows a warning and returns false while GcodeStateGuard holds movement back; retries a
    // failed restore.
    bool require_gcode_state();
    // A script that saved its G-code state as state_name; restores it separately if it fails.
    void run_guarded_move(const std::string& script, const std::string& state_name,
                          const std::string& done_message);
    void send_restore(GcodeStateGuard::Ticket ticket);
    void run_script(const std::string& script, const std::string& done_message);
    // Reports the result of a print.* call with a toast.
    std::function<void(const moonraker::RpcResponse&)> report(std::string done_message);

    view::widgets::Toast& toast_;
    viewmodel::BaseViewModel& view_model_;
    moonraker::MoonrakerSession* session_{nullptr};
    ThumbnailSource* thumbnails_{nullptr};
    GcodeStateGuard gcode_state_;
};

} // namespace app

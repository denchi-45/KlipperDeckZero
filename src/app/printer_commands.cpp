/*
 * SPDX-License-Identifier: MIT
 */

#include "printer_commands.h"

#include "base_viewmodel.h"
#include "file_list_parser.h"
#include "gcode.h"
#include "logger.h"
#include "moonraker_session.h"
#include "thumbnail_source.h"
#include "toast.h"

namespace app {
namespace {

// Klipper errors often add a second line of advice; the toast has room for one line.
std::string first_line(const std::string& message) {
    return message.substr(0, message.find('\n'));
}

} // namespace

PrinterCommands::PrinterCommands(view::widgets::Toast& toast, viewmodel::BaseViewModel& view_model)
    : toast_(toast), view_model_(view_model) {}

void PrinterCommands::set_session(moonraker::MoonrakerSession* session) {
    session_ = session;
}

void PrinterCommands::set_thumbnail_source(ThumbnailSource* thumbnails) {
    thumbnails_ = thumbnails;
}

const model::PrinterState* PrinterCommands::printer() const {
    if (!session_ || session_->session_state() != moonraker::SessionState::Ready) {
        return nullptr;
    }
    return &session_->printer();
}

void PrinterCommands::set_temperatures(const TemperatureRequest& request) {
    const auto script = temperature_script(request);
    if (script.empty() || !require_ready()) {
        return;
    }
    run_script(script, "Target set: " + describe(request));
}

void PrinterCommands::move_extruder(const ExtruderRequest& request) {
    if (!require_ready()) {
        return;
    }
    if (const char* reason = extrude_block_reason(*printer())) {
        toast_.show(reason, view::widgets::ToastTone::Warning);
        return;
    }
    run_script(gcode::move_extruder(request.length_mm, request.speed_mm_s), describe(request));
}

void PrinterCommands::list_files(FilesHandler handler) {
    if (!session_) {
        handler({}, "Not connected");
        return;
    }
    session_->call("server.files.list", {{"root", "gcodes"}},
                   [handler = std::move(handler)](const moonraker::RpcResponse& response) {
                       if (!response.ok) {
                           handler({}, first_line(response.error.message));
                           return;
                       }
                       handler(parse_file_list(response.result), std::string());
                   });
}

void PrinterCommands::file_metadata(const std::string& path, MetadataHandler handler) {
    if (!session_) {
        handler(nullptr);
        return;
    }
    session_->call("server.files.metadata", {{"filename", path}},
                   [handler = std::move(handler)](const moonraker::RpcResponse& response) {
                       if (!response.ok) {
                           handler(nullptr);
                           return;
                       }
                       const auto metadata = parse_metadata(response.result);
                       handler(&metadata);
                   });
}

void PrinterCommands::file_thumbnail(const std::string& path, ThumbnailHandler handler) {
    if (!thumbnails_) {
        handler({});
        return;
    }
    thumbnails_->fetch(path, [path, handler = std::move(handler)](std::string png, const std::string& error) {
        if (!error.empty()) {
            LOG_WARN("thumbnail {}: {}", path, error);
        }
        handler(std::move(png));
    });
}

void PrinterCommands::start_print(const std::string& path) {
    if (!require_ready()) {
        return;
    }
    const auto state = printer()->print_state;
    if (state == model::PrintState::Printing || state == model::PrintState::Paused) {
        toast_.show("A print is already running", view::widgets::ToastTone::Warning);
        return;
    }
    LOG_INFO("print start: {}", path);
    session_->print_start(path, [this, path](const moonraker::RpcResponse& response) {
        if (!response.ok) {
            LOG_WARN("print start failed: {}", response.error.message);
            toast_.show("Failed: " + first_line(response.error.message), view::widgets::ToastTone::Error, 4000);
            return;
        }
        toast_.show("Print started", view::widgets::ToastTone::Success);
        view_model_.show_page(model::AppPage::Status);
    });
}

void PrinterCommands::pause_print() {
    if (require_ready()) {
        LOG_INFO("print pause");
        session_->print_pause(report("Print paused"));
    }
}

void PrinterCommands::resume_print() {
    if (require_ready()) {
        LOG_INFO("print resume");
        session_->print_resume(report("Print resumed"));
    }
}

void PrinterCommands::cancel_print() {
    if (require_ready()) {
        LOG_INFO("print cancel");
        session_->print_cancel(report("Print cancelled"));
    }
}

void PrinterCommands::emergency_stop() {
    // Sent whenever Moonraker is reachable: stopping must not wait for a Ready state.
    if (!session_) {
        toast_.show("Not connected", view::widgets::ToastTone::Warning);
        return;
    }
    LOG_WARN("emergency stop requested");
    session_->emergency_stop(report("Emergency stop sent"));
}

std::function<void(const moonraker::RpcResponse&)> PrinterCommands::report(std::string done_message) {
    return [this, done_message = std::move(done_message)](const moonraker::RpcResponse& response) {
        if (response.ok) {
            toast_.show(done_message, view::widgets::ToastTone::Success);
            return;
        }
        LOG_WARN("{} failed: {}", done_message, response.error.message);
        toast_.show("Failed: " + first_line(response.error.message), view::widgets::ToastTone::Error, 4000);
    };
}

bool PrinterCommands::require_ready() {
    if (printer()) {
        return true;
    }
    toast_.show("Printer is not ready", view::widgets::ToastTone::Warning);
    return false;
}

void PrinterCommands::run_script(const std::string& script, const std::string& done_message) {
    LOG_INFO("gcode: {}", script);
    session_->send_gcode(script, [this, done_message](const moonraker::RpcResponse& response) {
        if (response.ok) {
            toast_.show(done_message, view::widgets::ToastTone::Success);
            return;
        }
        LOG_WARN("gcode failed: {}", response.error.message);
        toast_.show("Failed: " + first_line(response.error.message), view::widgets::ToastTone::Error, 4000);
    });
}

} // namespace app

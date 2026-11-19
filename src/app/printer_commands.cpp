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

#include <cstdio>
#include <random>

namespace app {
namespace {

// Klipper errors often add a second line of advice; the toast has room for one line.
std::string first_line(const std::string& message) {
    return message.substr(0, message.find('\n'));
}

// Tells this run's saved G-code state names apart from those of an earlier run, which Klipper
// may still hold.
std::string make_run_id() {
    std::random_device random;
    char buffer[9];
    std::snprintf(buffer, sizeof(buffer), "%08x", static_cast<unsigned>(random()));
    return buffer;
}

} // namespace

PrinterCommands::PrinterCommands(view::widgets::Toast& toast, viewmodel::BaseViewModel& view_model)
    : toast_(toast), view_model_(view_model), gcode_state_(make_run_id()) {}

void PrinterCommands::set_session(moonraker::MoonrakerSession* session) {
    session_ = session;
    gcode_state_.reset();
}

void PrinterCommands::set_thumbnail_source(ThumbnailSource* thumbnails) {
    thumbnails_ = thumbnails;
}

void PrinterCommands::set_webcam_source(WebcamSource* webcams) {
    webcams_ = webcams;
}

WebcamSource* PrinterCommands::webcams() const {
    return webcams_;
}

const model::PrinterState* PrinterCommands::printer() const {
    if (!session_ || session_->session_state() != moonraker::SessionState::Ready) {
        return nullptr;
    }
    return &session_->printer();
}

void PrinterCommands::on_session_changed() {
    if (!session_) {
        return;
    }
    // Leaving these states takes a Klipper restart, which resets the G-code state (and forgets
    // the saved one). A plain Moonraker reconnect keeps Klipper running, so it keeps the guard.
    const auto state = session_->session_state();
    if (state == moonraker::SessionState::WaitingKlippy || state == moonraker::SessionState::KlippyError) {
        gcode_state_.reset();
    }
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
    if (require_gcode_state()) {
        run_guarded_move(gcode::kExtruderState,
                         [&request](const std::string& name) {
                             return gcode::move_extruder(request.length_mm, request.speed_mm_s, name);
                         },
                         describe(request));
    }
}

void PrinterCommands::home_all() {
    if (require_ready() && require_gcode_state()) {
        run_script(gcode::home_axes(""), "Homed all axes");
    }
}

void PrinterCommands::home_axis(MoveAxis axis) {
    if (require_ready() && require_gcode_state()) {
        const std::string letter(1, axis_letter(axis));
        run_script(gcode::home_axes(letter), "Homed " + letter);
    }
}

void PrinterCommands::jog(const JogRequest& request) {
    if (require_ready() && require_gcode_state()) {
        run_guarded_move(gcode::kJogState,
                         [&request](const std::string& name) {
                             return gcode::jog_axis(axis_letter(request.axis), request.distance_mm,
                                                    request.feed_mm_min, name);
                         },
                         describe(request));
    }
}

void PrinterCommands::motors_off() {
    if (require_ready()) {
        run_script(gcode::motors_off(), "Motors off");
    }
}

void PrinterCommands::show_warning(const std::string& message) {
    toast_.show(message, view::widgets::ToastTone::Warning);
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
    if (!require_gcode_state()) {
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
    if (require_ready() && require_gcode_state()) {
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

bool PrinterCommands::require_gcode_state() {
    const char* reason = gcode_state_.block_reason();
    if (!reason) {
        return true;
    }
    toast_.show(reason, view::widgets::ToastTone::Warning);
    if (gcode_state_.state() == GcodeStateGuard::State::RestoreFailed) {
        send_restore(gcode_state_.begin_restore());
    }
    return false;
}

void PrinterCommands::run_guarded_move(const std::string& state_prefix, const ScriptBuilder& build_script,
                                       const std::string& done_message) {
    const auto ticket = gcode_state_.begin_move(state_prefix);
    const auto script = build_script(gcode_state_.state_name());
    LOG_INFO("gcode: {}", script);
    session_->send_gcode(script, [this, ticket, done_message](const moonraker::RpcResponse& response) {
        // A reset (session replaced, Klipper restarted) dropped this move: no toast on the
        // screen of whatever came after it.
        if (!gcode_state_.is_current(ticket)) {
            LOG_INFO("gcode response after reset ignored: {}", response.ok ? "ok" : response.error.message);
            return;
        }
        const bool restore = gcode_state_.move_done(ticket, response.ok);
        if (response.ok) {
            toast_.show(done_message, view::widgets::ToastTone::Success);
            return;
        }
        LOG_WARN("gcode failed: {}", response.error.message);
        toast_.show("Failed: " + first_line(response.error.message), view::widgets::ToastTone::Error, 4000);
        if (restore) {
            send_restore(ticket);
        }
    });
}

void PrinterCommands::send_restore(GcodeStateGuard::Ticket ticket) {
    const auto script = gcode::restore_gcode_state(gcode_state_.state_name());
    LOG_INFO("gcode: {}", script);
    session_->send_gcode(script, [this, ticket](const moonraker::RpcResponse& response) {
        if (!gcode_state_.is_current(ticket)) {
            LOG_INFO("gcode state restore response after reset ignored");
            return;
        }
        // Without a saved state the printer is not in the move's relative mode: done as well.
        // Timeouts and disconnects stay failures, the move may have run.
        const bool nothing_saved = !response.ok && gcode_state_.nothing_to_restore(response.error.message);
        gcode_state_.restore_done(ticket, response.ok || nothing_saved);
        if (nothing_saved) {
            LOG_INFO("gcode state restore: nothing saved ({})", response.error.message);
        }
        else if (!response.ok) {
            LOG_WARN("gcode state restore failed: {}", response.error.message);
            if (gcode_state_.state() == GcodeStateGuard::State::RestoreFailed) {
                toast_.show("G-code state not restored: " + first_line(response.error.message),
                            view::widgets::ToastTone::Warning, 4000);
            }
        }
    });
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

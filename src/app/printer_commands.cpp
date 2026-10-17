/*
 * SPDX-License-Identifier: MIT
 */

#include "printer_commands.h"

#include "logger.h"
#include "moonraker_session.h"
#include "toast.h"

namespace app {
namespace {

// Klipper errors often add a second line of advice; the toast has room for one line.
std::string first_line(const std::string& message) {
    return message.substr(0, message.find('\n'));
}

} // namespace

PrinterCommands::PrinterCommands(view::widgets::Toast& toast) : toast_(toast) {}

void PrinterCommands::set_session(moonraker::MoonrakerSession* session) {
    session_ = session;
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

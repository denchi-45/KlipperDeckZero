/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "application_config.h"
#include "moonraker_session.h"

#include "lvgl.h"

#include <functional>
#include <memory>

namespace app {

// Owns the Moonraker session and drives it from an LVGL timer, so all session
// callbacks run on the LVGL thread.
class PrinterService {
public:
    using ChangeListener = std::function<void(const moonraker::MoonrakerSession& session)>;

    explicit PrinterService(const MoonrakerConfig& config);
    ~PrinterService();

    PrinterService(const PrinterService&) = delete;
    PrinterService& operator=(const PrinterService&) = delete;

    void start();
    void stop();

    moonraker::MoonrakerSession& session();
    void set_change_listener(ChangeListener listener);

private:
    static void poll_timer_cb(lv_timer_t* timer);
    void on_session_changed();

    std::unique_ptr<moonraker::MoonrakerSession> session_;
    lv_timer_t* poll_timer_{nullptr};
    ChangeListener change_listener_;
    moonraker::SessionState logged_state_{moonraker::SessionState::Stopped};
    model::KlippyState logged_klippy_{model::KlippyState::Unknown};
    model::PrintState logged_print_{model::PrintState::Unknown};
};

} // namespace app

/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "moonraker_session.h"
#include "printer_state.h"

#include <cstdint>

namespace app {

enum class RestartKind {
    Klipper,  // RESTART
    Firmware, // FIRMWARE_RESTART
};

// True when a restart would abort a running or paused print.
// Only a Ready session has a live print state: after a Klippy shutdown the last
// state can still read Printing, and restarting is then the way to recover.
bool restart_would_abort_print(moonraker::SessionState session, model::PrintState print);

// Two-press confirmation for the restart shortcuts, independent of LVGL.
// The first press arms a kind; pressing the same key again within the window confirms it.
class RestartConfirm {
public:
    enum class Result {
        Armed,     // ask the user to press again
        Confirmed, // send the restart
        Blocked,   // refused: a restart would abort the running print
    };

    static constexpr uint32_t kWindowMs = 3000;

    Result press(RestartKind kind, bool printing, uint32_t now_ms);
    void cancel();

private:
    bool armed_{false};
    RestartKind kind_{RestartKind::Klipper};
    uint32_t armed_at_{0};
};

} // namespace app

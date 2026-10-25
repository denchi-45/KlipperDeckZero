/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <string>

namespace app {

// Keeps movement commands back while a SAVE/RESTORE_GCODE_STATE move may have left the printer
// in relative mode, independent of LVGL. PrinterCommands sends the scripts.
//
// A guarded move (jog_axis(), move_extruder()) ends with RESTORE_GCODE_STATE, but Klipper stops
// a script at its first error, so a failed G1 leaves G91 / M83 behind. The guard then asks for
// the restore as a separate script and refuses movement until it succeeded. A failed restore
// keeps movement refused; the next movement attempt retries it.
class GcodeStateGuard {
public:
    enum class State {
        Idle,
        Moving,        // a guarded move is in flight
        Restoring,     // the separate restore is in flight
        RestoreFailed, // the printer may still be in relative mode
    };

    // Identifies the request a response belongs to; responses of an older ticket are ignored.
    using Ticket = std::uint32_t;

    State state() const;
    // Toast text while movement is refused, or nullptr when it may run.
    const char* block_reason() const;
    // The saved state the pending or failed restore is for, e.g. "kdz_jog".
    const std::string& state_name() const;

    // Starts a guarded move that saved its state as state_name. Only valid while Idle.
    Ticket begin_move(const std::string& state_name);
    // The move script finished. Returns true when the caller has to send the restore now; it
    // then calls restore_done() with the same ticket.
    bool move_done(Ticket ticket, bool ok);
    // Retries the restore after RestoreFailed. Only valid in that state.
    Ticket begin_restore();
    void restore_done(Ticket ticket, bool ok);

    // Klipper restarted (its G-code state is back to the defaults) or the session was replaced:
    // forgets everything and ignores responses that are still on the way.
    void reset();

private:
    State state_{State::Idle};
    std::string state_name_;
    Ticket ticket_{0};
};

} // namespace app

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
// keeps movement refused; the next movement attempt retries it. A restore that finds no saved
// state (see nothing_to_restore()) counts as done.
//
// Klipper keeps a saved state after RESTORE_GCODE_STATE, so every move saves under its own name
// (begin_move()): a restore for a move that never reached Klipper then finds nothing instead of
// an older move's state. run_id tells apart the names of different app runs.
//
// Printing must not start or resume while a separate restore may still be on the way, or the
// restore would undo the print's settings (see block_reason()).
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

    explicit GcodeStateGuard(std::string run_id = std::string());

    State state() const;
    // Toast text while movement, or starting and resuming a print, is refused; nullptr when it
    // may run.
    const char* block_reason() const;
    // The saved state of the current move, and of the pending or failed restore, e.g.
    // "kdz_jog_1f2e3d4c_7".
    const std::string& state_name() const;
    // False for a response to a request from before a reset() or a newer request.
    bool is_current(Ticket ticket) const;

    // Starts a guarded move. Only valid while Idle. The move saves its state under state_name(),
    // a name made from prefix (e.g. "kdz_jog") that no earlier move used.
    Ticket begin_move(const std::string& prefix);
    // The move script finished. Returns true when the caller has to send the restore now; it
    // then calls restore_done() with the same ticket.
    bool move_done(Ticket ticket, bool ok);
    // Retries the restore after RestoreFailed. Only valid in that state.
    Ticket begin_restore();
    void restore_done(Ticket ticket, bool ok);
    // True when a failed restore's error says Klipper holds no saved state under state_name()
    // (the move never ran SAVE_GCODE_STATE, or Klipper restarted): nothing is left to restore.
    bool nothing_to_restore(const std::string& error_message) const;

    // Klipper restarted (its G-code state is back to the defaults) or the session was replaced:
    // forgets everything and ignores responses that are still on the way.
    void reset();

private:
    std::string run_id_;
    State state_{State::Idle};
    std::string state_name_;
    Ticket ticket_{0};
};

} // namespace app

/*
 * SPDX-License-Identifier: MIT
 */

#include "gcode_state_guard.h"
#include "test_framework.h"

#include <string>

namespace {

using app::GcodeStateGuard;
using State = GcodeStateGuard::State;

} // namespace

TEST_CASE("gcode state guard: a successful move needs no separate restore") {
    GcodeStateGuard guard;
    CHECK(guard.state() == State::Idle);
    CHECK(guard.block_reason() == nullptr);

    const auto ticket = guard.begin_move("kdz_jog");
    // Movement waits for the move in flight.
    CHECK(guard.state() == State::Moving);
    CHECK(guard.block_reason() != nullptr);

    CHECK(!guard.move_done(ticket, true));
    CHECK(guard.state() == State::Idle);
    CHECK(guard.block_reason() == nullptr);
}

TEST_CASE("gcode state guard: a failed move blocks movement until the restore succeeds") {
    GcodeStateGuard guard;
    const auto ticket = guard.begin_move("kdz_jog");
    CHECK(guard.move_done(ticket, false));
    CHECK(guard.state() == State::Restoring);
    CHECK_EQ(guard.state_name(), std::string("kdz_jog_1"));
    CHECK(guard.block_reason() != nullptr);

    guard.restore_done(ticket, true);
    CHECK(guard.state() == State::Idle);
    CHECK(guard.block_reason() == nullptr);
}

TEST_CASE("gcode state guard: a failed restore keeps movement blocked until a retry succeeds") {
    GcodeStateGuard guard;
    const auto ticket = guard.begin_move("kdz_extruder");
    CHECK(guard.move_done(ticket, false));
    guard.restore_done(ticket, false);
    CHECK(guard.state() == State::RestoreFailed);
    CHECK(guard.block_reason() != nullptr);

    const auto retry = guard.begin_restore();
    CHECK(guard.state() == State::Restoring);
    // The retry restores the failed move's state.
    CHECK_EQ(guard.state_name(), std::string("kdz_extruder_1"));
    guard.restore_done(retry, false);
    CHECK(guard.state() == State::RestoreFailed);

    const auto again = guard.begin_restore();
    guard.restore_done(again, true);
    CHECK(guard.state() == State::Idle);
}

TEST_CASE("gcode state guard: responses from before a reset are ignored") {
    GcodeStateGuard guard;
    const auto ticket = guard.begin_move("kdz_jog");
    // Klipper restarts while the move is in flight.
    guard.reset();
    CHECK(guard.state() == State::Idle);
    CHECK(!guard.move_done(ticket, false));
    CHECK(guard.state() == State::Idle);

    // A restore answered after a reset does not block the next move either.
    const auto move = guard.begin_move("kdz_jog");
    CHECK(guard.move_done(move, false));
    guard.reset();
    guard.restore_done(move, false);
    CHECK(guard.state() == State::Idle);

    // An old retry answering during a newer move leaves that move alone.
    const auto failed = guard.begin_move("kdz_jog");
    guard.move_done(failed, false);
    guard.restore_done(failed, false);
    const auto retry = guard.begin_restore();
    guard.reset();
    const auto next = guard.begin_move("kdz_jog");
    guard.restore_done(retry, true);
    CHECK(guard.state() == State::Moving);
    CHECK(!guard.move_done(next, true));
    CHECK(guard.state() == State::Idle);
}

TEST_CASE("gcode state guard: an unknown-state error for our name means nothing to restore") {
    GcodeStateGuard guard;
    const auto ticket = guard.begin_move("kdz_jog");
    // The move never reached Klipper, and neither did the first restore.
    CHECK(guard.move_done(ticket, false));
    CHECK(!guard.nothing_to_restore("not connected: printer.gcode.script"));
    guard.restore_done(ticket, false);
    CHECK(guard.state() == State::RestoreFailed);

    // After the reconnect the retry finds no saved state under the move's name.
    const auto retry = guard.begin_restore();
    const std::string error = "Unknown g-code state: kdz_jog_1";
    CHECK(guard.nothing_to_restore(error));
    guard.restore_done(retry, guard.nothing_to_restore(error));
    CHECK(guard.state() == State::Idle);
    CHECK(guard.block_reason() == nullptr);
}

TEST_CASE("gcode state guard: an unknown-state error for another name keeps movement blocked") {
    GcodeStateGuard guard;
    const auto ticket = guard.begin_move("kdz_jog");
    CHECK(guard.move_done(ticket, false));
    CHECK(!guard.nothing_to_restore("Unknown g-code state: kdz_extruder_1"));
    CHECK(!guard.nothing_to_restore("Unknown g-code state: kdz_jog_12"));
    CHECK(!guard.nothing_to_restore("Unknown g-code state: kdz_jog"));
    CHECK(!guard.nothing_to_restore("Unknown g-code state: default"));
    guard.restore_done(ticket, guard.nothing_to_restore("Unknown g-code state: kdz_extruder_1"));
    CHECK(guard.state() == State::RestoreFailed);
}

TEST_CASE("gcode state guard: other restore errors keep RestoreFailed") {
    GcodeStateGuard guard;
    const auto ticket = guard.begin_move("kdz_extruder");
    CHECK(guard.move_done(ticket, false));
    for (const std::string error : {"request timed out: printer.gcode.script", "send failed: printer.gcode.script",
                                    "Must home axis first", ""}) {
        CHECK(!guard.nothing_to_restore(error));
    }
    guard.restore_done(ticket, guard.nothing_to_restore("Must home axis first"));
    CHECK(guard.state() == State::RestoreFailed);
    CHECK(guard.block_reason() != nullptr);
}

TEST_CASE("gcode state guard: every move saves under a name no earlier move used") {
    GcodeStateGuard guard("1f2e3d4c");
    const auto first = guard.begin_move("kdz_jog");
    const std::string first_name = guard.state_name();
    CHECK_EQ(first_name, std::string("kdz_jog_1f2e3d4c_1"));
    CHECK(!guard.move_done(first, true));

    // Klipper still holds first_name (RESTORE_GCODE_STATE keeps it). The next jog never reaches
    // Klipper, so its restore has to miss instead of bringing back the first jog's state.
    const auto second = guard.begin_move("kdz_jog");
    CHECK(guard.state_name() != first_name);
    CHECK(guard.move_done(second, false));
    CHECK(!guard.nothing_to_restore("Unknown g-code state: " + first_name));
    CHECK(guard.nothing_to_restore("Unknown g-code state: " + guard.state_name()));

    // Names stay fresh across a reset, and another run never shares them.
    guard.reset();
    guard.begin_move("kdz_jog");
    CHECK_EQ(guard.state_name(), std::string("kdz_jog_1f2e3d4c_4"));
    GcodeStateGuard other_run("99aa00bb");
    other_run.begin_move("kdz_jog");
    CHECK(other_run.state_name() != first_name);
}

TEST_CASE("gcode state guard: print start and resume wait until a failed move is restored") {
    // PrinterCommands refuses start_print() / resume_print() while block_reason() is set: a
    // separate restore sent after RESUME would undo what the print changed.
    GcodeStateGuard guard;
    const auto ticket = guard.begin_move("kdz_jog");
    CHECK(guard.block_reason() != nullptr);
    CHECK(guard.move_done(ticket, false));
    CHECK(guard.block_reason() != nullptr);
    guard.restore_done(ticket, false);
    CHECK(guard.block_reason() != nullptr);
    const auto retry = guard.begin_restore();
    guard.restore_done(retry, true);
    CHECK(guard.block_reason() == nullptr);
}

TEST_CASE("gcode state guard: responses from before a reset are not current") {
    GcodeStateGuard guard;
    const auto ticket = guard.begin_move("kdz_jog");
    CHECK(guard.is_current(ticket));
    // The session is replaced while the jog is in flight; its answer must not show a toast.
    guard.reset();
    CHECK(!guard.is_current(ticket));
    CHECK(!guard.move_done(ticket, true));

    // A retry supersedes the failed move's ticket.
    const auto move = guard.begin_move("kdz_jog");
    guard.move_done(move, false);
    guard.restore_done(move, false);
    const auto retry = guard.begin_restore();
    CHECK(!guard.is_current(move));
    CHECK(guard.is_current(retry));
}

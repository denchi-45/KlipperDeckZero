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
    CHECK_EQ(guard.state_name(), std::string("kdz_jog"));
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
    CHECK_EQ(guard.state_name(), std::string("kdz_extruder"));
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

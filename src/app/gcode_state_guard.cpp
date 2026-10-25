/*
 * SPDX-License-Identifier: MIT
 */

#include "gcode_state_guard.h"

#include <cctype>

namespace app {

GcodeStateGuard::State GcodeStateGuard::state() const {
    return state_;
}

const char* GcodeStateGuard::block_reason() const {
    switch (state_) {
        case State::Moving:
            return "Wait for the last move";
        case State::Restoring:
            return "Restoring G-code state";
        case State::RestoreFailed:
            return "G-code state not restored, retrying";
        case State::Idle:
            break;
    }
    return nullptr;
}

const std::string& GcodeStateGuard::state_name() const {
    return state_name_;
}

GcodeStateGuard::Ticket GcodeStateGuard::begin_move(const std::string& state_name) {
    state_ = State::Moving;
    state_name_ = state_name;
    return ++ticket_;
}

bool GcodeStateGuard::move_done(Ticket ticket, bool ok) {
    if (ticket != ticket_ || state_ != State::Moving) {
        return false;
    }
    if (ok) {
        // The RESTORE at the end of the script ran.
        state_ = State::Idle;
        return false;
    }
    state_ = State::Restoring;
    return true;
}

GcodeStateGuard::Ticket GcodeStateGuard::begin_restore() {
    state_ = State::Restoring;
    return ++ticket_;
}

void GcodeStateGuard::restore_done(Ticket ticket, bool ok) {
    if (ticket != ticket_ || state_ != State::Restoring) {
        return;
    }
    state_ = ok ? State::Idle : State::RestoreFailed;
}

bool GcodeStateGuard::nothing_to_restore(const std::string& error_message) const {
    if (state_name_.empty()) {
        return false;
    }
    // Klipper's RESTORE_GCODE_STATE: "Unknown g-code state: <name>". The name has to end there,
    // so an error for "kdz_jog2" is not taken for "kdz_jog".
    const std::string expected = "Unknown g-code state: " + state_name_;
    const auto pos = error_message.find(expected);
    if (pos == std::string::npos) {
        return false;
    }
    const auto end = pos + expected.size();
    if (end == error_message.size()) {
        return true;
    }
    const char next = error_message[end];
    return !std::isalnum(static_cast<unsigned char>(next)) && next != '_';
}

void GcodeStateGuard::reset() {
    state_ = State::Idle;
    state_name_.clear();
    ++ticket_;
}

} // namespace app

/*
 * SPDX-License-Identifier: MIT
 */

#include "restart_confirm.h"
#include "test_framework.h"

namespace {

using app::RestartConfirm;
using app::RestartKind;
using Result = RestartConfirm::Result;

} // namespace

TEST_CASE("restart confirm: second press within the window confirms") {
    RestartConfirm confirm;
    CHECK(confirm.press(RestartKind::Klipper, false, 1000) == Result::Armed);
    CHECK(confirm.press(RestartKind::Klipper, false, 1000 + RestartConfirm::kWindowMs) == Result::Confirmed);
    // Confirming disarms, so a third press starts over.
    CHECK(confirm.press(RestartKind::Klipper, false, 5000) == Result::Armed);
}

TEST_CASE("restart confirm: a late second press only re-arms") {
    RestartConfirm confirm;
    CHECK(confirm.press(RestartKind::Firmware, false, 0) == Result::Armed);
    CHECK(confirm.press(RestartKind::Firmware, false, RestartConfirm::kWindowMs + 1) == Result::Armed);
    CHECK(confirm.press(RestartKind::Firmware, false, RestartConfirm::kWindowMs + 2) == Result::Confirmed);
}

TEST_CASE("restart confirm: the other kind does not confirm") {
    RestartConfirm confirm;
    CHECK(confirm.press(RestartKind::Klipper, false, 0) == Result::Armed);
    CHECK(confirm.press(RestartKind::Firmware, false, 100) == Result::Armed);
    CHECK(confirm.press(RestartKind::Firmware, false, 200) == Result::Confirmed);
}

TEST_CASE("restart confirm: cancel and printing disarm") {
    RestartConfirm confirm;
    CHECK(confirm.press(RestartKind::Klipper, false, 0) == Result::Armed);
    confirm.cancel();
    CHECK(confirm.press(RestartKind::Klipper, false, 100) == Result::Armed);

    CHECK(confirm.press(RestartKind::Klipper, true, 200) == Result::Blocked);
    CHECK(confirm.press(RestartKind::Klipper, false, 300) == Result::Armed);
}

TEST_CASE("restart confirm: survives tick wraparound") {
    RestartConfirm confirm;
    CHECK(confirm.press(RestartKind::Klipper, false, 0xFFFFFF00u) == Result::Armed);
    CHECK(confirm.press(RestartKind::Klipper, false, 0x00000100u) == Result::Confirmed);
}

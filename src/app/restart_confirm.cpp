/*
 * SPDX-License-Identifier: MIT
 */

#include "restart_confirm.h"

namespace app {

RestartConfirm::Result RestartConfirm::press(RestartKind kind, bool printing, uint32_t now_ms) {
    if (printing) {
        cancel();
        return Result::Blocked;
    }

    // Unsigned subtraction keeps working when the millisecond tick wraps.
    if (armed_ && kind_ == kind && now_ms - armed_at_ <= kWindowMs) {
        cancel();
        return Result::Confirmed;
    }

    armed_ = true;
    kind_ = kind;
    armed_at_ = now_ms;
    return Result::Armed;
}

void RestartConfirm::cancel() {
    armed_ = false;
}

} // namespace app

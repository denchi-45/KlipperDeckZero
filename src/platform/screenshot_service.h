/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 denchi-45
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <string>

namespace platform::screenshot {

struct CaptureResult {
    bool success{false};
    std::string path;
    std::string error;
};

CaptureResult capture_active_screen();

} // namespace platform::screenshot

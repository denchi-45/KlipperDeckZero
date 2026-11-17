/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "panel_input.h"

#include <cstdint>

namespace screen {

// Maps an LVGL key to the input the page panels use; false for keys they don't handle.
bool to_panel_input(uint32_t key, app::PanelInput& input);

} // namespace screen

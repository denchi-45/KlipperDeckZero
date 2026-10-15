/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "printer_state.h"

#include <nlohmann/json.hpp>

namespace moonraker {

// Printer objects/fields requested with printer.objects.subscribe.
nlohmann::json subscription_objects();

// Merges a status object ({"extruder": {"temperature": ...}, ...}) into state.
// Only fields present in the update are changed, matching notify_status_update diffs.
// Returns true if anything changed.
bool apply_status(model::PrinterState& state, const nlohmann::json& status);

} // namespace moonraker

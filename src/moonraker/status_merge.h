/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "printer_state.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace moonraker {

// Printer objects/fields requested with printer.objects.subscribe, plus the temperature of each
// extra sensor object.
nlohmann::json subscription_objects(const std::vector<std::string>& sensor_objects = {});

// Sensor objects to watch besides the nozzle and bed, from the `heaters` status
// ({"available_sensors": [...]}); empty when it has none.
std::vector<std::string> extra_sensor_objects(const nlohmann::json& heaters);
// Replaces state.sensors with one unread entry per object, named after the part after the
// object type ("temperature_sensor Micro4" -> "Micro4").
void init_sensors(model::PrinterState& state, const std::vector<std::string>& sensor_objects);

// Merges a status object ({"extruder": {"temperature": ...}, ...}) into state.
// Only fields present in the update are changed, matching notify_status_update diffs.
// Returns true if anything changed.
bool apply_status(model::PrinterState& state, const nlohmann::json& status);

} // namespace moonraker

/*
 * SPDX-License-Identifier: MIT
 */

#include "status_merge.h"
#include "test_framework.h"

using nlohmann::json;

TEST_CASE("apply_status merges a full snapshot") {
    model::PrinterState state;
    const auto status = json::parse(R"({
        "webhooks": {"state": "ready", "state_message": "Printer is ready"},
        "print_stats": {"state": "printing", "filename": "benchy.gcode", "print_duration": 120.5,
                        "total_duration": 130.0, "filament_used": 512.0, "message": "",
                        "info": {"current_layer": 3, "total_layer": 120}},
        "virtual_sdcard": {"progress": 0.25, "is_active": true},
        "display_status": {"progress": 0.0, "message": null},
        "extruder": {"temperature": 209.8, "target": 210.0, "power": 0.45, "can_extrude": true},
        "heater_bed": {"temperature": 59.9, "target": 60.0, "power": 0.2},
        "toolhead": {"position": [10.0, 20.0, 0.4, 100.0], "homed_axes": "xyz"},
        "gcode_move": {"speed_factor": 1.5, "extrude_factor": 0.95},
        "fan": {"speed": 1.0},
        "idle_timeout": {"state": "Printing"}
    })");

    CHECK(moonraker::apply_status(state, status));
    CHECK(state.klippy_state == model::KlippyState::Ready);
    CHECK(state.print_state == model::PrintState::Printing);
    CHECK_EQ(state.filename, std::string("benchy.gcode"));
    CHECK_NEAR(state.print_duration, 120.5, 1e-9);
    CHECK_EQ(state.current_layer, 3);
    CHECK_EQ(state.total_layer, 120);
    CHECK_NEAR(state.extruder.temperature, 209.8, 1e-9);
    CHECK(state.extruder.can_extrude);
    CHECK(state.has_heater_bed);
    CHECK_NEAR(state.heater_bed.target, 60.0, 1e-9);
    CHECK_NEAR(state.position[2], 0.4, 1e-9);
    CHECK_EQ(state.homed_axes, std::string("xyz"));
    CHECK_NEAR(state.speed_factor, 1.5, 1e-9);
    CHECK_EQ(state.idle_state, std::string("Printing"));
    CHECK_NEAR(state.progress(), 0.25, 1e-9);
    CHECK_NEAR(state.estimated_remaining(), 361.5, 1e-6);
}

TEST_CASE("apply_status applies partial diffs and reports changes") {
    model::PrinterState state;
    state.extruder.target = 200.0;
    state.extruder.temperature = 25.0;

    CHECK(moonraker::apply_status(state, json::parse(R"({"extruder": {"temperature": 30.0}})")));
    CHECK_NEAR(state.extruder.temperature, 30.0, 1e-9);
    CHECK_NEAR(state.extruder.target, 200.0, 1e-9);

    // Same value again: no change.
    CHECK(!moonraker::apply_status(state, json::parse(R"({"extruder": {"temperature": 30.0}})")));
    // Unknown objects are ignored.
    CHECK(!moonraker::apply_status(
        state, json::parse(R"({"temperature_sensor chamber": {"temperature": 30.0}})")));
}

TEST_CASE("apply_status handles null values") {
    model::PrinterState state;
    CHECK(moonraker::apply_status(state, json::parse(R"({
        "print_stats": {"info": {"current_layer": 5, "total_layer": 10}},
        "heater_bed": {"temperature": 20.0}
    })")));
    CHECK(state.has_heater_bed);

    CHECK(moonraker::apply_status(state, json::parse(R"({
        "print_stats": {"info": {"current_layer": null, "total_layer": null}},
        "heater_bed": null
    })")));
    CHECK_EQ(state.current_layer, -1);
    CHECK_EQ(state.total_layer, -1);
    CHECK(!state.has_heater_bed);
}

TEST_CASE("display_status progress takes precedence over file progress") {
    model::PrinterState state;
    state.sdcard_progress = 0.10;
    state.display_progress = 0.30;
    CHECK_NEAR(state.progress(), 0.30, 1e-9);
    state.display_progress = 0.0;
    CHECK_NEAR(state.progress(), 0.10, 1e-9);
    state.print_duration = 0.0;
    CHECK(state.estimated_remaining() < 0.0);
}

TEST_CASE("extra sensors skip the nozzle and bed and are named after the object") {
    const auto objects = moonraker::extra_sensor_objects(json::parse(R"({
        "available_sensors": ["temperature_sensor Micro4", "extruder", "heater_bed", "extruder1"]
    })"));
    CHECK_EQ(objects.size(), std::size_t{2});
    CHECK_EQ(objects[0], std::string("temperature_sensor Micro4"));
    CHECK(moonraker::extra_sensor_objects(json()).empty());

    model::PrinterState state;
    moonraker::init_sensors(state, objects);
    CHECK_EQ(state.sensors[0].name, std::string("Micro4"));
    CHECK_EQ(state.sensors[1].name, std::string("extruder1"));
    CHECK(moonraker::subscription_objects(objects).contains("extruder1"));

    // A null reading leaves the sensor unknown.
    CHECK(!moonraker::apply_status(state, json::parse(R"({"extruder1": {"temperature": null}})")));
    CHECK(!state.sensors[1].known);
}

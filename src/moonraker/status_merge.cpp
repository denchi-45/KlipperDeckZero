/*
 * SPDX-License-Identifier: MIT
 */

#include "status_merge.h"

#include <string>

namespace moonraker {
namespace {

using Json = nlohmann::json;

class Merger {
public:
    explicit Merger(const Json& object) : object_(object) {}

    void number(const char* key, double& target) {
        const auto it = object_.find(key);
        if (it != object_.end() && it->is_number()) {
            assign(target, it->get<double>());
        }
    }

    void integer(const char* key, int& target) {
        const auto it = object_.find(key);
        if (it == object_.end()) {
            return;
        }
        if (it->is_number()) {
            assign(target, it->get<int>());
        }
        else if (it->is_null()) {
            assign(target, -1);
        }
    }

    void boolean(const char* key, bool& target) {
        const auto it = object_.find(key);
        if (it != object_.end() && it->is_boolean()) {
            assign(target, it->get<bool>());
        }
    }

    void string(const char* key, std::string& target) {
        const auto it = object_.find(key);
        if (it == object_.end()) {
            return;
        }
        if (it->is_string()) {
            assign(target, it->get<std::string>());
        }
        else if (it->is_null()) {
            assign(target, std::string());
        }
    }

    template <typename Enum, typename Parser>
    void enumeration(const char* key, Enum& target, Parser parse) {
        const auto it = object_.find(key);
        if (it != object_.end() && it->is_string()) {
            assign(target, parse(it->get<std::string>()));
        }
    }

    bool changed() const {
        return changed_;
    }

private:
    template <typename T>
    void assign(T& target, const T& value) {
        if (!(target == value)) {
            target = value;
            changed_ = true;
        }
    }

    const Json& object_;
    bool changed_{false};
};

const Json* find_object(const Json& status, const char* name) {
    const auto it = status.find(name);
    return it != status.end() && it->is_object() ? &*it : nullptr;
}

void merge_heater(Merger& merger, model::HeaterState& heater) {
    merger.number("temperature", heater.temperature);
    merger.number("target", heater.target);
    merger.number("power", heater.power);
}

} // namespace

Json subscription_objects(const std::vector<std::string>& sensor_objects) {
    Json objects = {
        {"webhooks", {"state", "state_message"}},
        {"print_stats",
         {"state", "filename", "print_duration", "total_duration", "filament_used", "message", "info"}},
        {"virtual_sdcard", {"progress", "is_active"}},
        {"display_status", {"progress", "message"}},
        {"extruder", {"temperature", "target", "power", "can_extrude"}},
        {"heater_bed", {"temperature", "target", "power"}},
        {"toolhead", {"position", "homed_axes"}},
        {"gcode_move", {"speed_factor", "extrude_factor"}},
        {"fan", {"speed"}},
        {"idle_timeout", {"state"}},
    };
    for (const auto& object : sensor_objects) {
        objects[object] = {"temperature"};
    }
    return objects;
}

std::vector<std::string> extra_sensor_objects(const Json& heaters) {
    std::vector<std::string> objects;
    const auto it = heaters.is_object() ? heaters.find("available_sensors") : heaters.end();
    if (it == heaters.end() || !it->is_array()) {
        return objects;
    }
    for (const auto& entry : *it) {
        if (!entry.is_string()) {
            continue;
        }
        auto object = entry.get<std::string>();
        // The nozzle and bed have their own rows.
        if (object != "extruder" && object != "heater_bed") {
            objects.push_back(std::move(object));
        }
    }
    return objects;
}

void init_sensors(model::PrinterState& state, const std::vector<std::string>& sensor_objects) {
    state.sensors.clear();
    for (const auto& object : sensor_objects) {
        model::SensorState sensor;
        sensor.object = object;
        const auto space = object.find(' ');
        sensor.name = space == std::string::npos ? object : object.substr(space + 1);
        state.sensors.push_back(std::move(sensor));
    }
}

bool apply_status(model::PrinterState& state, const Json& status) {
    if (!status.is_object()) {
        return false;
    }

    bool changed = false;

    if (const auto* object = find_object(status, "webhooks")) {
        Merger merger(*object);
        merger.enumeration("state", state.klippy_state, model::parse_klippy_state);
        merger.string("state_message", state.state_message);
        changed |= merger.changed();
    }

    if (const auto* object = find_object(status, "print_stats")) {
        Merger merger(*object);
        merger.enumeration("state", state.print_state, model::parse_print_state);
        merger.string("filename", state.filename);
        merger.number("print_duration", state.print_duration);
        merger.number("total_duration", state.total_duration);
        merger.number("filament_used", state.filament_used);
        merger.string("message", state.print_message);
        changed |= merger.changed();

        if (const auto* info = find_object(*object, "info")) {
            Merger info_merger(*info);
            info_merger.integer("current_layer", state.current_layer);
            info_merger.integer("total_layer", state.total_layer);
            changed |= info_merger.changed();
        }
    }

    if (const auto* object = find_object(status, "virtual_sdcard")) {
        Merger merger(*object);
        merger.number("progress", state.sdcard_progress);
        merger.boolean("is_active", state.sdcard_active);
        changed |= merger.changed();
    }

    if (const auto* object = find_object(status, "display_status")) {
        Merger merger(*object);
        merger.number("progress", state.display_progress);
        merger.string("message", state.display_message);
        changed |= merger.changed();
    }

    if (const auto* object = find_object(status, "extruder")) {
        Merger merger(*object);
        merge_heater(merger, state.extruder);
        merger.boolean("can_extrude", state.extruder.can_extrude);
        changed |= merger.changed();
    }

    if (const auto it = status.find("heater_bed"); it != status.end()) {
        // Moonraker returns null for objects the printer does not have.
        const bool present = it->is_object();
        if (state.has_heater_bed != present) {
            state.has_heater_bed = present;
            changed = true;
        }
        if (present) {
            Merger merger(*it);
            merge_heater(merger, state.heater_bed);
            changed |= merger.changed();
        }
    }

    if (const auto* object = find_object(status, "toolhead")) {
        Merger merger(*object);
        merger.string("homed_axes", state.homed_axes);
        changed |= merger.changed();

        const auto position = object->find("position");
        if (position != object->end() && position->is_array()) {
            for (std::size_t axis = 0; axis < state.position.size() && axis < position->size(); ++axis) {
                const auto& value = (*position)[axis];
                if (value.is_number() && state.position[axis] != value.get<double>()) {
                    state.position[axis] = value.get<double>();
                    changed = true;
                }
            }
        }
    }

    if (const auto* object = find_object(status, "gcode_move")) {
        Merger merger(*object);
        merger.number("speed_factor", state.speed_factor);
        merger.number("extrude_factor", state.extrude_factor);
        changed |= merger.changed();
    }

    if (const auto* object = find_object(status, "fan")) {
        Merger merger(*object);
        merger.number("speed", state.fan_speed);
        changed |= merger.changed();
    }

    if (const auto* object = find_object(status, "idle_timeout")) {
        Merger merger(*object);
        merger.string("state", state.idle_state);
        changed |= merger.changed();
    }

    for (auto& sensor : state.sensors) {
        const auto* object = find_object(status, sensor.object.c_str());
        if (!object) {
            continue;
        }
        const auto it = object->find("temperature");
        if (it == object->end() || !it->is_number()) {
            continue;
        }
        const double temperature = it->get<double>();
        if (!sensor.known || sensor.temperature != temperature) {
            sensor.temperature = temperature;
            sensor.known = true;
            changed = true;
        }
    }

    return changed;
}

} // namespace moonraker

/*
 * SPDX-License-Identifier: MIT
 *
 * Fake Moonraker server for developing KlipperDeckZero without a printer.
 *
 * Implements the subset of the Moonraker JSON-RPC API used by the app over
 * WebSocket (ws://127.0.0.1:7125/websocket) and simulates heaters, extrusion
 * checks, printing progress, emergency stop and firmware restart.
 *
 * Usage: fake_moonraker [--host 127.0.0.1] [--port 7125] [--speed 20] [--print <file>]
 *                       [--webcam <snapshot url>]...
 *   --speed  print time multiplier (simulated seconds per real second)
 *   --print  start printing one of the fake files immediately (e.g. 3DBenchy_PLA_0.2mm.gcode)
 *   --webcam list a webcam with this snapshot URL in server.webcams.list (repeatable); the
 *            images themselves come from any HTTP server, e.g. `python -m http.server`
 */

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocketServer.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <utility>
#include <thread>
#include <vector>

using nlohmann::json;
using Clock = std::chrono::steady_clock;

namespace {

std::atomic<bool> g_running{true};

void on_signal(int) {
    g_running = false;
}

struct FakeFile {
    std::string path;
    double size;
    double estimated_time;
    double filament_total;
    double object_height;
};

const std::vector<FakeFile> kFiles = {
    {"3DBenchy_PLA_0.2mm.gcode", 3'456'789, 4980, 4890.5, 48.0},
    {"calibration_cube_20mm.gcode", 456'123, 1260, 1520.2, 20.0},
    {"phone_stand_PETG.gcode", 2'345'678, 7320, 9120.8, 85.0},
    {"spool_holder/arm_v2.gcode", 1'234'567, 3600, 3010.0, 30.0},
    // Slicer-style names that only differ at the end, longer than a list row.
    {"3DBenchy_0.2mm_PLA_Generic Klipper Printer_1h26m.gcode", 2'411'000, 5160, 4710.0, 48.0},
    {"3DBenchy_0.2mm_PLA_Generic Klipper Printer_29m15s.gcode", 1'038'000, 1755, 4650.0, 48.0},
};

class Printer {
public:
    Printer(double speed, std::vector<std::string> webcams) : speed_(speed), webcams_(std::move(webcams)) {
        reset_objects();
    }

    json start_print(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mutex_);
        return print_start(filename);
    }

    // Returns {result} or {error}; may queue notifications.
    json handle(const std::string& method, const json& params, int connection_id) {
        std::lock_guard<std::mutex> lock(mutex_);

        if (method == "server.connection.identify") {
            return result({{"connection_id", connection_id}});
        }
        if (method == "server.info") {
            return result({{"klippy_connected", klippy_state_ != "disconnected"},
                           {"klippy_state", klippy_state_},
                           {"components", json::array({"file_manager", "klippy_apis"})},
                           {"failed_components", json::array()},
                           {"warnings", json::array()},
                           {"websocket_count", 1},
                           {"moonraker_version", "fake-0.1"},
                           {"api_version", {1, 5, 0}}});
        }
        if (method == "printer.info") {
            return result({{"state", klippy_state_},
                           {"state_message", state_message_},
                           {"hostname", "fake-printer"},
                           {"software_version", "v0.12.0-fake"}});
        }
        if (klippy_state_ != "ready" && method.rfind("printer.", 0) == 0 &&
            method != "printer.firmware_restart" && method != "printer.restart") {
            return error(503, "Klippy Host not connected");
        }
        if (method == "printer.objects.list") {
            json names = json::array();
            for (const auto& [name, value] : objects_.items()) {
                names.push_back(name);
            }
            return result({{"objects", names}});
        }
        if (method == "printer.objects.subscribe" || method == "printer.objects.query") {
            if (method == "printer.objects.subscribe") {
                subscribers_.insert(connection_id);
            }
            return result({{"eventtime", eventtime()}, {"status", select(params.value("objects", json::object()))}});
        }
        if (method == "printer.gcode.script") {
            return run_script(params.value("script", std::string()));
        }
        if (method == "printer.print.start") {
            return print_start(params.value("filename", std::string()));
        }
        if (method == "printer.print.pause") {
            if (print_state() != "printing") {
                return error(400, "Print is not active");
            }
            set_print_state("paused");
            push_console("// Print paused");
            return result("ok");
        }
        if (method == "printer.print.resume") {
            if (print_state() != "paused") {
                return error(400, "Print is not paused");
            }
            set_print_state("printing");
            push_console("// Print resumed");
            return result("ok");
        }
        if (method == "printer.print.cancel") {
            if (print_state() != "printing" && print_state() != "paused") {
                return error(400, "No active print");
            }
            set_print_state("cancelled");
            objects_["virtual_sdcard"]["is_active"] = false;
            heaters_off();
            return result("ok");
        }
        if (method == "printer.emergency_stop") {
            emergency_stop();
            return result("ok");
        }
        if (method == "printer.firmware_restart" || method == "printer.restart") {
            firmware_restart();
            return result("ok");
        }
        if (method == "server.webcams.list") {
            json list = json::array();
            for (std::size_t i = 0; i < webcams_.size(); ++i) {
                list.push_back({{"name", i == 0 ? std::string("front") : "cam" + std::to_string(i + 1)},
                                {"enabled", true},
                                {"service", "mjpegstreamer-adaptive"},
                                {"target_fps", 15},
                                {"stream_url", "/webcam/?action=stream"},
                                {"snapshot_url", webcams_[i]},
                                {"flip_horizontal", false},
                                {"flip_vertical", false},
                                {"rotation", 0}});
            }
            return result({{"webcams", list}});
        }
        if (method == "server.files.list") {
            json files = json::array();
            for (const auto& file : kFiles) {
                files.push_back({{"path", file.path},
                                 {"modified", 1'760'000'000.0},
                                 {"size", file.size},
                                 {"permissions", "rw"}});
            }
            return result(files);
        }
        if (method == "server.files.metadata") {
            const auto* file = find_file(params.value("filename", std::string()));
            if (!file) {
                return error(404, "Metadata not available");
            }
            return result({{"filename", file->path},
                           {"size", file->size},
                           {"modified", 1'760'000'000.0},
                           {"slicer", "PrusaSlicer"},
                           {"slicer_version", "2.8.1"},
                           {"estimated_time", file->estimated_time},
                           {"filament_total", file->filament_total},
                           {"object_height", file->object_height},
                           {"layer_height", 0.2},
                           {"first_layer_height", 0.2},
                           {"first_layer_extr_temp", 215},
                           {"first_layer_bed_temp", 60},
                           {"thumbnails", json::array()}});
        }
        return error(-32601, "Method not found: " + method);
    }

    // Advances the simulation and returns messages to broadcast:
    // (true, msg) for subscribers only, (false, msg) for everyone.
    std::vector<std::pair<bool, json>> tick(double dt) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<std::pair<bool, json>> out;

        if (restart_at_ && Clock::now() >= *restart_at_) {
            restart_at_.reset();
            reset_objects();
            klippy_state_ = "ready";
            state_message_ = "Printer is ready";
            objects_["webhooks"] = {{"state", "ready"}, {"state_message", state_message_}};
            notifications_.push_back(notification("notify_klippy_ready", json()));
        }

        if (klippy_state_ == "ready") {
            simulate_heater(objects_["extruder"], 0.10, dt);
            simulate_heater(objects_["heater_bed"], 0.04, dt);
            objects_["extruder"]["can_extrude"] =
                objects_["extruder"]["temperature"].get<double>() >= kMinExtrudeTemp;
            simulate_print(dt);
        }

        for (auto& message : notifications_) {
            out.emplace_back(false, std::move(message));
        }
        notifications_.clear();

        json diff = json::object();
        for (const auto& [name, value] : objects_.items()) {
            json changed = json::object();
            for (const auto& [key, field] : value.items()) {
                if (!last_sent_.contains(name) || !last_sent_[name].contains(key) ||
                    last_sent_[name][key] != field) {
                    changed[key] = field;
                }
            }
            if (!changed.empty()) {
                diff[name] = changed;
            }
        }
        last_sent_ = objects_;
        if (!diff.empty()) {
            out.emplace_back(true, notification("notify_status_update", json::array({diff, eventtime()})));
        }
        return out;
    }

    bool is_subscriber(int connection_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        return subscribers_.count(connection_id) > 0;
    }

    void forget(int connection_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        subscribers_.erase(connection_id);
    }

private:
    static constexpr double kAmbient = 24.0;
    static constexpr double kMinExtrudeTemp = 170.0;

    static json result(json value) {
        return {{"result", std::move(value)}};
    }

    static json error(int code, const std::string& message) {
        return {{"error", {{"code", code}, {"message", message}}}};
    }

    static json notification(const std::string& method, json params) {
        json message = {{"jsonrpc", "2.0"}, {"method", method}};
        if (!params.is_null()) {
            message["params"] = std::move(params);
        }
        return message;
    }

    double eventtime() const {
        return std::chrono::duration<double>(Clock::now() - started_).count();
    }

    void reset_objects() {
        objects_ = {
            {"webhooks", {{"state", "ready"}, {"state_message", "Printer is ready"}}},
            {"print_stats",
             {{"state", "standby"},
              {"filename", ""},
              {"print_duration", 0.0},
              {"total_duration", 0.0},
              {"filament_used", 0.0},
              {"message", ""},
              {"info", {{"current_layer", nullptr}, {"total_layer", nullptr}}}}},
            {"virtual_sdcard", {{"progress", 0.0}, {"is_active", false}, {"file_position", 0}}},
            {"display_status", {{"progress", 0.0}, {"message", nullptr}}},
            {"extruder", {{"temperature", kAmbient}, {"target", 0.0}, {"power", 0.0}, {"can_extrude", false}}},
            {"heater_bed", {{"temperature", kAmbient}, {"target", 0.0}, {"power", 0.0}}},
            {"toolhead",
             {{"position", {0.0, 0.0, 0.0, 0.0}},
              {"homed_axes", ""},
              {"axis_minimum", {0.0, 0.0, -2.0, -99999.0}},
              {"axis_maximum", {235.0, 235.0, 250.0, 99999.0}}}},
            {"gcode_move", {{"speed_factor", 1.0}, {"extrude_factor", 1.0}, {"gcode_position", {0.0, 0.0, 0.0, 0.0}}}},
            {"fan", {{"speed", 0.0}}},
            {"idle_timeout", {{"state", "Idle"}}},
            {"heaters",
             {{"available_heaters", {"extruder", "heater_bed"}},
              {"available_sensors", {"extruder", "heater_bed", "temperature_sensor mcu"}}}},
            {"temperature_sensor mcu", {{"temperature", 41.8}}},
        };
        current_file_ = nullptr;
    }

    json select(const json& request) {
        json status = json::object();
        for (const auto& [name, fields] : request.items()) {
            if (!objects_.contains(name)) {
                continue;
            }
            if (fields.is_array() && !fields.empty()) {
                json object = json::object();
                for (const auto& field : fields) {
                    const auto key = field.get<std::string>();
                    if (objects_[name].contains(key)) {
                        object[key] = objects_[name][key];
                    }
                }
                status[name] = object;
            }
            else {
                status[name] = objects_[name];
            }
        }
        return status;
    }

    std::string print_state() const {
        return objects_["print_stats"]["state"].get<std::string>();
    }

    void set_print_state(const std::string& state) {
        objects_["print_stats"]["state"] = state;
        objects_["idle_timeout"]["state"] = state == "printing" ? "Printing" : "Ready";
    }

    const FakeFile* find_file(const std::string& name) const {
        const auto it = std::find_if(kFiles.begin(), kFiles.end(), [&](const FakeFile& f) { return f.path == name; });
        return it == kFiles.end() ? nullptr : &*it;
    }

    void push_console(const std::string& line) {
        notifications_.push_back(notification("notify_gcode_response", json::array({line})));
    }

    void heaters_off() {
        objects_["extruder"]["target"] = 0.0;
        objects_["heater_bed"]["target"] = 0.0;
    }

    void simulate_heater(json& heater, double rate, double dt) {
        const double target = heater["target"].get<double>();
        const double temperature = heater["temperature"].get<double>();
        const double goal = target > 0.0 ? target : kAmbient;
        const double step = (goal - temperature) * std::min(1.0, rate * dt * 4.0);
        double next = temperature + step + noise_(rng_) * (target > 0.0 ? 0.15 : 0.03);
        if (std::fabs(goal - next) < 0.05) {
            next = goal;
        }
        heater["temperature"] = std::round(next * 100.0) / 100.0;
        heater["power"] = target > 0.0 ? std::clamp((goal - next) / 15.0 + 0.25, 0.0, 1.0) : 0.0;
    }

    void simulate_print(double dt) {
        auto& stats = objects_["print_stats"];
        const auto state = print_state();
        if (state == "printing" || state == "paused") {
            stats["total_duration"] = stats["total_duration"].get<double>() + dt * speed_;
        }
        if (state != "printing" || !current_file_) {
            return;
        }
        // Wait for heaters like a real start G-code (M190/M109) would.
        const auto& extruder = objects_["extruder"];
        if (extruder["temperature"].get<double>() < extruder["target"].get<double>() - 3.0) {
            stats["message"] = "Heating...";
            return;
        }
        stats["message"] = "";

        const double duration = stats["print_duration"].get<double>() + dt * speed_;
        const double progress = std::min(1.0, duration / current_file_->estimated_time);
        stats["print_duration"] = duration;
        stats["filament_used"] = current_file_->filament_total * progress;
        const int total_layers = static_cast<int>(current_file_->object_height / 0.2);
        stats["info"] = {{"current_layer", std::max(1, static_cast<int>(progress * total_layers))},
                         {"total_layer", total_layers}};
        objects_["virtual_sdcard"]["progress"] = progress;
        objects_["virtual_sdcard"]["file_position"] = static_cast<int>(progress * current_file_->size);
        objects_["toolhead"]["position"] = {100.0 + 40.0 * std::sin(duration), 100.0 + 40.0 * std::cos(duration),
                                            progress * current_file_->object_height, 0.0};

        if (progress >= 1.0) {
            set_print_state("complete");
            objects_["virtual_sdcard"]["is_active"] = false;
            heaters_off();
            push_console("// Done printing file");
        }
    }

    json print_start(const std::string& filename) {
        const auto* file = find_file(filename);
        if (!file) {
            return error(400, "File not found: " + filename);
        }
        if (print_state() == "printing" || print_state() == "paused") {
            return error(400, "Printer is busy");
        }
        current_file_ = file;
        auto& stats = objects_["print_stats"];
        stats["filename"] = file->path;
        stats["print_duration"] = 0.0;
        stats["total_duration"] = 0.0;
        stats["filament_used"] = 0.0;
        stats["info"] = {{"current_layer", 0}, {"total_layer", static_cast<int>(file->object_height / 0.2)}};
        set_print_state("printing");
        objects_["virtual_sdcard"] = {{"progress", 0.0}, {"is_active", true}, {"file_position", 0}};
        objects_["extruder"]["target"] = 215.0;
        objects_["heater_bed"]["target"] = 60.0;
        objects_["toolhead"]["homed_axes"] = "xyz";
        push_console("// Starting print: " + file->path);
        return result("ok");
    }

    void emergency_stop() {
        heaters_off();
        klippy_state_ = "shutdown";
        state_message_ = "Shutdown due to M112 command\nOnce the underlying issue is corrected, use the\n"
                         "\"FIRMWARE_RESTART\" command to reset the firmware, reload the\nconfig, and restart the host software.";
        objects_["webhooks"] = {{"state", "shutdown"}, {"state_message", state_message_}};
        if (print_state() == "printing" || print_state() == "paused") {
            set_print_state("error");
        }
        push_console("!! Shutdown due to M112 command");
        notifications_.push_back(notification("notify_klippy_shutdown", json()));
    }

    void firmware_restart() {
        klippy_state_ = "startup";
        state_message_ = "Printer is restarting";
        notifications_.push_back(notification("notify_klippy_disconnected", json()));
        restart_at_ = Clock::now() + std::chrono::seconds(3);
    }

    static double param(const std::string& word, char key, double fallback) {
        return word.size() > 1 && std::toupper(static_cast<unsigned char>(word[0])) == key
                   ? std::atof(word.c_str() + 1)
                   : fallback;
    }

    json run_script(const std::string& script) {
        std::istringstream lines(script);
        std::string line;
        while (std::getline(lines, line)) {
            std::istringstream words(line);
            std::vector<std::string> tokens;
            for (std::string word; words >> word;) {
                tokens.push_back(word);
            }
            if (tokens.empty()) {
                continue;
            }
            std::string command = tokens[0];
            std::transform(command.begin(), command.end(), command.begin(), ::toupper);

            auto named = [&](const std::string& name) -> std::string {
                for (const auto& token : tokens) {
                    const auto eq = token.find('=');
                    if (eq != std::string::npos) {
                        std::string key = token.substr(0, eq);
                        std::transform(key.begin(), key.end(), key.begin(), ::toupper);
                        if (key == name) {
                            return token.substr(eq + 1);
                        }
                    }
                }
                return {};
            };
            auto letter = [&](char key, double fallback) {
                double value = fallback;
                for (std::size_t i = 1; i < tokens.size(); ++i) {
                    value = param(tokens[i], key, value);
                }
                return value;
            };

            if (command == "SET_HEATER_TEMPERATURE") {
                const auto heater = named("HEATER");
                if (heater != "extruder" && heater != "heater_bed") {
                    return error(400, "Unknown heater '" + heater + "'");
                }
                objects_[heater]["target"] = std::atof(named("TARGET").c_str());
            }
            else if (command == "M104" || command == "M109") {
                objects_["extruder"]["target"] = letter('S', 0.0);
            }
            else if (command == "M140" || command == "M190") {
                objects_["heater_bed"]["target"] = letter('S', 0.0);
            }
            else if (command == "TURN_OFF_HEATERS") {
                heaters_off();
            }
            else if (command == "G28") {
                objects_["toolhead"]["homed_axes"] = "xyz";
                objects_["toolhead"]["position"] = {0.0, 0.0, 0.0, 0.0};
            }
            else if (command == "M84" || command == "M18") {
                objects_["toolhead"]["homed_axes"] = "";
            }
            else if (command == "G1" || command == "G0") {
                const double e = letter('E', 0.0);
                if (e != 0.0) {
                    if (!objects_["extruder"]["can_extrude"].get<bool>()) {
                        push_console("!! Extrude below minimum temp");
                        return error(400,
                                     "Extrude below minimum temp\nSee the 'min_extrude_temp' config option for details");
                    }
                    auto& position = objects_["toolhead"]["position"];
                    position[3] = position[3].get<double>() + e;
                }
            }
            else if (command == "M106") {
                objects_["fan"]["speed"] = std::clamp(letter('S', 255.0) / 255.0, 0.0, 1.0);
            }
            else if (command == "M107") {
                objects_["fan"]["speed"] = 0.0;
            }
            else if (command == "M220") {
                objects_["gcode_move"]["speed_factor"] = letter('S', 100.0) / 100.0;
            }
            else if (command == "M221") {
                objects_["gcode_move"]["extrude_factor"] = letter('S', 100.0) / 100.0;
            }
            else if (command == "M117") {
                const auto pos = line.find(' ');
                objects_["display_status"]["message"] = pos == std::string::npos ? "" : line.substr(pos + 1);
            }
            else if (command == "M112") {
                emergency_stop();
            }
            // Everything else (M83, G90, SAVE_GCODE_STATE, ...) is accepted silently.
        }
        return result("ok");
    }

    std::mutex mutex_;
    double speed_;
    std::vector<std::string> webcams_;
    json objects_;
    json last_sent_ = json::object();
    std::string klippy_state_{"ready"};
    std::string state_message_{"Printer is ready"};
    std::vector<json> notifications_;
    std::set<int> subscribers_;
    const FakeFile* current_file_{nullptr};
    std::optional<Clock::time_point> restart_at_;
    Clock::time_point started_{Clock::now()};
    std::mt19937 rng_{42};
    std::normal_distribution<double> noise_{0.0, 1.0};
};

} // namespace

int main(int argc, char** argv) {
    std::string host = "127.0.0.1";
    int port = 7125;
    double speed = 20.0;
    std::string autostart;
    std::vector<std::string> webcams;
    for (int i = 1; i < argc; i += 2) {
        const std::string flag = argv[i];
        if (flag != "--host" && flag != "--port" && flag != "--speed" && flag != "--print" && flag != "--webcam") {
            std::fprintf(stderr, "unknown option: %s\n", flag.c_str());
            return 2;
        }
        if (i + 1 >= argc) {
            std::fprintf(stderr, "missing value for %s\n", flag.c_str());
            return 2;
        }
        if (flag == "--host") {
            host = argv[i + 1];
        }
        else if (flag == "--port") {
            port = std::atoi(argv[i + 1]);
        }
        else if (flag == "--speed") {
            speed = std::atof(argv[i + 1]);
        }
        else if (flag == "--print") {
            autostart = argv[i + 1];
        }
        else if (flag == "--webcam") {
            webcams.emplace_back(argv[i + 1]);
        }
    }

    // Show request logs immediately even when stdout is redirected.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    ix::initNetSystem();

    Printer printer(speed, std::move(webcams));
    if (!autostart.empty()) {
        const auto started = printer.start_print(autostart);
        if (started.contains("error")) {
            std::fprintf(stderr, "--print: %s\n", started["error"]["message"].get<std::string>().c_str());
            return 1;
        }
    }
    ix::WebSocketServer server(port, host);
    std::atomic<int> next_connection_id{1};
    std::mutex clients_mutex;
    std::map<ix::WebSocket*, int> clients;

    server.setOnClientMessageCallback(
        [&](std::shared_ptr<ix::ConnectionState>, ix::WebSocket& socket, const ix::WebSocketMessagePtr& message) {
            if (message->type == ix::WebSocketMessageType::Open) {
                const int id = next_connection_id++;
                std::lock_guard<std::mutex> lock(clients_mutex);
                clients[&socket] = id;
                std::printf("client %d connected (%s)\n", id, message->openInfo.uri.c_str());
                return;
            }
            if (message->type == ix::WebSocketMessageType::Close) {
                std::lock_guard<std::mutex> lock(clients_mutex);
                const auto it = clients.find(&socket);
                if (it != clients.end()) {
                    std::printf("client %d disconnected\n", it->second);
                    printer.forget(it->second);
                    clients.erase(it);
                }
                return;
            }
            if (message->type != ix::WebSocketMessageType::Message) {
                return;
            }

            int connection_id = 0;
            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                connection_id = clients[&socket];
            }
            const json request = json::parse(message->str, nullptr, false);
            if (request.is_discarded() || !request.is_object() || !request.contains("method")) {
                socket.sendText(json{{"jsonrpc", "2.0"}, {"error", {{"code", -32700}, {"message", "Parse error"}}},
                                     {"id", nullptr}}
                                    .dump());
                return;
            }
            json response;
            try {
                const auto method = request.at("method").get<std::string>();
                std::printf("[%d] %s\n", connection_id, method.c_str());
                response = printer.handle(method, request.value("params", json::object()), connection_id);
            }
            catch (const json::exception& e) {
                // A wrong-shaped request must not escape the connection thread and terminate the server.
                response = json{{"error", {{"code", -32602}, {"message", std::string("Invalid params: ") + e.what()}}}};
            }
            response["jsonrpc"] = "2.0";
            response["id"] = request.value("id", json());
            socket.sendText(response.dump());
        });

    const auto listening = server.listen();
    if (!listening.first) {
        std::fprintf(stderr, "listen failed on %s:%d: %s\n", host.c_str(), port, listening.second.c_str());
        return 1;
    }
    server.start();
    std::printf("fake moonraker listening on ws://%s:%d/websocket (speed x%.0f)\n", host.c_str(), port, speed);

    constexpr double kTickSeconds = 0.25;
    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(kTickSeconds * 1000)));
        const auto messages = printer.tick(kTickSeconds);
        if (messages.empty()) {
            continue;
        }
        std::lock_guard<std::mutex> lock(clients_mutex);
        for (const auto& [subscribers_only, payload] : messages) {
            const auto text = payload.dump();
            for (const auto& [socket, id] : clients) {
                if (!subscribers_only || printer.is_subscriber(id)) {
                    socket->sendText(text);
                }
            }
        }
    }

    server.stop();
    ix::uninitNetSystem();
    return 0;
}

/*
 * SPDX-License-Identifier: MIT
 */

#include "fake_transport.h"
#include "moonraker_session.h"
#include "test_framework.h"

#include <chrono>

using moonraker::Clock;
using moonraker::MoonrakerSession;
using moonraker::SessionState;
using moonraker::TransportEventType;
using nlohmann::json;
using namespace std::chrono_literals;

namespace {

struct Fixture {
    FakeTransport::Shared shared;
    MoonrakerSession session{std::make_unique<FakeTransport>(shared), make_options()};
    Clock::time_point now{};

    static moonraker::SessionOptions make_options() {
        moonraker::SessionOptions options;
        options.client_version = "1.2.3";
        options.reconnect_delay = 3000ms;
        options.klippy_retry_delay = 2000ms;
        return options;
    }

    void poll(std::chrono::milliseconds advance = 0ms) {
        now += advance;
        session.poll(now);
    }

    const json& last_sent() const {
        return shared.sent.back();
    }

    std::string last_method() const {
        return shared.sent.empty() ? std::string() : last_sent()["method"].get<std::string>();
    }

    // Runs the startup sequence up to the point where server.info is pending.
    void connect_and_identify() {
        session.start();
        inject(shared, TransportEventType::Opened);
        poll();
        CHECK_EQ(last_method(), std::string("server.connection.identify"));
        reply(shared, last_sent(), json{{"connection_id", 1}});
        poll();
        CHECK_EQ(last_method(), std::string("server.info"));
    }

    // available_sensors is the printer's heaters.available_sensors list.
    void reach_ready(const json& available_sensors = json::array({"extruder"})) {
        connect_and_identify();
        reply(shared, last_sent(), json{{"klippy_state", "ready"}});
        poll();
        CHECK_EQ(last_method(), std::string("printer.objects.query"));
        CHECK(last_sent()["params"]["objects"].contains("heaters"));
        reply(shared, last_sent(), json{{"status", {{"heaters", {{"available_sensors", available_sensors}}}}}});
        poll();
        CHECK_EQ(last_method(), std::string("printer.objects.subscribe"));
        reply(shared, last_sent(), json{{"eventtime", 1.0},
                                        {"status",
                                         {{"extruder", {{"temperature", 25.0}, {"target", 0.0}}},
                                          {"print_stats", {{"state", "standby"}}}}}});
        poll();
        CHECK(session.session_state() == SessionState::Ready);
    }
};

} // namespace

TEST_CASE("session: identify payload") {
    Fixture f;
    f.session.start();
    CHECK(f.session.session_state() == SessionState::Connecting);
    CHECK_EQ(f.shared.open_calls, 1);
    inject(f.shared, TransportEventType::Opened);
    f.poll();
    CHECK(f.session.session_state() == SessionState::Initializing);
    const auto& params = f.last_sent()["params"];
    CHECK_EQ(params["client_name"].get<std::string>(), std::string("KlipperDeckZero"));
    CHECK_EQ(params["version"].get<std::string>(), std::string("1.2.3"));
    CHECK_EQ(params["type"].get<std::string>(), std::string("display"));
    CHECK(!params.contains("api_key"));
}

TEST_CASE("session: ready flow subscribes and merges snapshot") {
    Fixture f;
    f.reach_ready();
    const auto& objects = f.shared.sent.back()["params"]["objects"];
    CHECK(objects.contains("extruder"));
    CHECK(objects.contains("print_stats"));
    CHECK(f.session.printer().klippy_state == model::KlippyState::Ready);
    CHECK_NEAR(f.session.printer().extruder.temperature, 25.0, 1e-9);
    CHECK(f.session.printer().print_state == model::PrintState::Standby);
}

TEST_CASE("session: extra temperature sensors are subscribed and merged") {
    Fixture f;
    f.reach_ready(json::array({"temperature_sensor Micro4", "extruder", "heater_bed", "temperature_fan chamber"}));
    const auto& objects = f.shared.sent.back()["params"]["objects"];
    CHECK(objects.contains("temperature_sensor Micro4"));
    CHECK(objects.contains("temperature_fan chamber"));

    const auto& sensors = f.session.printer().sensors;
    CHECK_EQ(sensors.size(), std::size_t{2});
    CHECK_EQ(sensors[0].name, std::string("Micro4"));
    CHECK_EQ(sensors[1].name, std::string("chamber"));
    CHECK(!sensors[0].known);

    notify(f.shared, "notify_status_update",
           json::array({{{"temperature_sensor Micro4", {{"temperature", 52.2}}}}, 2.0}));
    f.poll();
    CHECK(f.session.printer().sensors[0].known);
    CHECK_NEAR(f.session.printer().sensors[0].temperature, 52.2, 1e-9);
    CHECK(!f.session.printer().sensors[1].known);
}

TEST_CASE("session: a failed sensor query still subscribes") {
    Fixture f;
    f.connect_and_identify();
    reply(f.shared, f.last_sent(), json{{"klippy_state", "ready"}});
    f.poll();
    CHECK_EQ(f.last_method(), std::string("printer.objects.query"));
    reply_error(f.shared, f.last_sent(), 400, "Invalid object");
    f.poll();
    CHECK_EQ(f.last_method(), std::string("printer.objects.subscribe"));
    CHECK(f.session.printer().sensors.empty());
}

TEST_CASE("session: status updates bump revision and notify listener") {
    Fixture f;
    f.reach_ready();
    int changes = 0;
    f.session.set_change_listener([&] { ++changes; });
    const auto before = f.session.revision();

    notify(f.shared, "notify_status_update", json::array({{{"extruder", {{"temperature", 180.5}}}}, 2.0}));
    f.poll();
    CHECK_EQ(changes, 1);
    CHECK(f.session.revision() > before);
    CHECK_NEAR(f.session.printer().extruder.temperature, 180.5, 1e-9);

    // Nothing new: no notification.
    f.poll();
    CHECK_EQ(changes, 1);
}

TEST_CASE("session: klippy startup retries server.info") {
    Fixture f;
    f.connect_and_identify();
    reply(f.shared, f.last_sent(), json{{"klippy_state", "startup"}});
    f.poll();
    CHECK(f.session.session_state() == SessionState::WaitingKlippy);
    const auto sent_before = f.shared.sent.size();
    f.poll(1999ms);
    CHECK_EQ(f.shared.sent.size(), sent_before);
    f.poll(1ms);
    CHECK_EQ(f.shared.sent.size(), sent_before + 1);
    CHECK_EQ(f.last_method(), std::string("server.info"));
}

TEST_CASE("session: klippy error queries printer.info message") {
    Fixture f;
    f.connect_and_identify();
    reply(f.shared, f.last_sent(), json{{"klippy_state", "error"}});
    f.poll();
    CHECK(f.session.session_state() == SessionState::KlippyError);
    CHECK_EQ(f.last_method(), std::string("printer.info"));
    reply(f.shared, f.last_sent(), json{{"state", "error"}, {"state_message", "MCU 'mcu' shutdown"}});
    f.poll();
    CHECK_EQ(f.session.printer().state_message, std::string("MCU 'mcu' shutdown"));
}

TEST_CASE("session: klippy notifications drive state") {
    Fixture f;
    f.reach_ready();

    notify(f.shared, "notify_klippy_shutdown", json());
    f.poll();
    CHECK(f.session.session_state() == SessionState::KlippyError);
    CHECK(f.session.printer().klippy_state == model::KlippyState::Shutdown);

    notify(f.shared, "notify_klippy_disconnected", json());
    f.poll();
    CHECK(f.session.session_state() == SessionState::WaitingKlippy);

    notify(f.shared, "notify_klippy_ready", json());
    f.poll();
    CHECK_EQ(f.last_method(), std::string("server.info"));
}

TEST_CASE("session: reconnects after transport loss") {
    Fixture f;
    f.reach_ready();
    inject(f.shared, TransportEventType::Closed, "connection reset");
    f.poll();
    CHECK(f.session.session_state() == SessionState::Connecting);
    CHECK_EQ(f.session.last_error(), std::string("connection reset"));
    CHECK(f.session.printer().klippy_state == model::KlippyState::Unknown);
    CHECK_EQ(f.shared.open_calls, 1);

    f.poll(2999ms);
    CHECK_EQ(f.shared.open_calls, 1);
    f.poll(1ms);
    CHECK_EQ(f.shared.open_calls, 2);

    // Late response from the old connection must be ignored after reconnect.
    inject(f.shared, TransportEventType::Opened);
    f.poll();
    CHECK(f.session.session_state() == SessionState::Initializing);
}

TEST_CASE("session: stop does not schedule reconnect") {
    Fixture f;
    f.reach_ready();
    f.session.stop();
    CHECK(f.session.session_state() == SessionState::Stopped);
    f.poll(10000ms);
    CHECK_EQ(f.shared.open_calls, 1);
}

TEST_CASE("session: gcode responses and operations") {
    Fixture f;
    f.reach_ready();
    notify(f.shared, "notify_gcode_response", json::array({"!! Must home axis first"}));
    f.poll();
    auto lines = f.session.take_gcode_responses();
    CHECK_EQ(lines.size(), std::size_t{1});
    CHECK_EQ(lines.front(), std::string("!! Must home axis first"));
    CHECK(f.session.take_gcode_responses().empty());

    f.session.send_gcode("SET_HEATER_TEMPERATURE HEATER=extruder TARGET=200");
    CHECK_EQ(f.last_method(), std::string("printer.gcode.script"));
    CHECK_EQ(f.last_sent()["params"]["script"].get<std::string>(),
             std::string("SET_HEATER_TEMPERATURE HEATER=extruder TARGET=200"));

    f.session.print_start("benchy.gcode");
    CHECK_EQ(f.last_method(), std::string("printer.print.start"));
    f.session.emergency_stop();
    CHECK_EQ(f.last_method(), std::string("printer.emergency_stop"));
    f.session.restart();
    CHECK_EQ(f.last_method(), std::string("printer.restart"));
    f.session.firmware_restart();
    CHECK_EQ(f.last_method(), std::string("printer.firmware_restart"));

    notify(f.shared, "notify_filelist_changed", json::array({{{"action", "create_file"}}}));
    f.poll();
    CHECK(f.session.take_filelist_changed());
    CHECK(!f.session.take_filelist_changed());
}

TEST_CASE("session: klippy shutdown notification schedules server.info fallback") {
    Fixture f;
    f.reach_ready();
    notify(f.shared, "notify_klippy_shutdown", json());
    f.poll();
    CHECK(f.session.session_state() == SessionState::KlippyError);
    CHECK_EQ(f.last_method(), std::string("printer.info"));
    reply(f.shared, f.last_sent(), json{{"state", "shutdown"}, {"state_message", "Emergency stop"}});
    f.poll();

    // notify_klippy_ready is never delivered; the fallback poll must still run.
    const auto sent_before = f.shared.sent.size();
    f.poll(9999ms);
    CHECK_EQ(f.shared.sent.size(), sent_before);
    f.poll(1ms);
    CHECK_EQ(f.shared.sent.size(), sent_before + 1);
    CHECK_EQ(f.last_method(), std::string("server.info"));

    reply(f.shared, f.last_sent(), json{{"klippy_state", "ready"}});
    f.poll();
    CHECK_EQ(f.last_method(), std::string("printer.objects.query"));
    reply(f.shared, f.last_sent(), json{{"status", {{"heaters", {{"available_sensors", json::array()}}}}}});
    f.poll();
    CHECK_EQ(f.last_method(), std::string("printer.objects.subscribe"));
    reply(f.shared, f.last_sent(), json{{"status", json::object()}});
    f.poll();
    CHECK(f.session.session_state() == SessionState::Ready);
    CHECK(f.session.printer().klippy_state == model::KlippyState::Ready);
}

TEST_CASE("session: silent connection is probed, then reconnected") {
    Fixture f;
    f.reach_ready();
    const auto sent_before = f.shared.sent.size();

    // Reaching Ready is a fresh baseline: no probe right away.
    f.poll(9999ms);
    CHECK_EQ(f.shared.sent.size(), sent_before);
    f.poll(1ms);
    CHECK_EQ(f.shared.sent.size(), sent_before + 1);
    CHECK_EQ(f.last_method(), std::string("server.info"));
    CHECK(f.session.session_state() == SessionState::Ready);

    // Only one probe outstanding at a time.
    f.poll(4999ms);
    CHECK_EQ(f.shared.sent.size(), sent_before + 1);
    CHECK(f.session.session_state() == SessionState::Ready);

    f.poll(1ms);
    CHECK(f.session.session_state() == SessionState::Connecting);
    CHECK_EQ(f.session.last_error(), std::string("no response from Moonraker (liveness check failed)"));
    CHECK_EQ(f.shared.open_calls, 1);

    f.poll(2999ms);
    CHECK_EQ(f.shared.open_calls, 1);
    f.poll(1ms);
    CHECK_EQ(f.shared.open_calls, 2);

    inject(f.shared, TransportEventType::Opened);
    f.poll();
    CHECK(f.session.session_state() == SessionState::Initializing);
    CHECK_EQ(f.last_method(), std::string("server.connection.identify"));
}

TEST_CASE("session: answered probe keeps the connection") {
    Fixture f;
    f.reach_ready();
    f.poll(10000ms);
    CHECK_EQ(f.last_method(), std::string("server.info"));
    const auto sent_after_probe = f.shared.sent.size();

    f.poll(1000ms);
    reply(f.shared, f.last_sent(), json{{"klippy_state", "ready"}});
    f.poll();
    CHECK(f.session.session_state() == SessionState::Ready);

    // Well past the probe timeout, but inside the new liveness window: nothing happens.
    f.poll(9999ms);
    CHECK(f.session.session_state() == SessionState::Ready);
    CHECK_EQ(f.shared.open_calls, 1);
    CHECK_EQ(f.shared.close_calls, 0);
    CHECK_EQ(f.shared.sent.size(), sent_after_probe);

    // The next silent window probes again.
    f.poll(1ms);
    CHECK_EQ(f.shared.sent.size(), sent_after_probe + 1);
    CHECK_EQ(f.last_method(), std::string("server.info"));
}

TEST_CASE("session: status traffic postpones the probe") {
    Fixture f;
    f.reach_ready();
    const auto sent_before = f.shared.sent.size();
    for (int i = 0; i < 5; ++i) {
        f.poll(5000ms);
        notify(f.shared, "notify_status_update", json::array({{{"extruder", {{"temperature", 26.0}}}}, 2.0}));
        f.poll();
    }
    CHECK_EQ(f.shared.sent.size(), sent_before);
    CHECK(f.session.session_state() == SessionState::Ready);
}

TEST_CASE("session: server.info timeout while waiting for klippy reconnects") {
    Fixture f;
    f.connect_and_identify();
    reply(f.shared, f.last_sent(), json{{"klippy_state", "startup"}});
    f.poll();
    CHECK(f.session.session_state() == SessionState::WaitingKlippy);
    f.poll(2000ms);
    CHECK_EQ(f.last_method(), std::string("server.info"));

    // The retry is never answered: the link is dead, not Klippy.
    f.poll(10000ms);
    CHECK(f.session.session_state() == SessionState::Connecting);
    f.poll(3000ms);
    CHECK_EQ(f.shared.open_calls, 2);
}

TEST_CASE("session: server.info error reply keeps polling") {
    Fixture f;
    f.connect_and_identify();
    reply_error(f.shared, f.last_sent(), 503, "Service Unavailable");
    f.poll();
    CHECK(f.session.session_state() == SessionState::Initializing);
    f.poll(2000ms);
    CHECK_EQ(f.last_method(), std::string("server.info"));
    CHECK_EQ(f.shared.open_calls, 1);
}

TEST_CASE("session: server error reply with a local error code while waiting for klippy keeps polling") {
    Fixture f;
    f.connect_and_identify();
    reply(f.shared, f.last_sent(), json{{"klippy_state", "startup"}});
    f.poll();
    CHECK(f.session.session_state() == SessionState::WaitingKlippy);
    f.poll(2000ms);
    CHECK_EQ(f.last_method(), std::string("server.info"));
    const auto sent_before = f.shared.sent.size();

    // The server answered, even though its code collides with kErrorTimeout.
    reply_error(f.shared, f.last_sent(), moonraker::kErrorTimeout, "server-side timeout");
    f.poll();
    CHECK(f.session.session_state() == SessionState::WaitingKlippy);
    CHECK_EQ(f.shared.open_calls, 1);
    f.poll(1999ms);
    CHECK_EQ(f.shared.sent.size(), sent_before);
    f.poll(1ms);
    CHECK_EQ(f.shared.sent.size(), sent_before + 1);
    CHECK_EQ(f.last_method(), std::string("server.info"));
    CHECK_EQ(f.shared.open_calls, 1);
}

TEST_CASE("session: probe answered with a local error code keeps the connection") {
    Fixture f;
    f.reach_ready();
    f.poll(10000ms);
    CHECK_EQ(f.last_method(), std::string("server.info"));

    reply_error(f.shared, f.last_sent(), moonraker::kErrorSendFailed, "server-side send failure");
    f.poll();
    CHECK(f.session.session_state() == SessionState::Ready);
    CHECK_EQ(f.shared.open_calls, 1);
    CHECK_EQ(f.shared.close_calls, 0);

    // Past the probe timeout: the answered probe does not trip the watchdog.
    f.poll(5000ms);
    CHECK(f.session.session_state() == SessionState::Ready);
    CHECK_EQ(f.shared.open_calls, 1);
}

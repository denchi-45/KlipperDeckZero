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

// The request that looks up the Klipper service in Moonraker's database.
const std::string kServiceLookup = "server.database.get_item";

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

    // The most recent request sent with this method (null when there is none).
    json last_sent_of(const std::string& method) const {
        for (auto it = shared.sent.rbegin(); it != shared.sent.rend(); ++it) {
            if ((*it)["method"] == method) {
                return *it;
            }
        }
        return json();
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
        // The service is only asked once Klippy is identified.
        CHECK(last_sent_of(kServiceLookup).is_null());
    }

    // Moonraker's stored service info as server.database.get_item returns it; an empty
    // klipper_service is what Moonraker stores after a failed lookup.
    static json stored_service(const std::string& klipper_service) {
        json value = json::object();
        if (!klipper_service.empty()) {
            value = {{"unit_name", klipper_service + ".service"}, {"active_state", "active"}};
        }
        return json{{"namespace", "moonraker"}, {"key", "klippy_connection.service_info"}, {"value", value}};
    }

    // Answers the latest service lookup with klipper_service as the stored unit (empty: none).
    void answer_service_lookup(const std::string& klipper_service = "klipper") {
        const auto request = last_sent_of(kServiceLookup);
        CHECK(!request.is_null());
        CHECK_EQ(request["params"]["namespace"].get<std::string>(), std::string("moonraker"));
        CHECK_EQ(request["params"]["key"].get<std::string>(), std::string("klippy_connection.service_info"));
        reply(shared, request, stored_service(klipper_service));
        poll();
    }

    // Runs the ready flow from a pending server.info; service lookup is then pending.
    void finish_ready(const json& available_sensors = json::array({"extruder"})) {
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
        // Klippy is identified now, so its service is asked.
        CHECK_EQ(last_method(), std::string(kServiceLookup));
    }

    // available_sensors is the printer's heaters.available_sensors list; klipper_service is what
    // service lookup reports.
    void reach_ready(const json& available_sensors = json::array({"extruder"}),
                     const std::string& klipper_service = "klipper") {
        connect_and_identify();
        finish_ready(available_sensors);
        answer_service_lookup(klipper_service);
        CHECK_EQ(session.klipper_service(), klipper_service);
    }

    // From Ready: Klippy disconnects and stays in startup until it has been stuck for the timeout.
    // The last server.info retry is left pending.
    void stall_klippy() {
        notify(shared, "notify_klippy_disconnected", json());
        poll();
        CHECK(session.session_state() == SessionState::WaitingKlippy);
        // Keep answering server.info with "startup" so the link stays alive.
        for (int i = 0; i < 14; ++i) {
            poll(2000ms);
            reply(shared, last_sent(), json{{"klippy_state", "startup"}});
            poll();
        }
        CHECK(!session.klippy_unresponsive());
        poll(2000ms);
        CHECK(session.klippy_unresponsive());
        CHECK_EQ(last_method(), std::string("server.info"));
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
    const auto objects = f.last_sent_of("printer.objects.subscribe")["params"]["objects"];
    CHECK(objects.contains("extruder"));
    CHECK(objects.contains("print_stats"));
    CHECK(f.session.printer().klippy_state == model::KlippyState::Ready);
    CHECK_NEAR(f.session.printer().extruder.temperature, 25.0, 1e-9);
    CHECK(f.session.printer().print_state == model::PrintState::Standby);
}

TEST_CASE("session: extra temperature sensors are subscribed and merged") {
    Fixture f;
    f.reach_ready(json::array({"temperature_sensor Micro4", "extruder", "heater_bed", "temperature_fan chamber"}));
    const auto objects = f.last_sent_of("printer.objects.subscribe")["params"]["objects"];
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

TEST_CASE("session: klippy stuck in startup becomes unresponsive") {
    Fixture f;
    f.reach_ready();
    notify(f.shared, "notify_klippy_disconnected", json());
    f.poll();
    CHECK(f.session.session_state() == SessionState::WaitingKlippy);
    CHECK(!f.session.klippy_unresponsive());
    // The service verified while Klippy was ready is kept while it is gone.
    CHECK_EQ(f.session.klipper_service(), std::string("klipper"));

    // Keep answering server.info with "startup" so the link stays alive.
    for (int i = 0; i < 14; ++i) {
        f.poll(2000ms);
        reply(f.shared, f.last_sent(), json{{"klippy_state", "startup"}});
        f.poll();
    }
    CHECK(!f.session.klippy_unresponsive());
    const auto before = f.session.revision();
    f.poll(2000ms);
    CHECK(f.session.klippy_unresponsive());
    CHECK(f.session.revision() > before);

    // The last server.info retry is still pending.
    const auto info_index = f.shared.sent.size() - 1;
    CHECK_EQ(f.last_method(), std::string("server.info"));

    f.session.restart_klipper_service();
    CHECK_EQ(f.last_method(), std::string("machine.services.restart"));
    CHECK_EQ(f.last_sent()["params"]["service"].get<std::string>(), std::string("klipper"));
    // Nothing is cleared until Moonraker confirms the restart.
    CHECK(f.session.klippy_unresponsive());
    reply(f.shared, f.last_sent(), json::object());
    f.poll();
    CHECK(!f.session.klippy_unresponsive());

    // The restarted Klippy gets a fresh full timeout.
    reply(f.shared, f.shared.sent[info_index], json{{"klippy_state", "startup"}});
    f.poll();
    for (int i = 0; i < 14; ++i) {
        f.poll(2000ms);
        CHECK_EQ(f.last_method(), std::string("server.info"));
        reply(f.shared, f.last_sent(), json{{"klippy_state", "startup"}});
        f.poll();
    }
    f.poll(1999ms);
    CHECK(f.session.session_state() == SessionState::WaitingKlippy);
    CHECK(!f.session.klippy_unresponsive());
    f.poll(1ms);
    CHECK(f.session.klippy_unresponsive());
}

TEST_CASE("session: unanswered firmware restart marks klippy unresponsive until ready") {
    Fixture f;
    f.connect_and_identify();
    reply(f.shared, f.last_sent(), json{{"klippy_state", "shutdown"}});
    f.poll();
    CHECK(f.session.session_state() == SessionState::KlippyError);
    reply(f.shared, f.last_sent(), json{{"state", "shutdown"}, {"state_message", "Lost communication"}});
    f.poll();

    bool timed_out = false;
    f.session.firmware_restart([&](const moonraker::RpcResponse& response) {
        timed_out = moonraker::is_no_response(response);
    });
    CHECK_EQ(f.last_method(), std::string("printer.firmware_restart"));
    CHECK(!f.session.klippy_unresponsive());
    f.poll(10000ms);
    CHECK(timed_out);
    CHECK(f.session.klippy_unresponsive());

    // Klippy comes back: the stale timeout no longer counts.
    notify(f.shared, "notify_klippy_ready", json());
    f.poll();
    reply(f.shared, f.last_sent(), json{{"klippy_state", "ready"}});
    f.poll();
    reply(f.shared, f.last_sent(), json{{"status", {{"heaters", {{"available_sensors", json::array()}}}}}});
    f.poll();
    reply(f.shared, f.last_sent(), json{{"status", json::object()}});
    f.poll();
    CHECK(f.session.session_state() == SessionState::Ready);
    CHECK(!f.session.klippy_unresponsive());
}

TEST_CASE("session: unanswered restart while ready offers the service restart") {
    Fixture f;
    f.reach_ready();

    // Klippy hangs without any notification: only the restart's timeout shows it.
    f.session.restart();
    CHECK_EQ(f.last_method(), std::string("printer.restart"));
    f.poll(10000ms);
    CHECK(f.session.klippy_unresponsive());
    // The liveness probe went out too; Moonraker itself still answers it.
    CHECK_EQ(f.last_method(), std::string("server.info"));
    reply(f.shared, f.last_sent(), json{{"klippy_state", "ready"}});
    f.poll();
    CHECK(f.session.session_state() == SessionState::Ready);
    CHECK(f.session.klippy_unresponsive());

    f.session.restart_klipper_service();
    CHECK_EQ(f.last_method(), std::string("machine.services.restart"));
    CHECK(f.session.klippy_unresponsive());
    reply(f.shared, f.last_sent(), json::object());
    f.poll();
    CHECK(!f.session.klippy_unresponsive());
}

TEST_CASE("session: rejected service restart keeps klippy unresponsive") {
    Fixture f;
    f.reach_ready();
    f.session.restart();
    f.poll(10000ms);
    CHECK(f.session.klippy_unresponsive());
    reply(f.shared, f.last_sent(), json{{"klippy_state", "ready"}});
    f.poll();

    bool answered = false;
    f.session.restart_klipper_service([&](const moonraker::RpcResponse& response) {
        answered = !response.ok && !moonraker::is_no_response(response);
    });
    CHECK_EQ(f.last_method(), std::string("machine.services.restart"));
    // Even with a code that collides with the local timeout, a server reply is a rejection.
    reply_error(f.shared, f.last_sent(), moonraker::kErrorTimeout, "Unauthorized");
    f.poll();
    CHECK(answered);
    CHECK(f.session.session_state() == SessionState::Ready);
    CHECK(f.session.klippy_unresponsive());

    // The user can still retry the service restart.
    const auto sent_before = f.shared.sent.size();
    f.session.restart_klipper_service();
    CHECK_EQ(f.shared.sent.size(), sent_before + 1);
    CHECK_EQ(f.last_method(), std::string("machine.services.restart"));
}

TEST_CASE("session: unanswered service restart keeps klippy unresponsive") {
    Fixture f;
    f.reach_ready();
    f.session.restart();
    f.poll(10000ms);
    CHECK(f.session.klippy_unresponsive());
    reply(f.shared, f.last_sent(), json{{"klippy_state", "ready"}});
    f.poll();

    bool timed_out = false;
    f.session.restart_klipper_service([&](const moonraker::RpcResponse& response) {
        timed_out = moonraker::is_no_response(response);
    });
    CHECK_EQ(f.last_method(), std::string("machine.services.restart"));
    f.poll(10000ms);
    CHECK(timed_out);
    CHECK(f.session.session_state() == SessionState::Ready);
    CHECK(f.session.klippy_unresponsive());

    f.session.restart_klipper_service();
    CHECK_EQ(f.last_method(), std::string("machine.services.restart"));
}

TEST_CASE("session: rejected service restart while klippy is stuck keeps the expired timeout") {
    Fixture f;
    f.reach_ready();
    f.stall_klippy();
    reply(f.shared, f.last_sent(), json{{"klippy_state", "startup"}});
    f.poll();
    CHECK(f.session.klippy_unresponsive());

    f.session.restart_klipper_service();
    reply_error(f.shared, f.last_sent(), 500, "Service restart failed");
    f.poll();
    CHECK(f.session.session_state() == SessionState::WaitingKlippy);
    CHECK(f.session.klippy_unresponsive());
}

TEST_CASE("session: restart timeout after klippy already recovered is ignored") {
    Fixture f;
    f.reach_ready();

    bool timed_out = false;
    f.session.restart([&](const moonraker::RpcResponse& response) {
        timed_out = moonraker::is_no_response(response);
    });
    CHECK_EQ(f.last_method(), std::string("printer.restart"));

    // Klippy restarts and is ready again before the restart request times out.
    f.poll(1000ms);
    notify(f.shared, "notify_klippy_disconnected", json());
    f.poll();
    CHECK(f.session.session_state() == SessionState::WaitingKlippy);
    notify(f.shared, "notify_klippy_ready", json());
    f.poll();
    CHECK_EQ(f.last_method(), std::string("server.info"));
    reply(f.shared, f.last_sent(), json{{"klippy_state", "ready"}});
    f.poll();
    reply(f.shared, f.last_sent(), json{{"status", {{"heaters", {{"available_sensors", json::array()}}}}}});
    f.poll();
    reply(f.shared, f.last_sent(), json{{"status", json::object()}});
    f.poll();
    CHECK(f.session.session_state() == SessionState::Ready);

    // The original request's late timeout must not mark the recovered Klippy unresponsive.
    f.poll(10000ms);
    CHECK(timed_out);
    CHECK(!f.session.klippy_unresponsive());
    if (f.last_method() == "server.info") {
        reply(f.shared, f.last_sent(), json{{"klippy_state", "ready"}});
        f.poll();
    }
    CHECK(f.session.session_state() == SessionState::Ready);
    CHECK(!f.session.klippy_unresponsive());
}

TEST_CASE("session: status update queued before a restart does not hide its timeout") {
    Fixture f;
    f.reach_ready();

    // The update arrives before the restart is sent but is handled after it.
    notify(f.shared, "notify_status_update", json::array({{{"extruder", {{"temperature", 26.0}}}}, 2.0}));
    f.session.restart();
    CHECK_EQ(f.last_method(), std::string("printer.restart"));
    f.poll(1ms);
    CHECK(!f.session.klippy_unresponsive());

    // Klippy hangs after that update: the restart's timeout must still count.
    f.poll(9999ms);
    CHECK(f.session.klippy_unresponsive());
    if (f.last_method() == "server.info") {
        reply(f.shared, f.last_sent(), json{{"klippy_state", "ready"}});
        f.poll();
    }
    CHECK(f.session.klippy_unresponsive());
}

TEST_CASE("session: service restart targets the klipper instance behind this moonraker") {
    Fixture f;
    f.reach_ready(json::array({"extruder"}), "klipper-2");
    f.session.restart();
    f.poll(10000ms);
    CHECK(f.session.klippy_unresponsive());

    f.session.restart_klipper_service();
    CHECK_EQ(f.last_method(), std::string("machine.services.restart"));
    CHECK_EQ(f.last_sent()["params"]["service"].get<std::string>(), std::string("klipper-2"));
}

TEST_CASE("session: unknown klipper service is never restarted") {
    Fixture f;
    f.reach_ready(json::array({"extruder"}), "");
    f.session.restart();
    f.poll(10000ms);
    CHECK(f.session.klippy_unresponsive());
    CHECK(f.session.klipper_service().empty());

    const auto sent_before = f.shared.sent.size();
    bool failed = false;
    f.session.restart_klipper_service([&](const moonraker::RpcResponse& response) {
        failed = !response.ok;
    });
    CHECK(failed);
    CHECK_EQ(f.shared.sent.size(), sent_before);
    CHECK(f.last_sent_of("machine.services.restart").is_null());
}

TEST_CASE("session: service lookup of an earlier connection is dropped on reconnect") {
    Fixture f;
    f.reach_ready(json::array({"extruder"}), "klipper-2");
    inject(f.shared, TransportEventType::Closed, "gone");
    f.poll();
    CHECK(f.session.klipper_service().empty());
}

TEST_CASE("session: klipper service is asked only after klippy is identified") {
    Fixture f;
    f.connect_and_identify();
    // While Moonraker says "disconnected" or "startup" it may not have looked up the service yet.
    reply(f.shared, f.last_sent(), json{{"klippy_state", "disconnected"}});
    f.poll();
    f.poll(2000ms);
    reply(f.shared, f.last_sent(), json{{"klippy_state", "startup"}});
    f.poll();
    CHECK(f.last_sent_of(kServiceLookup).is_null());
    CHECK(f.session.klipper_service().empty());

    // Ready: Klippy is identified.
    f.poll(2000ms);
    f.finish_ready();
    f.answer_service_lookup("klipper-2");
    CHECK_EQ(f.session.klipper_service(), std::string("klipper-2"));
}

TEST_CASE("session: klipper service is asked when klippy reports an error") {
    Fixture f;
    f.connect_and_identify();
    reply(f.shared, f.last_sent(), json{{"klippy_state", "shutdown"}});
    f.poll();
    CHECK(f.session.session_state() == SessionState::KlippyError);
    CHECK(!f.last_sent_of(kServiceLookup).is_null());
    // printer.info still goes out last, for the error message.
    CHECK_EQ(f.last_method(), std::string("printer.info"));
    f.answer_service_lookup("klipper-2");
    CHECK_EQ(f.session.klipper_service(), std::string("klipper-2"));
}

TEST_CASE("session: verified service is kept while klippy stays disconnected") {
    Fixture f;
    f.reach_ready(json::array({"extruder"}), "klipper-2");

    // Klippy stops and only Moonraker answers: the service restart must still be possible.
    notify(f.shared, "notify_klippy_disconnected", json());
    f.poll();
    for (int i = 0; i < 16; ++i) {
        f.poll(2000ms);
        reply(f.shared, f.last_sent(), json{{"klippy_state", "disconnected"}});
        f.poll();
    }
    CHECK(f.session.klippy_unresponsive());
    CHECK_EQ(f.session.klipper_service(), std::string("klipper-2"));

    f.session.restart_klipper_service();
    CHECK_EQ(f.last_method(), std::string("machine.services.restart"));
    CHECK_EQ(f.last_sent()["params"]["service"].get<std::string>(), std::string("klipper-2"));
}

TEST_CASE("session: failed service lookup is asked again") {
    Fixture f;
    f.connect_and_identify();
    f.finish_ready();
    reply_error(f.shared, f.last_sent(), 503, "Service unavailable");
    f.poll();
    CHECK(f.session.klipper_service().empty());

    const auto sent_before = f.shared.sent.size();
    f.poll(9999ms);
    CHECK(f.last_sent_of(kServiceLookup) == f.shared.sent[sent_before - 1]);
    f.poll(1ms);
    const auto retry = f.last_sent_of(kServiceLookup);
    CHECK(retry != f.shared.sent[sent_before - 1]);
    f.answer_service_lookup("klipper-2");
    CHECK_EQ(f.session.klipper_service(), std::string("klipper-2"));
}

TEST_CASE("session: unanswered service lookup is asked again") {
    Fixture f;
    f.connect_and_identify();
    f.finish_ready();
    const auto first = f.last_sent();

    // The request times out (Moonraker answers the liveness probe meanwhile).
    f.poll(10000ms);
    if (f.last_method() == "server.info") {
        reply(f.shared, f.last_sent(), json{{"klippy_state", "ready"}});
        f.poll();
    }
    CHECK(f.session.klipper_service().empty());
    f.poll(10000ms);
    const auto retry = f.last_sent_of(kServiceLookup);
    CHECK(retry != first);
    f.answer_service_lookup("klipper-2");
    CHECK_EQ(f.session.klipper_service(), std::string("klipper-2"));
}

TEST_CASE("session: failed service lookup is retried after klippy disconnects") {
    // Ready, the lookup fails (error or no answer), and Klippy goes away before the retry.
    for (const bool time_out : {false, true}) {
        for (const char* waiting_state : {"startup", "disconnected"}) {
            Fixture f;
            f.connect_and_identify();
            f.finish_ready();
            if (time_out) {
                f.poll(10000ms);
                if (f.last_method() == "server.info") {
                    reply(f.shared, f.last_sent(), json{{"klippy_state", "ready"}});
                    f.poll();
                }
            }
            else {
                reply_error(f.shared, f.last_sent(), 503, "Service unavailable");
                f.poll();
            }
            CHECK(f.session.klipper_service().empty());
            const auto failed = f.last_sent_of(kServiceLookup);

            notify(f.shared, "notify_klippy_disconnected", json());
            f.poll();
            CHECK(f.session.session_state() == SessionState::WaitingKlippy);

            // Moonraker keeps answering while Klippy stays away; the lookup is asked again.
            bool asked_again = false;
            for (int i = 0; i < 30 && !asked_again; ++i) {
                f.poll(2000ms);
                if (f.last_method() == "server.info") {
                    reply(f.shared, f.last_sent(), json{{"klippy_state", waiting_state}});
                    f.poll();
                }
                asked_again = f.last_sent_of(kServiceLookup) != failed;
            }
            CHECK(asked_again);
            f.answer_service_lookup("klipper-2");
            CHECK_EQ(f.session.klipper_service(), std::string("klipper-2"));
        }
    }
}

TEST_CASE("session: service info Moonraker failed to look up leaves the service unknown") {
    Fixture f;
    f.connect_and_identify();
    f.finish_ready();
    reply_error(f.shared, f.last_sent(), 503, "Service unavailable");
    f.poll();

    // Klippy reconnects and Moonraker's own lookup fails: it stores empty service info (and
    // machine.system_info would now report a default "klipper").
    notify(f.shared, "notify_klippy_disconnected", json());
    f.poll();
    // Moonraker keeps answering server.info with "startup".
    const auto wait = [&f](std::chrono::milliseconds step) {
        f.poll(step);
        reply(f.shared, f.last_sent_of("server.info"), json{{"klippy_state", "startup"}});
        f.poll();
    };
    const auto failed_lookup = f.last_sent_of(kServiceLookup);
    for (int i = 0; i < 30 && f.last_sent_of(kServiceLookup) == failed_lookup; ++i) {
        wait(2000ms);
    }
    CHECK(f.last_sent_of(kServiceLookup) != failed_lookup);
    f.answer_service_lookup("");
    CHECK(f.session.klipper_service().empty());

    // Stuck: no service restart is sent for an unverified name.
    for (int i = 0; i < 16; ++i) {
        wait(2000ms);
    }
    CHECK(f.session.klippy_unresponsive());
    bool failed = false;
    f.session.restart_klipper_service([&](const moonraker::RpcResponse& response) {
        failed = !response.ok;
    });
    CHECK(failed);
    CHECK(f.last_sent_of("machine.services.restart").is_null());

    // Once Moonraker has the real unit, the next retry picks it up.
    const auto unknown_lookup = f.last_sent_of(kServiceLookup);
    for (int i = 0; i < 30 && f.last_sent_of(kServiceLookup) == unknown_lookup; ++i) {
        wait(2000ms);
    }
    f.answer_service_lookup("klipper-2");
    CHECK_EQ(f.session.klipper_service(), std::string("klipper-2"));
}

TEST_CASE("session: stored unit name without a suffix is used as is") {
    Fixture f;
    f.connect_and_identify();
    f.finish_ready();
    reply(f.shared, f.last_sent(), json{{"value", {{"unit_name", "klipper-2"}}}});
    f.poll();
    CHECK_EQ(f.session.klipper_service(), std::string("klipper-2"));
}

TEST_CASE("session: service lookup reply from an earlier connection is dropped") {
    Fixture f;
    f.connect_and_identify();
    f.finish_ready();
    const auto stale = f.last_sent();

    inject(f.shared, TransportEventType::Closed, "gone");
    f.poll();
    f.poll(3000ms);
    inject(f.shared, TransportEventType::Opened);
    f.poll();
    reply(f.shared, stale, Fixture::stored_service("klipper"));
    f.poll();
    CHECK(f.session.klipper_service().empty());
}

TEST_CASE("session: malformed service lookup leaves the service unknown") {
    const json replies[] = {
        json{{"value", nullptr}},
        json{{"value", {{"unit_name", 2}}}},
        json{{"value", {{"unit_name", ""}}}},
        json{{"value", {{"unit_name", ".service"}}}},
        json::array(),
        json("ok"),
    };
    for (const auto& result : replies) {
        Fixture f;
        f.connect_and_identify();
        f.finish_ready();
        reply(f.shared, f.last_sent(), result);
        f.poll();
        CHECK(f.session.klipper_service().empty());
        CHECK(f.session.session_state() == SessionState::Ready);
    }
}

TEST_CASE("session: an answered restart makes earlier restart timeouts stale") {
    Fixture f;
    f.reach_ready();

    f.session.restart();
    f.poll(1000ms);
    f.session.restart();
    reply(f.shared, f.last_sent(), json("ok"));
    f.poll();
    CHECK(!f.session.klippy_unresponsive());

    // Only the first request times out; Klippy answered the second, so it is alive.
    f.poll(9000ms);
    CHECK(!f.session.klippy_unresponsive());
}

TEST_CASE("session: an answered restart clears an earlier timeout") {
    Fixture f;
    f.reach_ready();

    f.session.restart();
    f.poll(1000ms);
    f.session.restart();
    const auto second = f.last_sent();
    f.poll(9000ms);
    CHECK(f.session.klippy_unresponsive());

    reply(f.shared, second, json("ok"));
    f.poll();
    CHECK(!f.session.klippy_unresponsive());
}

TEST_CASE("session: an answered restart keeps watching later requests") {
    Fixture f;
    f.reach_ready();

    f.session.restart();
    reply(f.shared, f.last_sent(), json("ok"));
    f.poll();

    // A request sent after that answer still counts when it times out.
    f.session.restart();
    f.poll(10000ms);
    CHECK(f.session.klippy_unresponsive());
}

TEST_CASE("session: an older answered restart keeps a newer failure") {
    Fixture f;
    f.reach_ready();

    f.session.restart();
    const auto older = f.last_sent();
    f.shared.send_ok = false;
    f.session.restart();
    f.poll();
    f.shared.send_ok = true;
    CHECK(f.session.klippy_unresponsive());

    // The older request's answer does not tell anything about the newer one.
    reply(f.shared, older, json("ok"));
    f.poll();
    CHECK(f.session.klippy_unresponsive());
}

TEST_CASE("session: status update after an unanswered restart clears unresponsive") {
    Fixture f;
    f.reach_ready();
    f.session.firmware_restart();
    f.poll(10000ms);
    CHECK(f.session.klippy_unresponsive());

    notify(f.shared, "notify_status_update", json::array({{{"extruder", {{"temperature", 26.0}}}}, 2.0}));
    f.poll();
    CHECK(!f.session.klippy_unresponsive());
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

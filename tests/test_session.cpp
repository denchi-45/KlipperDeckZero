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

    void reach_ready() {
        connect_and_identify();
        reply(shared, last_sent(), json{{"klippy_state", "ready"}});
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

    notify(f.shared, "notify_filelist_changed", json::array({{{"action", "create_file"}}}));
    f.poll();
    CHECK(f.session.take_filelist_changed());
    CHECK(!f.session.take_filelist_changed());
}

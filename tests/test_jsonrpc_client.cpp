/*
 * SPDX-License-Identifier: MIT
 */

#include "fake_transport.h"
#include "jsonrpc_client.h"
#include "test_framework.h"

#include <chrono>

using moonraker::Clock;
using moonraker::JsonRpcClient;
using moonraker::RpcResponse;
using moonraker::TransportEventType;
using nlohmann::json;
using namespace std::chrono_literals;

namespace {

struct Fixture {
    FakeTransport::Shared shared;
    JsonRpcClient client{std::make_unique<FakeTransport>(shared)};
    Clock::time_point now{};

    void connect() {
        client.open();
        inject(shared, TransportEventType::Opened);
        client.poll(now);
    }
};

} // namespace

TEST_CASE("jsonrpc: request is serialized and response routed by id") {
    Fixture f;
    f.connect();
    CHECK(f.client.connected());

    RpcResponse received;
    int calls = 0;
    f.client.call("printer.objects.query",
                  json{{"objects", {{"extruder", nullptr}}}},
                  [&](const RpcResponse& r) {
                      received = r;
                      ++calls;
                  });

    CHECK_EQ(f.shared.sent.size(), std::size_t{1});
    const auto request = f.shared.sent[0];
    CHECK_EQ(request["jsonrpc"].get<std::string>(), std::string("2.0"));
    CHECK_EQ(request["method"].get<std::string>(), std::string("printer.objects.query"));
    CHECK(request.contains("params"));

    reply(f.shared, request, json{{"status", json::object()}});
    CHECK_EQ(calls, 0);  // only dispatched from poll()
    f.client.poll(f.now);
    CHECK_EQ(calls, 1);
    CHECK(received.ok);
    CHECK_EQ(f.client.pending_requests(), std::size_t{0});
}

TEST_CASE("jsonrpc: requests without params omit the field") {
    Fixture f;
    f.connect();
    f.client.call("server.info", [](const RpcResponse&) {});
    CHECK(!f.shared.sent[0].contains("params"));
}

TEST_CASE("jsonrpc: error response") {
    Fixture f;
    f.connect();
    RpcResponse received;
    f.client.call("printer.gcode.script", json{{"script", "G1 E10"}}, [&](const RpcResponse& r) {
        received = r;
    });
    inject(f.shared,
           TransportEventType::Message,
           json{{"jsonrpc", "2.0"},
                {"id", f.shared.sent[0]["id"]},
                {"error", {{"code", 400}, {"message", "Extrude below minimum temp"}}}}
               .dump());
    f.client.poll(f.now);
    CHECK(!received.ok);
    CHECK(!received.local_error);
    CHECK_EQ(received.error.code, 400);
    CHECK_EQ(received.error.message, std::string("Extrude below minimum temp"));
}

TEST_CASE("jsonrpc: server error reusing a local code is not marked local") {
    Fixture f;
    f.connect();
    RpcResponse received;
    f.client.call("server.info", [&](const RpcResponse& r) { received = r; });
    reply_error(f.shared, f.shared.sent[0], moonraker::kErrorTimeout, "server-side timeout");
    f.client.poll(f.now);
    CHECK(!received.ok);
    CHECK(!received.local_error);
    CHECK_EQ(received.error.code, moonraker::kErrorTimeout);
    CHECK_EQ(received.error.message, std::string("server-side timeout"));
}

TEST_CASE("jsonrpc: timeout") {
    Fixture f;
    f.connect();
    RpcResponse received;
    int calls = 0;
    f.client.call(
        "server.info", json(), [&](const RpcResponse& r) {
            received = r;
            ++calls;
        },
        1000ms);
    f.client.poll(f.now + 999ms);
    CHECK_EQ(calls, 0);
    f.client.poll(f.now + 1000ms);
    CHECK_EQ(calls, 1);
    CHECK(!received.ok);
    CHECK(received.local_error);
    CHECK_EQ(received.error.code, moonraker::kErrorTimeout);
}

TEST_CASE("jsonrpc: disconnect fails pending requests and calls while offline") {
    Fixture f;
    f.connect();
    int connection_events = 0;
    bool last_connected = true;
    f.client.set_connection_handler([&](bool connected, const std::string&) {
        last_connected = connected;
        ++connection_events;
    });

    RpcResponse first;
    f.client.call("server.info", [&](const RpcResponse& r) { first = r; });
    inject(f.shared, TransportEventType::Closed, "boom");
    f.client.poll(f.now);
    CHECK(!f.client.connected());
    CHECK(!last_connected);
    CHECK_EQ(connection_events, 1);
    CHECK(first.local_error);
    CHECK_EQ(first.error.code, moonraker::kErrorDisconnected);

    RpcResponse offline;
    int offline_calls = 0;
    f.client.call("server.info", [&](const RpcResponse& r) {
        offline = r;
        ++offline_calls;
    });
    CHECK_EQ(offline_calls, 0);  // never re-entrant
    f.client.poll(f.now);
    CHECK_EQ(offline_calls, 1);
    CHECK(offline.local_error);
    CHECK_EQ(offline.error.code, moonraker::kErrorDisconnected);
}

TEST_CASE("jsonrpc: send failure is reported") {
    Fixture f;
    f.connect();
    f.shared.send_ok = false;
    RpcResponse received;
    f.client.call("server.info", [&](const RpcResponse& r) { received = r; });
    f.client.poll(f.now);
    CHECK(received.local_error);
    CHECK_EQ(received.error.code, moonraker::kErrorSendFailed);
}

TEST_CASE("jsonrpc: notifications and malformed input") {
    Fixture f;
    f.connect();
    std::string method;
    json params;
    f.client.set_notification_handler([&](const std::string& m, const json& p) {
        method = m;
        params = p;
    });
    inject(f.shared, TransportEventType::Message, "{not json");
    inject(f.shared, TransportEventType::Message, "[1,2,3]");
    notify(f.shared, "notify_gcode_response", json::array({"// hello"}));
    f.client.poll(f.now);
    CHECK_EQ(method, std::string("notify_gcode_response"));
    CHECK_EQ(params[0].get<std::string>(), std::string("// hello"));
}

TEST_CASE("jsonrpc: last_message_at tracks incoming frames by poll time") {
    Fixture f;
    f.connect();
    CHECK(f.client.last_message_at() == f.now);

    f.client.poll(f.now + 5000ms);
    CHECK(f.client.last_message_at() == f.now);  // silence does not count

    notify(f.shared, "notify_status_update", json::array());
    f.client.poll(f.now + 7000ms);
    CHECK(f.client.last_message_at() == f.now + 7000ms);
}

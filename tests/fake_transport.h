/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "transport.h"

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

// In-memory transport: records sent frames and lets tests inject events.
class FakeTransport final : public moonraker::Transport {
public:
    struct Shared {
        moonraker::Transport::EventHandler handler;
        std::vector<nlohmann::json> sent;
        int open_calls{0};
        int close_calls{0};
        bool send_ok{true};
    };

    explicit FakeTransport(Shared& shared) : shared_(shared) {}

    void set_event_handler(EventHandler handler) override {
        shared_.handler = std::move(handler);
    }
    void open() override {
        ++shared_.open_calls;
    }
    void close() override {
        ++shared_.close_calls;
    }
    bool send(const std::string& text) override {
        if (!shared_.send_ok) {
            return false;
        }
        shared_.sent.push_back(nlohmann::json::parse(text));
        return true;
    }
    std::string describe() const override {
        return "fake";
    }

private:
    Shared& shared_;
};

inline void inject(FakeTransport::Shared& shared,
                   moonraker::TransportEventType type,
                   const std::string& payload = {}) {
    shared.handler(type, payload);
}

inline void reply(FakeTransport::Shared& shared,
                  const nlohmann::json& request,
                  const nlohmann::json& result) {
    inject(shared,
           moonraker::TransportEventType::Message,
           nlohmann::json{{"jsonrpc", "2.0"}, {"id", request["id"]}, {"result", result}}.dump());
}

inline void notify(FakeTransport::Shared& shared,
                   const std::string& method,
                   const nlohmann::json& params) {
    inject(shared,
           moonraker::TransportEventType::Message,
           nlohmann::json{{"jsonrpc", "2.0"}, {"method", method}, {"params", params}}.dump());
}

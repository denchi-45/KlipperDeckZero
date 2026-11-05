/*
 * SPDX-License-Identifier: MIT
 */

#include "jsonrpc_client.h"

#include <utility>
#include <vector>

namespace moonraker {

JsonRpcClient::JsonRpcClient(std::unique_ptr<Transport> transport)
    : transport_(std::move(transport)) {
    if (transport_) {
        transport_->set_event_handler([this](TransportEventType type, std::string payload) {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            queue_.push_back({type, std::move(payload)});
        });
    }
}

JsonRpcClient::~JsonRpcClient() {
    if (transport_) {
        transport_->close();
        transport_->set_event_handler(nullptr);
    }
}

void JsonRpcClient::set_notification_handler(NotificationHandler handler) {
    notification_handler_ = std::move(handler);
}

void JsonRpcClient::set_connection_handler(ConnectionHandler handler) {
    connection_handler_ = std::move(handler);
}

void JsonRpcClient::open() {
    if (!transport_) {
        return;
    }
    {
        // Drop anything left over from a previous connection.
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_.clear();
    }
    transport_->open();
}

void JsonRpcClient::close() {
    if (transport_) {
        transport_->close();
    }
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        queue_.clear();
    }
    const bool was_connected = connected_;
    connected_ = false;
    fail_all_pending(kErrorDisconnected, "connection closed");
    if (was_connected && connection_handler_) {
        connection_handler_(false, "closed by client");
    }
}

bool JsonRpcClient::connected() const {
    return connected_;
}

std::string JsonRpcClient::describe() const {
    return transport_ ? transport_->describe() : std::string("(no transport)");
}

uint64_t JsonRpcClient::call(const std::string& method,
                             Json params,
                             ResponseHandler handler,
                             std::chrono::milliseconds timeout) {
    const uint64_t id = next_id_++;

    Json request = {
        {"jsonrpc", "2.0"},
        {"method", method},
        {"id", id},
    };
    if (!params.is_null()) {
        request["params"] = std::move(params);
    }

    // Use the last poll time as "now" so tests with a synthetic clock stay deterministic.
    auto& pending = pending_[id];
    pending = PendingRequest{method, std::move(handler), last_poll_ + timeout};

    if (!connected_ || !transport_) {
        pending.failure_code = kErrorDisconnected;
    }
    else if (!transport_->send(request.dump())) {
        pending.failure_code = kErrorSendFailed;
    }
    if (pending.failure_code != 0) {
        // Reported from the next poll() rather than re-entrantly from call().
        pending.deadline = Clock::time_point::min();
    }
    return id;
}

void JsonRpcClient::poll(Clock::time_point now) {
    last_poll_ = now;

    std::deque<QueuedEvent> events;
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        events.swap(queue_);
    }

    for (auto& event : events) {
        switch (event.type) {
            case TransportEventType::Opened:
                connected_ = true;
                last_message_at_ = now;
                if (connection_handler_) {
                    connection_handler_(true, {});
                }
                break;
            case TransportEventType::Closed:
                connected_ = false;
                fail_all_pending(kErrorDisconnected, "disconnected: " + event.payload);
                if (connection_handler_) {
                    connection_handler_(false, event.payload);
                }
                break;
            case TransportEventType::Message:
                last_message_at_ = now;
                handle_message(event.payload);
                break;
        }
    }

    std::vector<uint64_t> expired;
    for (const auto& [id, request] : pending_) {
        if (request.deadline <= now) {
            expired.push_back(id);
        }
    }
    for (const auto id : expired) {
        auto it = pending_.find(id);
        if (it == pending_.end()) {
            continue;
        }
        RpcResponse response;
        response.local_error = true;
        if (it->second.failure_code == kErrorSendFailed) {
            response.error = {kErrorSendFailed, "send failed: " + it->second.method};
        }
        else if (it->second.failure_code == kErrorDisconnected) {
            response.error = {kErrorDisconnected, "not connected: " + it->second.method};
        }
        else {
            response.error = {kErrorTimeout, "request timed out: " + it->second.method};
        }
        complete(id, response);
    }
}

std::size_t JsonRpcClient::pending_requests() const {
    return pending_.size();
}

Clock::time_point JsonRpcClient::last_message_at() const {
    return last_message_at_;
}

void JsonRpcClient::handle_message(const std::string& text) {
    const Json message = Json::parse(text, nullptr, false);
    if (message.is_discarded() || !message.is_object()) {
        return;
    }

    const auto id_it = message.find("id");
    if (id_it != message.end() && id_it->is_number_unsigned()) {
        RpcResponse response;
        if (const auto error_it = message.find("error"); error_it != message.end()) {
            response.ok = false;
            response.error.code = error_it->value("code", 0);
            response.error.message = error_it->value("message", std::string("unknown error"));
        }
        else {
            response.ok = true;
            response.result = message.value("result", Json());
        }
        complete(id_it->get<uint64_t>(), response);
        return;
    }

    const auto method_it = message.find("method");
    if (method_it != message.end() && method_it->is_string() && notification_handler_) {
        notification_handler_(method_it->get<std::string>(), message.value("params", Json()));
    }
}

void JsonRpcClient::fail_all_pending(int code, const std::string& message) {
    auto pending = std::move(pending_);
    pending_.clear();
    RpcResponse response;
    response.error = {code, message};
    response.local_error = true;
    for (auto& [id, request] : pending) {
        if (request.handler) {
            request.handler(response);
        }
    }
}

void JsonRpcClient::complete(uint64_t id, const RpcResponse& response) {
    auto it = pending_.find(id);
    if (it == pending_.end()) {
        return;
    }
    auto handler = std::move(it->second.handler);
    pending_.erase(it);
    if (handler) {
        handler(response);
    }
}

} // namespace moonraker

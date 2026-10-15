/*
 * SPDX-License-Identifier: MIT
 */

#include "transport.h"

#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

#include <atomic>
#include <mutex>
#include <utility>

namespace moonraker {
namespace {

void ensure_net_system() {
    static std::once_flag once;
    std::call_once(once, [] { ix::initNetSystem(); });
}

class WebSocketTransport final : public Transport {
public:
    WebSocketTransport(std::string url, std::string api_key)
        : url_(std::move(url)), api_key_(std::move(api_key)) {
        ensure_net_system();
        socket_.setUrl(url_);
        // Reconnection is driven by the session so both transports behave the same.
        socket_.disableAutomaticReconnection();
        socket_.setPingInterval(30);
        if (!api_key_.empty()) {
            socket_.setExtraHeaders({{"X-Api-Key", api_key_}});
        }
        socket_.setOnMessageCallback([this](const ix::WebSocketMessagePtr& message) {
            on_message(message);
        });
    }

    ~WebSocketTransport() override {
        close();
    }

    void set_event_handler(EventHandler handler) override {
        std::lock_guard<std::mutex> lock(handler_mutex_);
        handler_ = std::move(handler);
    }

    void open() override {
        // stop() joins the previous worker thread (if any) before starting a fresh attempt.
        close();
        closed_reported_ = false;
        active_ = true;
        socket_.start();
    }

    void close() override {
        // Events caused by an intentional close are not reported.
        active_ = false;
        socket_.stop();
    }

    bool send(const std::string& text) override {
        return socket_.sendText(text).success;
    }

    std::string describe() const override {
        return url_;
    }

private:
    void emit(TransportEventType type, std::string payload) {
        std::lock_guard<std::mutex> lock(handler_mutex_);
        if (active_ && handler_) {
            handler_(type, std::move(payload));
        }
    }

    void on_message(const ix::WebSocketMessagePtr& message) {
        switch (message->type) {
            case ix::WebSocketMessageType::Open:
                emit(TransportEventType::Opened, {});
                break;
            case ix::WebSocketMessageType::Message:
                if (!message->binary) {
                    emit(TransportEventType::Message, message->str);
                }
                break;
            case ix::WebSocketMessageType::Close:
                report_closed(message->closeInfo.reason.empty() ? "connection closed"
                                                                : message->closeInfo.reason);
                break;
            case ix::WebSocketMessageType::Error:
                report_closed(message->errorInfo.reason.empty() ? "connection error"
                                                                : message->errorInfo.reason);
                break;
            default:
                break;
        }
    }

    void report_closed(std::string reason) {
        // IXWebSocket may report Error followed by Close; surface only the first.
        if (closed_reported_.exchange(true)) {
            return;
        }
        emit(TransportEventType::Closed, std::move(reason));
    }

    std::string url_;
    std::string api_key_;
    ix::WebSocket socket_;
    std::mutex handler_mutex_;
    EventHandler handler_;
    std::atomic<bool> active_{false};
    std::atomic<bool> closed_reported_{false};
};

} // namespace

std::unique_ptr<Transport> create_websocket_transport(const std::string& url,
                                                      const std::string& api_key) {
    return std::make_unique<WebSocketTransport>(url, api_key);
}

} // namespace moonraker

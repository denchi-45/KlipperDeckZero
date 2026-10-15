/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <functional>
#include <memory>
#include <string>

namespace moonraker {

enum class TransportEventType {
    Opened,
    Closed,   // connection lost or connect attempt failed; payload holds the reason
    Message,  // one complete JSON-RPC text frame
};

// Byte-stream/WebSocket connection to Moonraker carrying JSON-RPC text messages.
//
// Implementations may invoke the event handler from a background thread.
// open() / close() / send() are called from the owning (UI) thread.
// After a Closed event the transport is idle; call open() again to reconnect.
class Transport {
public:
    using EventHandler = std::function<void(TransportEventType type, std::string payload)>;

    virtual ~Transport() = default;

    virtual void set_event_handler(EventHandler handler) = 0;
    virtual void open() = 0;
    virtual void close() = 0;
    virtual bool send(const std::string& text) = 0;
    virtual std::string describe() const = 0;
};

// ws://host:7125/websocket. api_key is sent as X-Api-Key when not empty.
std::unique_ptr<Transport> create_websocket_transport(const std::string& url,
                                                      const std::string& api_key = {});

// Moonraker unix socket (messages delimited by ETX 0x03). Unsupported on Windows:
// returns a transport whose open() always fails with an explanatory reason.
std::unique_ptr<Transport> create_unix_socket_transport(const std::string& socket_path);

} // namespace moonraker

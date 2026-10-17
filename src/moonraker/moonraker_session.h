/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "jsonrpc_client.h"
#include "printer_state.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>

namespace moonraker {

enum class SessionState {
    Stopped = 0,
    Connecting,     // waiting for the transport to open
    Initializing,   // identify / server.info / subscribe in flight
    WaitingKlippy,  // Moonraker is up but Klippy is starting or disconnected
    KlippyError,    // Klippy reported error or shutdown (see PrinterState::state_message)
    Ready,          // subscribed; PrinterState is live
};

const char* to_string(SessionState state);

struct SessionOptions {
    std::string client_name{"KlipperDeckZero"};
    std::string client_version{"0.0.0"};
    std::string client_url{"https://github.com/denchi-45/KlipperDeckZero"};
    std::string api_key;
    std::chrono::milliseconds reconnect_delay{3000};
    std::chrono::milliseconds klippy_retry_delay{2000};
    std::chrono::milliseconds gcode_timeout{120000};
};

// Drives the Moonraker startup sequence and keeps a merged PrinterState.
// Everything (including callbacks) runs on the thread that calls poll().
class MoonrakerSession {
public:
    using ChangeListener = std::function<void()>;
    using ResultHandler = std::function<void(const RpcResponse& response)>;

    MoonrakerSession(std::unique_ptr<Transport> transport, SessionOptions options);

    void start();
    void stop();
    void poll(Clock::time_point now = Clock::now());

    SessionState session_state() const;
    const model::PrinterState& printer() const;
    // Incremented whenever session_state() or printer() changes.
    uint32_t revision() const;
    const std::string& last_error() const;
    std::string endpoint() const;

    // Called from poll() after any change to state / printer data.
    void set_change_listener(ChangeListener listener);

    // Console lines from notify_gcode_response ("// info", "!! error"), oldest first.
    std::deque<std::string> take_gcode_responses();
    // True once after notify_filelist_changed.
    bool take_filelist_changed();

    // Operations (handlers optional).
    void send_gcode(const std::string& script, ResultHandler handler = {});
    void print_start(const std::string& filename, ResultHandler handler = {});
    void print_pause(ResultHandler handler = {});
    void print_resume(ResultHandler handler = {});
    void print_cancel(ResultHandler handler = {});
    void emergency_stop(ResultHandler handler = {});
    // RESTART: reloads the config and restarts the Klipper host software.
    void restart(ResultHandler handler = {});
    // FIRMWARE_RESTART: also resets the MCUs, which recovers an MCU shutdown.
    void firmware_restart(ResultHandler handler = {});
    void call(const std::string& method, Json params, ResultHandler handler = {},
              std::chrono::milliseconds timeout = JsonRpcClient::kDefaultTimeout);

private:
    void on_connection(bool connected, const std::string& reason);
    void on_notification(const std::string& method, const Json& params);
    void identify();
    void query_server_info();
    void query_printer_info();
    void subscribe();
    void schedule_server_info(std::chrono::milliseconds delay);
    void set_state(SessionState state);
    void set_error(const std::string& message);
    void mark_changed();

    JsonRpcClient client_;
    SessionOptions options_;
    SessionState state_{SessionState::Stopped};
    model::PrinterState printer_;
    uint32_t revision_{0};
    bool dirty_{false};
    std::string last_error_;
    ChangeListener change_listener_;
    std::deque<std::string> gcode_responses_;
    bool filelist_changed_{false};

    Clock::time_point now_{Clock::now()};
    bool reconnect_scheduled_{false};
    Clock::time_point reconnect_at_{};
    bool server_info_scheduled_{false};
    Clock::time_point server_info_at_{};
    // Bumped on every (re)connection so responses from an old connection are ignored.
    uint32_t connection_epoch_{0};
};

} // namespace moonraker

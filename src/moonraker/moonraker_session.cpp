/*
 * SPDX-License-Identifier: MIT
 */

#include "moonraker_session.h"

#include "status_merge.h"

#include <algorithm>
#include <utility>

namespace moonraker {

namespace {
constexpr std::size_t kMaxGcodeResponses = 32;
} // namespace

// Errors produced locally when the server did not answer at all (as opposed to a server error).
// Server error replies may reuse these codes; only a locally generated failure means no answer.
bool is_no_response(const RpcResponse& response) {
    return response.local_error &&
           (response.error.code == kErrorTimeout || response.error.code == kErrorSendFailed);
}

const char* to_string(SessionState state) {
    switch (state) {
        case SessionState::Stopped:
            return "stopped";
        case SessionState::Connecting:
            return "connecting";
        case SessionState::Initializing:
            return "initializing";
        case SessionState::WaitingKlippy:
            return "waiting_klippy";
        case SessionState::KlippyError:
            return "klippy_error";
        case SessionState::Ready:
            return "ready";
    }
    return "unknown";
}

MoonrakerSession::MoonrakerSession(std::unique_ptr<Transport> transport, SessionOptions options)
    : client_(std::move(transport)), options_(std::move(options)) {
    client_.set_connection_handler([this](bool connected, const std::string& reason) {
        on_connection(connected, reason);
    });
    client_.set_notification_handler([this](const std::string& method, const Json& params) {
        on_notification(method, params);
    });
}

void MoonrakerSession::start() {
    if (state_ != SessionState::Stopped) {
        return;
    }
    reconnect_scheduled_ = false;
    server_info_scheduled_ = false;
    probe_pending_ = false;
    liveness_failed_ = false;
    set_state(SessionState::Connecting);
    client_.open();
}

void MoonrakerSession::stop() {
    reconnect_scheduled_ = false;
    server_info_scheduled_ = false;
    probe_pending_ = false;
    liveness_failed_ = false;
    ++connection_epoch_;
    // Mark stopped first: close() reports the disconnect synchronously.
    set_state(SessionState::Stopped);
    client_.close();
}

void MoonrakerSession::poll(Clock::time_point now) {
    now_ = now;
    client_.poll(now);

    if (liveness_failed_) {
        liveness_failed_ = false;
        force_reconnect("no response from Moonraker (liveness check failed)");
    }
    if (reconnect_scheduled_ && now >= reconnect_at_) {
        reconnect_scheduled_ = false;
        set_state(SessionState::Connecting);
        client_.open();
    }
    if (server_info_scheduled_ && now >= server_info_at_) {
        server_info_scheduled_ = false;
        query_server_info();
    }
    if (system_info_retry_scheduled_ && now >= system_info_retry_at_ && klippy_identified_ &&
        !system_info_pending_ && klipper_service_.empty()) {
        query_system_info();
    }
    if (state_ == SessionState::Ready && !probe_pending_ &&
        now - client_.last_message_at() >= options_.liveness_timeout) {
        probe_connection();
    }
    // The stuck timeout passes without any event, so its change is noticed here.
    if (klippy_unresponsive() != reported_unresponsive_) {
        reported_unresponsive_ = !reported_unresponsive_;
        mark_changed();
    }

    if (dirty_) {
        dirty_ = false;
        ++revision_;
        if (change_listener_) {
            change_listener_();
        }
    }
}

SessionState MoonrakerSession::session_state() const {
    return state_;
}

const model::PrinterState& MoonrakerSession::printer() const {
    return printer_;
}

uint32_t MoonrakerSession::revision() const {
    return revision_;
}

const std::string& MoonrakerSession::last_error() const {
    return last_error_;
}

std::string MoonrakerSession::endpoint() const {
    return client_.describe();
}

void MoonrakerSession::set_change_listener(ChangeListener listener) {
    change_listener_ = std::move(listener);
}

std::deque<std::string> MoonrakerSession::take_gcode_responses() {
    return std::exchange(gcode_responses_, {});
}

bool MoonrakerSession::take_filelist_changed() {
    return std::exchange(filelist_changed_, false);
}

void MoonrakerSession::on_connection(bool connected, const std::string& reason) {
    if (state_ == SessionState::Stopped) {
        return;
    }

    ++connection_epoch_;
    server_info_scheduled_ = false;
    probe_pending_ = false;
    liveness_failed_ = false;
    // The service belongs to the Moonraker we talk to, which may differ after a reconnect.
    system_info_pending_ = false;
    system_info_retry_scheduled_ = false;
    klippy_identified_ = false;
    if (!klipper_service_.empty()) {
        klipper_service_.clear();
        mark_changed();
    }

    if (connected) {
        last_error_.clear();
        set_state(SessionState::Initializing);
        identify();
        return;
    }

    set_error(reason);
    printer_.klippy_state = model::KlippyState::Unknown;
    set_state(SessionState::Connecting);
    reconnect_scheduled_ = true;
    reconnect_at_ = now_ + options_.reconnect_delay;
}

void MoonrakerSession::identify() {
    Json params = {
        {"client_name", options_.client_name},
        {"version", options_.client_version},
        {"type", "display"},
        {"url", options_.client_url},
    };
    if (!options_.api_key.empty()) {
        params["api_key"] = options_.api_key;
    }

    const auto epoch = connection_epoch_;
    client_.call("server.connection.identify", std::move(params), [this, epoch](const RpcResponse& response) {
        if (epoch != connection_epoch_) {
            return;
        }
        if (!response.ok) {
            // Older Moonraker versions or unix sockets without identify support still work.
            set_error("identify failed: " + response.error.message);
        }
        query_server_info();
    });
}

void MoonrakerSession::query_system_info() {
    // A host can run several Klipper instances (klipper, klipper-2, ...); only the one behind
    // this Moonraker may be restarted. Moonraker learns the unit name while it sets up the Klippy
    // connection and reports a default "klipper" before that (also while it says "startup"), so
    // this is first asked once Klippy is ready or has reported an error. From then on Moonraker
    // knows the real unit, so a retry may also run while Klippy is gone again. The name stays
    // valid until the Moonraker connection changes: one Moonraker always serves the same unit.
    // Without a name, the service restart is not offered.
    system_info_pending_ = true;
    system_info_retry_scheduled_ = false;
    const auto epoch = connection_epoch_;
    client_.call("machine.system_info", [this, epoch](const RpcResponse& response) {
        if (epoch != connection_epoch_) {
            return;
        }
        system_info_pending_ = false;
        const auto name = response.ok ? klipper_service_from(response.result) : std::string();
        if (name.empty()) {
            // Timeouts and errors can be transient: ask again later.
            system_info_retry_scheduled_ = true;
            system_info_retry_at_ = now_ + options_.klippy_retry_delay * 5;
            return;
        }
        if (klipper_service_ != name) {
            klipper_service_ = name;
            mark_changed();
        }
    });
}

std::string MoonrakerSession::klipper_service_from(const Json& result) {
    // Anything not shaped like a normal reply leaves the service unknown.
    if (!result.is_object()) {
        return {};
    }
    const auto info = result.find("system_info");
    if (info == result.end() || !info->is_object()) {
        return {};
    }
    const auto ids = info->find("instance_ids");
    if (ids == info->end() || !ids->is_object()) {
        return {};
    }
    const auto klipper = ids->find("klipper");
    if (klipper == ids->end() || !klipper->is_string()) {
        return {};
    }
    return klipper->get<std::string>();
}

void MoonrakerSession::query_server_info() {
    const auto epoch = connection_epoch_;
    client_.call("server.info", [this, epoch](const RpcResponse& response) {
        if (epoch != connection_epoch_) {
            return;
        }
        if (!response.ok) {
            set_error("server.info failed: " + response.error.message);
            if (is_no_response(response)) {
                // Moonraker answers server.info itself even while Klippy is down: the link is dead.
                liveness_failed_ = true;
                return;
            }
            schedule_server_info(options_.klippy_retry_delay);
            return;
        }

        const auto klippy = model::parse_klippy_state(
            response.result.value("klippy_state", std::string("unknown")));
        if (printer_.klippy_state != klippy) {
            printer_.klippy_state = klippy;
            mark_changed();
        }

        switch (klippy) {
            case model::KlippyState::Ready:
                subscribe();
                break;
            case model::KlippyState::Error:
            case model::KlippyState::Shutdown:
                set_state(SessionState::KlippyError);
                query_printer_info();
                // Recovery is announced by notify_klippy_ready; poll slowly as a fallback.
                schedule_server_info(options_.klippy_retry_delay * 5);
                break;
            default:
                set_state(SessionState::WaitingKlippy);
                schedule_server_info(options_.klippy_retry_delay);
                break;
        }
    });
}

void MoonrakerSession::query_printer_info() {
    const auto epoch = connection_epoch_;
    client_.call("printer.info", [this, epoch](const RpcResponse& response) {
        if (epoch != connection_epoch_ || !response.ok) {
            return;
        }
        const auto message = response.result.value("state_message", std::string());
        if (printer_.state_message != message) {
            printer_.state_message = message;
            mark_changed();
        }
    });
}

void MoonrakerSession::subscribe() {
    // Ask which temperature sensors the printer has first, so they join the subscription.
    const auto epoch = connection_epoch_;
    Json params = {{"objects", {{"heaters", {"available_sensors"}}}}};
    client_.call("printer.objects.query", std::move(params), [this, epoch](const RpcResponse& response) {
        if (epoch != connection_epoch_) {
            return;
        }
        // Without the list the printer is still usable; only the extra sensors are missing.
        const auto heaters = response.ok ? response.result.value("status", Json::object()).value("heaters", Json())
                                         : Json();
        subscribe_objects(extra_sensor_objects(heaters));
    });
}

void MoonrakerSession::subscribe_objects(const std::vector<std::string>& sensor_objects) {
    init_sensors(printer_, sensor_objects);
    const auto epoch = connection_epoch_;
    Json params = {{"objects", subscription_objects(sensor_objects)}};
    client_.call("printer.objects.subscribe", std::move(params), [this, epoch](const RpcResponse& response) {
        if (epoch != connection_epoch_) {
            return;
        }
        if (!response.ok) {
            set_error("subscribe failed: " + response.error.message);
            schedule_server_info(options_.klippy_retry_delay);
            return;
        }
        if (apply_status(printer_, response.result.value("status", Json::object()))) {
            mark_changed();
        }
        // The webhooks snapshot may still say "startup" in rare races; trust server.info.
        printer_.klippy_state = model::KlippyState::Ready;
        set_state(SessionState::Ready);
    });
}

void MoonrakerSession::schedule_server_info(std::chrono::milliseconds delay) {
    server_info_scheduled_ = true;
    server_info_at_ = now_ + delay;
}

void MoonrakerSession::probe_connection() {
    probe_pending_ = true;
    const auto epoch = connection_epoch_;
    // Only the reply matters: any incoming frame refreshes the client's last_message_at().
    client_.call("server.info", Json(), [this, epoch](const RpcResponse& response) {
        if (epoch != connection_epoch_) {
            return;
        }
        probe_pending_ = false;
        if (!response.ok && is_no_response(response)) {
            liveness_failed_ = true;
        }
    }, options_.probe_timeout);
}

void MoonrakerSession::force_reconnect(const std::string& reason) {
    const bool was_connected = client_.connected();
    // When connected, close() reports the disconnect synchronously and on_connection schedules the reconnect.
    client_.close();
    if (!was_connected) {
        on_connection(false, reason);
    }
    // Replace close()'s generic "closed by client" with the actual cause.
    set_error(reason);
}

void MoonrakerSession::on_notification(const std::string& method, const Json& params) {
    if (method == "notify_status_update") {
        if (params.is_array() && !params.empty() && apply_status(printer_, params[0])) {
            mark_changed();
        }
        // Status updates come from Klippy, so it is alive again. They settle only the restarts that
        // already went unanswered, not the ones still waiting: the update may have been queued
        // before those were sent.
        if (restart_unanswered()) {
            settle_restarts_through(unanswered_through_);
        }
        return;
    }

    if (method == "notify_gcode_response") {
        if (params.is_array()) {
            for (const auto& line : params) {
                if (line.is_string()) {
                    gcode_responses_.push_back(line.get<std::string>());
                }
            }
            while (gcode_responses_.size() > kMaxGcodeResponses) {
                gcode_responses_.pop_front();
            }
            mark_changed();
        }
        return;
    }

    if (method == "notify_klippy_ready") {
        server_info_scheduled_ = false;
        query_server_info();
        return;
    }

    if (method == "notify_klippy_shutdown") {
        printer_.klippy_state = model::KlippyState::Shutdown;
        set_state(SessionState::KlippyError);
        query_printer_info();
        // Recovery is announced by notify_klippy_ready; poll slowly as a fallback.
        schedule_server_info(options_.klippy_retry_delay * 5);
        mark_changed();
        return;
    }

    if (method == "notify_klippy_disconnected") {
        printer_.klippy_state = model::KlippyState::Disconnected;
        set_state(SessionState::WaitingKlippy);
        schedule_server_info(options_.klippy_retry_delay);
        mark_changed();
        return;
    }

    if (method == "notify_filelist_changed") {
        filelist_changed_ = true;
        mark_changed();
    }
}

void MoonrakerSession::send_gcode(const std::string& script, ResultHandler handler) {
    call("printer.gcode.script", {{"script", script}}, std::move(handler), options_.gcode_timeout);
}

void MoonrakerSession::print_start(const std::string& filename, ResultHandler handler) {
    call("printer.print.start", {{"filename", filename}}, std::move(handler));
}

void MoonrakerSession::print_pause(ResultHandler handler) {
    call("printer.print.pause", Json(), std::move(handler));
}

void MoonrakerSession::print_resume(ResultHandler handler) {
    call("printer.print.resume", Json(), std::move(handler));
}

void MoonrakerSession::print_cancel(ResultHandler handler) {
    call("printer.print.cancel", Json(), std::move(handler));
}

void MoonrakerSession::emergency_stop(ResultHandler handler) {
    call("printer.emergency_stop", Json(), std::move(handler));
}

void MoonrakerSession::restart(ResultHandler handler) {
    call("printer.restart", Json(), watch_klippy_restart(std::move(handler)));
}

void MoonrakerSession::firmware_restart(ResultHandler handler) {
    call("printer.firmware_restart", Json(), watch_klippy_restart(std::move(handler)));
}

void MoonrakerSession::restart_klipper_service(ResultHandler handler) {
    // Only a successful restart clears the unresponsive state: after a rejected or unanswered
    // request Klippy is still hung, and the user must be able to try again.
    if (klipper_service_.empty()) {
        if (handler) {
            RpcResponse response;
            response.error.message = "Klipper service name unknown";
            handler(response);
        }
        return;
    }
    const auto epoch = connection_epoch_;
    call("machine.services.restart", {{"service", klipper_service_}},
         [this, epoch, handler = std::move(handler)](const RpcResponse& response) {
             if (epoch == connection_epoch_ && response.ok) {
                 // Give the new Klippy process the full stuck timeout before calling it unresponsive again.
                 clear_klippy_restart_unanswered();
                 waiting_since_ = now_;
                 mark_changed();
             }
             if (handler) {
                 handler(response);
             }
         });
}

bool MoonrakerSession::klippy_unresponsive() const {
    switch (state_) {
        case SessionState::WaitingKlippy:
            return restart_unanswered() || now_ - waiting_since_ >= options_.klippy_stuck_timeout;
        case SessionState::KlippyError:
        case SessionState::Ready:
            // A Klippy that hangs while ready may never send a disconnect / shutdown notification,
            // so an unanswered restart is the only sign of it.
            return restart_unanswered();
        default:
            return false;
    }
}

MoonrakerSession::ResultHandler MoonrakerSession::watch_klippy_restart(ResultHandler handler) {
    // Moonraker forwards these to Klippy; no answer means Klippy (not Moonraker) is stuck,
    // since a dead link is caught by the liveness check instead.
    // Requests are numbered so that a timeout arriving after Klippy has shown it is alive
    // (a later answer, Ready, or a service restart) is recognised as stale.
    const auto epoch = connection_epoch_;
    const auto seq = ++restart_seq_;
    return [this, epoch, seq, handler = std::move(handler)](const RpcResponse& response) {
        if (epoch == connection_epoch_) {
            if (response.ok) {
                // Klippy answered this one: it and earlier requests are settled, later ones are not.
                settle_restarts_through(seq);
            }
            else if (is_no_response(response) && seq > restarts_settled_through_ && seq > unanswered_through_) {
                const bool was_unanswered = restart_unanswered();
                unanswered_through_ = seq;
                if (!was_unanswered) {
                    mark_changed();
                }
            }
        }
        if (handler) {
            handler(response);
        }
    };
}

void MoonrakerSession::call(const std::string& method,
                            Json params,
                            ResultHandler handler,
                            std::chrono::milliseconds timeout) {
    client_.call(method, std::move(params), std::move(handler), timeout);
}

void MoonrakerSession::set_state(SessionState state) {
    if (state_ != state) {
        if (state == SessionState::WaitingKlippy) {
            waiting_since_ = now_;
        }
        // Reaching Ready (or losing Moonraker) means the unanswered restart no longer applies;
        // a stuck Klippy moving between waiting and error keeps it.
        if (state != SessionState::WaitingKlippy && state != SessionState::KlippyError) {
            clear_klippy_restart_unanswered();
        }
        state_ = state;
        mark_changed();
        if (state == SessionState::Ready || state == SessionState::KlippyError) {
            klippy_identified_ = true;
            if (klipper_service_.empty() && !system_info_pending_) {
                query_system_info();
            }
        }
    }
}

void MoonrakerSession::clear_klippy_restart_unanswered() {
    // Also invalidates the restarts still waiting for an answer: Klippy has come back since they were sent.
    settle_restarts_through(restart_seq_);
}

void MoonrakerSession::settle_restarts_through(uint32_t seq) {
    const bool was_unanswered = restart_unanswered();
    restarts_settled_through_ = std::max(restarts_settled_through_, seq);
    if (was_unanswered != restart_unanswered()) {
        mark_changed();
    }
}

void MoonrakerSession::set_error(const std::string& message) {
    if (last_error_ != message) {
        last_error_ = message;
        mark_changed();
    }
}

void MoonrakerSession::mark_changed() {
    dirty_ = true;
}

} // namespace moonraker

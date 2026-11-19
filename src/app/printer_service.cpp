/*
 * SPDX-License-Identifier: MIT
 */

#include "printer_service.h"

#include "logger.h"
#include "transport.h"

#include <utility>

#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif

namespace app {
namespace {

constexpr uint32_t kPollIntervalMs = 50;

std::unique_ptr<moonraker::Transport> create_transport(const MoonrakerConfig& config) {
    if (config.transport == MoonrakerTransportKind::UnixSocket) {
        return moonraker::create_unix_socket_transport(expand_user_path(config.socket_path));
    }
    return moonraker::create_websocket_transport(config.websocket_url(), config.api_key);
}

} // namespace

PrinterService::PrinterService(const MoonrakerConfig& config) {
    moonraker::SessionOptions options;
    options.client_version = APP_VERSION;
    options.api_key = config.api_key;
    session_ = std::make_unique<moonraker::MoonrakerSession>(create_transport(config), std::move(options));
    session_->set_change_listener([this] { on_session_changed(); });
    thumbnails_ = create_thumbnail_source(config, *session_);
    webcams_ = std::make_unique<WebcamSource>(config, *session_);
}

PrinterService::~PrinterService() {
    stop();
}

void PrinterService::start() {
    if (poll_timer_) {
        return;
    }
    LOG_INFO("moonraker: connecting to {}", session_->endpoint());
    session_->start();
    poll_timer_ = lv_timer_create(poll_timer_cb, kPollIntervalMs, this);
}

void PrinterService::stop() {
    if (poll_timer_) {
        lv_timer_delete(poll_timer_);
        poll_timer_ = nullptr;
    }
    session_->stop();
}

moonraker::MoonrakerSession& PrinterService::session() {
    return *session_;
}

ThumbnailSource& PrinterService::thumbnails() {
    return *thumbnails_;
}

WebcamSource& PrinterService::webcams() {
    return *webcams_;
}

void PrinterService::set_change_listener(ChangeListener listener) {
    change_listener_ = std::move(listener);
}

void PrinterService::poll_timer_cb(lv_timer_t* timer) {
    auto* service = static_cast<PrinterService*>(lv_timer_get_user_data(timer));
    if (service) {
        service->session_->poll();
        service->thumbnails_->poll();
        service->webcams_->poll();
    }
}

void PrinterService::on_session_changed() {
    const auto state = session_->session_state();
    const auto& printer = session_->printer();
    if (state != logged_state_) {
        if (state == moonraker::SessionState::Connecting && !session_->last_error().empty()) {
            LOG_WARN("moonraker: {} ({})", moonraker::to_string(state), session_->last_error());
        }
        else {
            LOG_INFO("moonraker: {}", moonraker::to_string(state));
        }
        logged_state_ = state;
    }
    if (printer.klippy_state != logged_klippy_) {
        LOG_INFO("klippy: {} {}", model::to_string(printer.klippy_state), printer.state_message);
        logged_klippy_ = printer.klippy_state;
    }
    if (printer.print_state != logged_print_) {
        LOG_INFO("print: {} {}", model::to_string(printer.print_state), printer.filename);
        logged_print_ = printer.print_state;
    }

    if (change_listener_) {
        change_listener_(*session_);
    }
}

} // namespace app

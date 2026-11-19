/*
 * SPDX-License-Identifier: MIT
 */

#include "webcam_source.h"

#include "application_config.h"
#include "moonraker_session.h"

#include <utility>

namespace app {

WebcamSource::WebcamSource(const MoonrakerConfig& config, moonraker::MoonrakerSession& session)
    : session_(session),
      host_(config.transport == MoonrakerTransportKind::UnixSocket ? std::string("127.0.0.1") : config.host) {}

void WebcamSource::list(ListHandler handler) {
    session_.call("server.webcams.list", moonraker::Json::object(),
                  [handler = std::move(handler)](const moonraker::RpcResponse& response) {
                      if (!response.ok) {
                          const auto& message = response.error.message;
                          handler({}, message.empty() ? std::string("request failed")
                                                      : message.substr(0, message.find('\n')));
                          return;
                      }
                      handler(parse_webcams(response.result), std::string());
                  });
}

std::string WebcamSource::snapshot_url(const WebcamInfo& webcam) const {
    return webcam_snapshot_url(webcam, host_);
}

void WebcamSource::fetch(const std::string& url, SnapshotHandler handler) {
    fetcher_.get(url, std::move(handler));
}

void WebcamSource::poll() {
    fetcher_.poll();
}

} // namespace app

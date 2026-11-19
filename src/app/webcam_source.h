/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "http_fetcher.h"
#include "webcam.h"

#include <functional>
#include <string>
#include <vector>

namespace moonraker {
class MoonrakerSession;
}

namespace app {

struct MoonrakerConfig;

// The printer's webcams: the list comes from Moonraker, the still images straight from the
// camera's HTTP server (crowsnest / mjpg-streamer / camera-streamer), usually behind the web
// frontend on the printer host.
class WebcamSource {
public:
    // error is empty on success.
    using ListHandler = std::function<void(std::vector<WebcamInfo> webcams, const std::string& error)>;
    // jpeg holds the image on success; error is empty on success.
    using SnapshotHandler = std::function<void(std::string jpeg, const std::string& error)>;

    WebcamSource(const MoonrakerConfig& config, moonraker::MoonrakerSession& session);

    WebcamSource(const WebcamSource&) = delete;
    WebcamSource& operator=(const WebcamSource&) = delete;

    // server.webcams.list. Works while Klipper is not ready, as long as Moonraker is up.
    void list(ListHandler handler);
    // Where the webcam's still image is downloaded from; empty when it has none.
    std::string snapshot_url(const WebcamInfo& webcam) const;
    // The handler runs from poll(), never from fetch() itself.
    void fetch(const std::string& url, SnapshotHandler handler);
    // Runs the handlers of finished downloads; called from the LVGL thread.
    void poll();

private:
    moonraker::MoonrakerSession& session_;
    // The printer host: 127.0.0.1 over the unix socket, otherwise Moonraker's host.
    std::string host_;
    // Not IXWebSocket's HTTP client: mjpg-streamer answers in HTTP/1.0 without a length.
    // Sends no Moonraker API key, as the images may come from another server.
    moonraker::HttpFetcher fetcher_;
};

} // namespace app

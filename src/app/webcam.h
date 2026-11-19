/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "jsonrpc_client.h"

#include <string>
#include <vector>

namespace app {

// A webcam from Moonraker's webcam list (server.webcams.list), the same list Mainsail and
// Fluidd show.
struct WebcamInfo {
    std::string name;
    std::string service;
    std::string snapshot_url;
    std::string stream_url;
    bool flip_horizontal{false};
    bool flip_vertical{false};
    // Clockwise degrees: 0, 90, 180 or 270.
    int rotation{0};
    // 0 when Moonraker does not say.
    int target_fps{0};
};

// The enabled webcams of a server.webcams.list result, in Moonraker's order.
std::vector<WebcamInfo> parse_webcams(const moonraker::Json& result);

// The absolute http(s) URL of the webcam's still image, or empty when it has none.
// Moonraker stores the URLs as the web frontend uses them, often relative to the frontend's
// host (e.g. "/webcam/?action=snapshot", served by nginx on port 80), so relative URLs are
// resolved against http://<host>/. Without a snapshot URL, an mjpg-streamer style stream URL
// ("?action=stream") gives one.
std::string webcam_snapshot_url(const WebcamInfo& webcam, const std::string& host);

} // namespace app

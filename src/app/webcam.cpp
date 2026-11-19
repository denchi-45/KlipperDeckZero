/*
 * SPDX-License-Identifier: MIT
 */

#include "webcam.h"

#include <cmath>
#include <utility>

namespace app {
namespace {

std::string string_or(const moonraker::Json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

bool bool_or(const moonraker::Json& object, const char* key, bool fallback) {
    const auto it = object.find(key);
    return it != object.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

double number_or(const moonraker::Json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_number() ? it->get<double>() : 0.0;
}

// Rounds to a multiple of 90 in [0, 360).
int normalize_rotation(double degrees) {
    if (!std::isfinite(degrees)) {
        return 0;
    }
    const long quarter = std::lround(degrees / 90.0);
    return static_cast<int>(((quarter % 4) + 4) % 4) * 90;
}

bool starts_with(const std::string& text, const char* prefix) {
    return text.rfind(prefix, 0) == 0;
}

// The host part of a URL: an IPv6 literal needs brackets.
std::string url_host(const std::string& host) {
    if (host.find(':') != std::string::npos && !starts_with(host, "[")) {
        return "[" + host + "]";
    }
    return host;
}

} // namespace

std::vector<WebcamInfo> parse_webcams(const moonraker::Json& result) {
    std::vector<WebcamInfo> webcams;
    if (!result.is_object()) {
        return webcams;
    }
    const auto list = result.find("webcams");
    if (list == result.end() || !list->is_array()) {
        return webcams;
    }
    for (const auto& entry : *list) {
        if (!entry.is_object() || !bool_or(entry, "enabled", true)) {
            continue;
        }
        WebcamInfo webcam;
        webcam.name = string_or(entry, "name");
        webcam.service = string_or(entry, "service");
        webcam.snapshot_url = string_or(entry, "snapshot_url");
        webcam.stream_url = string_or(entry, "stream_url");
        webcam.flip_horizontal = bool_or(entry, "flip_horizontal", false);
        webcam.flip_vertical = bool_or(entry, "flip_vertical", false);
        webcam.rotation = normalize_rotation(number_or(entry, "rotation"));
        const double fps = number_or(entry, "target_fps");
        webcam.target_fps = std::isfinite(fps) && fps > 0 && fps < 1000 ? static_cast<int>(fps) : 0;
        webcams.push_back(std::move(webcam));
    }
    return webcams;
}

std::string webcam_snapshot_url(const WebcamInfo& webcam, const std::string& host) {
    std::string url = webcam.snapshot_url;
    if (url.empty()) {
        const auto pos = webcam.stream_url.find("action=stream");
        if (pos == std::string::npos) {
            return {};
        }
        url = webcam.stream_url;
        url.replace(pos, std::string("action=stream").size(), "action=snapshot");
    }
    if (starts_with(url, "http://") || starts_with(url, "https://")) {
        return url;
    }
    if (starts_with(url, "//")) {
        return "http:" + url;
    }
    if (host.empty()) {
        return {};
    }
    if (!starts_with(url, "/")) {
        url.insert(0, "/");
    }
    return "http://" + url_host(host) + url;
}

} // namespace app

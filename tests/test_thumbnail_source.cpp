/*
 * SPDX-License-Identifier: MIT
 */

#include "application_config.h"
#include "fake_transport.h"
#include "moonraker_session.h"
#include "test_framework.h"
#include "thumbnail_source.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using moonraker::TransportEventType;
using nlohmann::json;

namespace {

struct Fetched {
    int calls{0};
    std::string png;
    std::string error;
};

app::ThumbnailSource::Handler record(Fetched& fetched) {
    return [&fetched](std::string png, const std::string& error) {
        ++fetched.calls;
        fetched.png = std::move(png);
        fetched.error = error;
    };
}

// The last request of the given method, or null.
json find_request(const std::vector<json>& sent, const std::string& method) {
    for (auto it = sent.rbegin(); it != sent.rend(); ++it) {
        if (it->value("method", "") == method) {
            return *it;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("thumbnails: the download URL escapes each path segment") {
    CHECK_EQ(app::gcodes_file_url("192.168.0.20", 7125, "parts/.thumbs/a b-300x300.png"),
             std::string("http://192.168.0.20:7125/server/files/gcodes/parts/.thumbs/a%20b-300x300.png"));
    CHECK_EQ(app::gcodes_file_url("printer.local", 80, "100%_#1?.png"),
             std::string("http://printer.local:80/server/files/gcodes/100%25_%231%3F.png"));
}

TEST_CASE("thumbnails: the gcodes directory is read from server.files.roots") {
    CHECK_EQ(app::gcodes_root_directory(json::parse(R"([
                 {"name": "config", "path": "/home/pi/printer_data/config", "permissions": "rw"},
                 {"name": "gcodes", "path": "/home/pi/printer_data/gcodes/", "permissions": "rw"}
             ])")),
             std::string("/home/pi/printer_data/gcodes"));
    CHECK(app::gcodes_root_directory(json::parse(R"([{"name": "gcodes", "path": 1}])")).empty());
    CHECK(app::gcodes_root_directory(json::parse(R"([{"name": 1}, "gcodes"])")).empty());
    CHECK(app::gcodes_root_directory(json::object()).empty());
}

TEST_CASE("thumbnails: over the unix socket the files are read from the gcodes directory") {
    const auto root = std::filesystem::temp_directory_path() / "kdz_test_thumbnails";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "parts" / ".thumbs");
    std::ofstream(root / "parts" / ".thumbs" / "a b.png", std::ios::binary) << "PNG BYTES";

    FakeTransport::Shared shared;
    moonraker::MoonrakerSession session(std::make_unique<FakeTransport>(shared), moonraker::SessionOptions{});
    session.start();
    inject(shared, TransportEventType::Opened);
    session.poll();

    app::MoonrakerConfig config;
    config.transport = app::MoonrakerTransportKind::UnixSocket;
    auto source = app::create_thumbnail_source(config, session);

    // Both wait for the one roots query.
    Fetched first;
    Fetched missing;
    source->fetch("parts/.thumbs/a b.png", record(first));
    source->fetch("parts/.thumbs/missing.png", record(missing));
    const auto roots_requests = std::count_if(shared.sent.begin(), shared.sent.end(), [](const json& request) {
        return request.value("method", "") == "server.files.roots";
    });
    CHECK(roots_requests == 1);
    CHECK(first.calls == 0);

    const auto request = find_request(shared.sent, "server.files.roots");
    reply(shared, request, json::array({{{"name", "gcodes"}, {"path", root.generic_string()}}}));
    session.poll();
    CHECK(first.calls == 1);
    CHECK_EQ(first.png, std::string("PNG BYTES"));
    CHECK(first.error.empty());
    CHECK(missing.calls == 1);
    CHECK(missing.png.empty());
    CHECK(!missing.error.empty());

    // Later fetches reuse the directory and are answered from poll().
    Fetched again;
    const auto sent_before = shared.sent.size();
    source->fetch("parts/.thumbs/a b.png", record(again));
    CHECK(again.calls == 0);
    CHECK(shared.sent.size() == sent_before);
    source->poll();
    CHECK(again.calls == 1);
    CHECK_EQ(again.png, std::string("PNG BYTES"));

    std::filesystem::remove_all(root);
}

TEST_CASE("thumbnails: an unknown gcodes directory fails the waiting fetches") {
    FakeTransport::Shared shared;
    moonraker::MoonrakerSession session(std::make_unique<FakeTransport>(shared), moonraker::SessionOptions{});
    session.start();
    inject(shared, TransportEventType::Opened);
    session.poll();

    app::MoonrakerConfig config;
    config.transport = app::MoonrakerTransportKind::UnixSocket;
    auto source = app::create_thumbnail_source(config, session);

    Fetched fetched;
    source->fetch("a.png", record(fetched));
    reply_error(shared, find_request(shared.sent, "server.files.roots"), -32601, "Method not found");
    session.poll();
    CHECK(fetched.calls == 1);
    CHECK(fetched.png.empty());
    CHECK(!fetched.error.empty());

    // The next fetch asks again.
    source->fetch("a.png", record(fetched));
    const auto roots_requests = std::count_if(shared.sent.begin(), shared.sent.end(), [](const json& request) {
        return request.value("method", "") == "server.files.roots";
    });
    CHECK(roots_requests == 2);
}

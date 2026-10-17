/*
 * SPDX-License-Identifier: MIT
 */

#include "thumbnail_source.h"

#include "application_config.h"
#include "http_getter.h"
#include "moonraker_session.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <utility>
#include <vector>

namespace app {
namespace {

// Slicer thumbnails are a few KB to a few hundred KB; anything bigger is not a thumbnail.
constexpr std::streamoff kMaxThumbnailBytes = 2 * 1024 * 1024;

bool is_unreserved(unsigned char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' ||
           c == '_' || c == '.' || c == '~';
}

// Downloads from the Moonraker host over HTTP.
class HttpThumbnailSource final : public ThumbnailSource {
public:
    HttpThumbnailSource(std::string host, int port, std::string api_key)
        : host_(std::move(host)), port_(port), getter_(std::move(api_key)) {}

    void fetch(const std::string& path, Handler handler) override {
        getter_.get(gcodes_file_url(host_, port_, path), std::move(handler));
    }

    void poll() override {
        getter_.poll();
    }

private:
    std::string host_;
    int port_;
    moonraker::HttpGetter getter_;
};

// Reads the files from Moonraker's gcodes directory on this device. The directory is asked
// for once with server.files.roots; fetches made meanwhile wait for the answer.
class LocalThumbnailSource final : public ThumbnailSource {
public:
    explicit LocalThumbnailSource(moonraker::MoonrakerSession& session) : session_(session) {}

    void fetch(const std::string& path, Handler handler) override {
        waiting_.emplace_back(path, std::move(handler));
        if (!root_.empty()) {
            // Answer from poll(), as the interface promises.
            return;
        }
        if (querying_) {
            return;
        }
        querying_ = true;
        session_.call("server.files.roots", moonraker::Json::object(),
                      [this](const moonraker::RpcResponse& response) {
                          querying_ = false;
                          if (response.ok) {
                              root_ = gcodes_root_directory(response.result);
                          }
                          answer_waiting(root_.empty() ? std::string("gcodes directory unknown")
                                                       : std::string());
                      });
    }

    void poll() override {
        if (!root_.empty() && !waiting_.empty()) {
            answer_waiting({});
        }
    }

private:
    void answer_waiting(const std::string& error) {
        auto waiting = std::move(waiting_);
        waiting_.clear();
        for (auto& [path, handler] : waiting) {
            if (!error.empty()) {
                handler({}, error);
                continue;
            }
            std::string png;
            std::string read_error;
            read_file(root_ + "/" + path, png, read_error);
            handler(std::move(png), read_error);
        }
    }

    static void read_file(const std::string& file_path, std::string& data, std::string& error) {
        std::ifstream file(file_path, std::ios::binary | std::ios::ate);
        if (!file) {
            error = "cannot open " + file_path;
            return;
        }
        const auto size = static_cast<std::streamoff>(file.tellg());
        if (size <= 0 || size > kMaxThumbnailBytes) {
            error = "unexpected size of " + file_path;
            return;
        }
        data.resize(static_cast<std::size_t>(size));
        file.seekg(0);
        if (!file.read(data.data(), size)) {
            data.clear();
            error = "cannot read " + file_path;
        }
    }

    moonraker::MoonrakerSession& session_;
    std::string root_;
    bool querying_{false};
    std::vector<std::pair<std::string, Handler>> waiting_;
};

} // namespace

std::unique_ptr<ThumbnailSource> create_thumbnail_source(const MoonrakerConfig& config,
                                                         moonraker::MoonrakerSession& session) {
    if (config.transport == MoonrakerTransportKind::UnixSocket) {
        return std::make_unique<LocalThumbnailSource>(session);
    }
    return std::make_unique<HttpThumbnailSource>(config.host, config.port, config.api_key);
}

std::string gcodes_file_url(const std::string& host, int port, const std::string& path) {
    std::string url = "http://" + host + ":" + std::to_string(port) + "/server/files/gcodes/";
    for (const unsigned char c : path) {
        if (is_unreserved(c) || c == '/') {
            url += static_cast<char>(c);
        }
        else {
            char escaped[4];
            std::snprintf(escaped, sizeof(escaped), "%%%02X", c);
            url += escaped;
        }
    }
    return url;
}

std::string gcodes_root_directory(const moonraker::Json& roots) {
    if (!roots.is_array()) {
        return {};
    }
    for (const auto& root : roots) {
        if (!root.is_object()) {
            continue;
        }
        // value() would throw on a field of another type.
        const auto name = root.find("name");
        const auto path = root.find("path");
        if (name == root.end() || !name->is_string() || *name != "gcodes" || path == root.end() ||
            !path->is_string()) {
            continue;
        }
        auto directory = path->get<std::string>();
        while (directory.size() > 1 && directory.back() == '/') {
            directory.pop_back();
        }
        return directory;
    }
    return {};
}

} // namespace app

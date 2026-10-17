/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "jsonrpc_client.h"

#include <functional>
#include <memory>
#include <string>

namespace moonraker {
class MoonrakerSession;
}

namespace app {

struct MoonrakerConfig;

// Reads the PNG thumbnails of G-code files from the printer host.
class ThumbnailSource {
public:
    // png holds the file on success; error is empty on success.
    using Handler = std::function<void(std::string png, const std::string& error)>;

    virtual ~ThumbnailSource() = default;

    // path is relative to the gcodes root (see thumbnail_path()). The handler runs from poll()
    // or from a session callback, never from fetch() itself.
    virtual void fetch(const std::string& path, Handler handler) = 0;
    // Runs the handlers of finished downloads; called from the LVGL thread.
    virtual void poll() {}
};

// Over the unix socket Moonraker runs on this device, so the files are read from its gcodes
// directory. Over a WebSocket they are downloaded from the same host with HTTP.
std::unique_ptr<ThumbnailSource> create_thumbnail_source(const MoonrakerConfig& config,
                                                         moonraker::MoonrakerSession& session);

// http://<host>:<port>/server/files/gcodes/<path>, with each path segment percent-encoded.
std::string gcodes_file_url(const std::string& host, int port, const std::string& path);
// Directory of the "gcodes" root in a `server.files.roots` result; empty when missing.
std::string gcodes_root_directory(const moonraker::Json& roots);

} // namespace app

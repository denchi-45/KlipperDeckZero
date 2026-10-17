/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>

namespace ix {
class HttpClient;
}

namespace moonraker {

// Plain HTTP GETs on a background thread, e.g. files from Moonraker's /server/files/.
// Like JsonRpcClient, finished requests are queued and their handlers run from poll(),
// so every handler runs on the thread calling poll() — the LVGL thread.
class HttpGetter {
public:
    // body holds the response on success; error is empty on success.
    using Handler = std::function<void(std::string body, const std::string& error)>;

    // api_key is sent as X-Api-Key when not empty.
    explicit HttpGetter(std::string api_key = {});
    ~HttpGetter();

    HttpGetter(const HttpGetter&) = delete;
    HttpGetter& operator=(const HttpGetter&) = delete;

    void get(const std::string& url, Handler handler);
    // Runs the handlers of the requests that finished since the last call.
    void poll();

private:
    struct Finished {
        std::uint64_t id;
        std::string body;
        std::string error;
    };

    std::string api_key_;
    std::uint64_t next_id_{1};
    std::map<std::uint64_t, Handler> handlers_;
    std::mutex finished_mutex_;
    std::deque<Finished> finished_;
    // Declared last so its worker thread is joined before the queue above goes away.
    std::unique_ptr<ix::HttpClient> client_;
};

} // namespace moonraker

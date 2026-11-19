/*
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>

namespace moonraker {

struct HttpUrl {
    std::string host; // without the brackets of an IPv6 literal
    int port{80};
    std::string target{"/"}; // path and query
};

// Splits an http:// URL; false, with error set, for anything else (https is not built in).
bool parse_http_url(const std::string& url, HttpUrl& parsed, std::string& error);
// The status code of a status line such as "HTTP/1.0 200 OK", or -1.
int parse_http_status(const std::string& line);
// A redirect's Location resolved against the URL it came from (RFC 3986 reference resolution):
// absolute URLs as they are, "//host/..." with http, and paths, relative paths and queries on the
// same server. Empty when there is nothing to follow.
std::string resolve_http_location(const HttpUrl& from, const std::string& location);
// The size on a chunk-size line of a chunked body ("1a2b", "1a2b;name=value"); false when the
// line is malformed.
bool parse_chunk_size(const std::string& line, std::uint64_t& size);
// A Content-Length value; false unless it is a plain decimal number.
bool parse_content_length(const std::string& value, std::uint64_t& length);

// Plain HTTP GETs on a background thread, one at a time, for servers IXWebSocket's client cannot
// read: it only takes HTTP/1.1 status lines and bodies with a length, while e.g. mjpg-streamer
// answers in HTTP/1.0 and ends the image by closing the connection.
// Like HttpGetter, finished requests are queued and their handlers run from poll().
class HttpFetcher {
public:
    // body holds the response on success; error is empty on success.
    using Handler = std::function<void(std::string body, const std::string& error)>;

    // Bodies larger than max_body_bytes fail.
    explicit HttpFetcher(std::size_t max_body_bytes = 8 * 1024 * 1024);
    ~HttpFetcher();

    HttpFetcher(const HttpFetcher&) = delete;
    HttpFetcher& operator=(const HttpFetcher&) = delete;

    void get(const std::string& url, Handler handler);
    // Runs the handlers of the requests that finished since the last call.
    void poll();

private:
    struct Job {
        std::uint64_t id;
        std::string url;
    };
    struct Finished {
        std::uint64_t id;
        std::string body;
        std::string error;
    };

    void run();
    void fetch(const std::string& url, std::string& body, std::string& error);

    const std::size_t max_body_bytes_;
    std::uint64_t next_id_{1};
    std::map<std::uint64_t, Handler> handlers_;

    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> jobs_;
    std::deque<Finished> finished_;
    std::atomic<bool> stop_{false};
    // Declared last: started once everything above exists, joined first.
    std::thread worker_;
};

} // namespace moonraker

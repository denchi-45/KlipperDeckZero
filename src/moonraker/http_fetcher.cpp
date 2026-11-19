/*
 * SPDX-License-Identifier: MIT
 */

#include "http_fetcher.h"

#include <ixwebsocket/IXCancellationRequest.h>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXSocket.h>
#include <ixwebsocket/IXSocketFactory.h>
#include <ixwebsocket/IXSocketTLSOptions.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <utility>

namespace moonraker {
namespace {

constexpr int kConnectTimeoutSeconds = 5;
constexpr int kTransferTimeoutSeconds = 15;
constexpr int kMaxRedirects = 3;
constexpr std::size_t kMaxHeaderLines = 100;

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string trim(const std::string& text) {
    const auto begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

std::string host_header(const HttpUrl& url) {
    std::string host = url.host.find(':') != std::string::npos ? "[" + url.host + "]" : url.host;
    if (url.port != 80) {
        host += ":" + std::to_string(url.port);
    }
    return host;
}

// Reads until the server closes the connection. The socket is non-blocking: recv() comes first,
// as it reports the close (0) even where the poll does not (seen with select() on Windows).
bool read_to_close(ix::Socket& socket, const ix::CancellationRequest& cancelled, std::size_t max_bytes,
                   std::string& body, std::string& error) {
    std::array<char, 16 * 1024> buffer;
    while (true) {
        if (cancelled()) {
            error = "timed out";
            return false;
        }
        const auto received = socket.recv(buffer.data(), buffer.size());
        if (received == 0) {
            return true;
        }
        if (received < 0) {
            if (!ix::Socket::isWaitNeeded()) {
                error = "connection error";
                return false;
            }
            if (socket.isReadyToRead(100) == ix::PollResultType::Error) {
                error = "connection error";
                return false;
            }
            continue;
        }
        body.append(buffer.data(), static_cast<std::size_t>(received));
        if (body.size() > max_bytes) {
            error = "response too large";
            return false;
        }
    }
}

bool read_chunked(ix::Socket& socket, const ix::CancellationRequest& cancelled, std::size_t max_bytes,
                  std::string& body, std::string& error) {
    while (true) {
        const auto size_line = socket.readLine(cancelled);
        if (!size_line.first) {
            error = "cannot read the response";
            return false;
        }
        char* end = nullptr;
        const unsigned long long size = std::strtoull(trim(size_line.second).c_str(), &end, 16);
        if (size > max_bytes || body.size() + size > max_bytes) {
            error = "response too large";
            return false;
        }
        if (size == 0) {
            return true;
        }
        const auto chunk = socket.readBytes(static_cast<std::size_t>(size), nullptr, nullptr, cancelled);
        if (!chunk.first) {
            error = "cannot read the response";
            return false;
        }
        body += chunk.second;
        // The CRLF after the chunk.
        if (!socket.readLine(cancelled).first) {
            error = "cannot read the response";
            return false;
        }
    }
}

} // namespace

bool parse_http_url(const std::string& url, HttpUrl& parsed, std::string& error) {
    const std::string scheme = lower(url.substr(0, url.find("://") == std::string::npos ? 0 : url.find("://")));
    if (scheme == "https") {
        error = "https is not supported";
        return false;
    }
    if (scheme != "http") {
        error = "not an http URL";
        return false;
    }
    const auto authority_begin = url.find("://") + 3;
    const auto path_begin = url.find_first_of("/?#", authority_begin);
    std::string authority = url.substr(authority_begin, path_begin == std::string::npos ? std::string::npos
                                                                                        : path_begin - authority_begin);
    // Credentials are not sent; drop them from the host.
    if (const auto at = authority.rfind('@'); at != std::string::npos) {
        authority.erase(0, at + 1);
    }

    HttpUrl result;
    std::string port;
    if (!authority.empty() && authority[0] == '[') {
        const auto close = authority.find(']');
        if (close == std::string::npos) {
            error = "bad host in URL";
            return false;
        }
        result.host = authority.substr(1, close - 1);
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') {
                error = "bad host in URL";
                return false;
            }
            port = authority.substr(close + 2);
        }
    }
    else {
        const auto colon = authority.rfind(':');
        result.host = authority.substr(0, colon);
        if (colon != std::string::npos) {
            port = authority.substr(colon + 1);
        }
    }
    if (result.host.empty()) {
        error = "no host in URL";
        return false;
    }
    if (!port.empty()) {
        if (port.size() > 5 || !std::all_of(port.begin(), port.end(), [](unsigned char c) { return std::isdigit(c); })) {
            error = "bad port in URL";
            return false;
        }
        result.port = std::atoi(port.c_str());
        if (result.port <= 0 || result.port > 65535) {
            error = "bad port in URL";
            return false;
        }
    }
    if (path_begin != std::string::npos) {
        result.target = url.substr(path_begin);
        if (const auto fragment = result.target.find('#'); fragment != std::string::npos) {
            result.target.erase(fragment);
        }
        if (result.target.empty() || result.target[0] != '/') {
            result.target.insert(0, "/");
        }
    }
    parsed = std::move(result);
    return true;
}

int parse_http_status(const std::string& line) {
    if (line.rfind("HTTP/1.", 0) != 0 || line.size() < 12 || line[8] != ' ') {
        return -1;
    }
    for (std::size_t i = 9; i < 12; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(line[i]))) {
            return -1;
        }
    }
    if (line.size() > 12 && line[12] != ' ' && line[12] != '\r' && line[12] != '\n') {
        return -1;
    }
    return std::atoi(line.substr(9, 3).c_str());
}

std::string resolve_http_location(const HttpUrl& from, const std::string& location) {
    const std::string target = trim(location);
    if (target.empty()) {
        return {};
    }
    if (lower(target.substr(0, 7)) == "http://" || lower(target.substr(0, 8)) == "https://") {
        return target;
    }
    if (target[0] != '/' || (target.size() > 1 && target[1] == '/')) {
        return {};
    }
    return "http://" + host_header(from) + target;
}

HttpFetcher::HttpFetcher(std::size_t max_body_bytes) : max_body_bytes_(max_body_bytes) {
    static std::once_flag once;
    std::call_once(once, [] { ix::initNetSystem(); });
    worker_ = std::thread([this] { run(); });
}

HttpFetcher::~HttpFetcher() {
    stop_ = true;
    wake_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void HttpFetcher::get(const std::string& url, Handler handler) {
    const auto id = next_id_++;
    handlers_[id] = std::move(handler);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        jobs_.push_back({id, url});
    }
    wake_.notify_one();
}

void HttpFetcher::poll() {
    std::deque<Finished> finished;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        finished.swap(finished_);
    }
    for (auto& item : finished) {
        const auto it = handlers_.find(item.id);
        if (it == handlers_.end()) {
            continue;
        }
        auto handler = std::move(it->second);
        handlers_.erase(it);
        if (handler) {
            handler(std::move(item.body), item.error);
        }
    }
}

void HttpFetcher::run() {
    while (true) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || !jobs_.empty(); });
            if (stop_) {
                return;
            }
            job = std::move(jobs_.front());
            jobs_.pop_front();
        }
        Finished finished{job.id, {}, {}};
        fetch(job.url, finished.body, finished.error);
        if (!finished.error.empty()) {
            finished.body.clear();
        }
        std::lock_guard<std::mutex> lock(mutex_);
        finished_.push_back(std::move(finished));
    }
}

void HttpFetcher::fetch(const std::string& start_url, std::string& body, std::string& error) {
    std::string url = start_url;
    for (int redirect = 0; redirect <= kMaxRedirects; ++redirect) {
        HttpUrl target;
        if (!parse_http_url(url, target, error)) {
            return;
        }
        std::string socket_error;
        auto socket = ix::createSocket(false, -1, socket_error, ix::SocketTLSOptions());
        if (!socket) {
            error = socket_error.empty() ? "cannot create a socket" : socket_error;
            return;
        }
        const auto connect_cancelled = ix::makeCancellationRequestWithTimeout(kConnectTimeoutSeconds, stop_);
        if (!socket->connect(target.host, target.port, socket_error, connect_cancelled)) {
            error = stop_ ? "stopped" : (socket_error.empty() ? "cannot connect" : socket_error);
            return;
        }
        const auto cancelled = ix::makeCancellationRequestWithTimeout(kTransferTimeoutSeconds, stop_);
        const std::string request = "GET " + target.target + " HTTP/1.1\r\n" + "Host: " + host_header(target) +
                                    "\r\n" + "User-Agent: KlipperDeckZero\r\n" + "Accept: image/jpeg, */*\r\n" +
                                    "Connection: close\r\n\r\n";
        if (!socket->writeBytes(request, cancelled)) {
            error = "cannot send the request";
            return;
        }

        const auto status_line = socket->readLine(cancelled);
        const int status = status_line.first ? parse_http_status(status_line.second) : -1;
        if (status < 0) {
            error = status_line.first ? "not an HTTP response" : "no response";
            return;
        }
        std::map<std::string, std::string> headers;
        for (std::size_t lines = 0;; ++lines) {
            const auto line = socket->readLine(cancelled);
            if (!line.first || lines > kMaxHeaderLines) {
                error = "cannot read the response headers";
                return;
            }
            const std::string text = trim(line.second);
            if (text.empty()) {
                break;
            }
            const auto colon = text.find(':');
            if (colon != std::string::npos) {
                headers[lower(trim(text.substr(0, colon)))] = trim(text.substr(colon + 1));
            }
        }

        if (status >= 300 && status < 400 && headers.count("location")) {
            url = resolve_http_location(target, headers["location"]);
            if (url.empty()) {
                error = "cannot follow the redirect";
                return;
            }
            continue;
        }
        if (status != 200) {
            error = "HTTP " + std::to_string(status);
            return;
        }

        if (lower(headers["transfer-encoding"]).find("chunked") != std::string::npos) {
            read_chunked(*socket, cancelled, max_body_bytes_, body, error);
            return;
        }
        if (headers.count("content-length")) {
            char* end = nullptr;
            const unsigned long long length = std::strtoull(headers["content-length"].c_str(), &end, 10);
            if (length > max_body_bytes_) {
                error = "response too large";
                return;
            }
            const auto content = socket->readBytes(static_cast<std::size_t>(length), nullptr, nullptr, cancelled);
            if (!content.first) {
                error = "cannot read the response";
                return;
            }
            body = content.second;
            return;
        }
        read_to_close(*socket, cancelled, max_body_bytes_, body, error);
        return;
    }
    error = "too many redirects";
}

} // namespace moonraker

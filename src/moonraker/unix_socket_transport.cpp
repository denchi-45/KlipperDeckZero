/*
 * SPDX-License-Identifier: MIT
 */

#include "transport.h"

#include <utility>

#if defined(_WIN32)

namespace moonraker {
namespace {

class UnsupportedUnixSocketTransport final : public Transport {
public:
    explicit UnsupportedUnixSocketTransport(std::string path) : path_(std::move(path)) {}

    void set_event_handler(EventHandler handler) override {
        handler_ = std::move(handler);
    }

    void open() override {
        if (handler_) {
            handler_(TransportEventType::Closed,
                     "unix socket transport is not available on Windows; use transport=websocket");
        }
    }

    void close() override {}

    bool send(const std::string&) override {
        return false;
    }

    std::string describe() const override {
        return "unix:" + path_;
    }

private:
    std::string path_;
    EventHandler handler_;
};

} // namespace

std::unique_ptr<Transport> create_unix_socket_transport(const std::string& socket_path) {
    return std::make_unique<UnsupportedUnixSocketTransport>(socket_path);
}

} // namespace moonraker

#else

#include <atomic>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <thread>

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace moonraker {
namespace {

constexpr char kEtx = '\x03';
constexpr int kPollIntervalMs = 200;

class UnixSocketTransport final : public Transport {
public:
    explicit UnixSocketTransport(std::string path) : path_(std::move(path)) {}

    ~UnixSocketTransport() override {
        close();
    }

    void set_event_handler(EventHandler handler) override {
        std::lock_guard<std::mutex> lock(handler_mutex_);
        handler_ = std::move(handler);
    }

    void open() override {
        close();
        stop_requested_ = false;
        worker_ = std::thread([this] { run(); });
    }

    void close() override {
        stop_requested_ = true;
        if (worker_.joinable()) {
            worker_.join();
        }
        std::lock_guard<std::mutex> lock(fd_mutex_);
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    bool send(const std::string& text) override {
        std::lock_guard<std::mutex> lock(fd_mutex_);
        if (fd_ < 0) {
            return false;
        }
        std::string frame = text;
        frame.push_back(kEtx);
        const char* data = frame.data();
        std::size_t remaining = frame.size();
        while (remaining > 0) {
            const auto written = ::send(fd_, data, remaining, MSG_NOSIGNAL);
            if (written < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return false;
            }
            data += written;
            remaining -= static_cast<std::size_t>(written);
        }
        return true;
    }

    std::string describe() const override {
        return "unix:" + path_;
    }

private:
    void emit(TransportEventType type, std::string payload) {
        std::lock_guard<std::mutex> lock(handler_mutex_);
        if (handler_) {
            handler_(type, std::move(payload));
        }
    }

    void run() {
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) {
            emit(TransportEventType::Closed, std::string("socket(): ") + std::strerror(errno));
            return;
        }

        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        if (path_.size() >= sizeof(address.sun_path)) {
            ::close(fd);
            emit(TransportEventType::Closed, "socket path too long: " + path_);
            return;
        }
        std::memcpy(address.sun_path, path_.c_str(), path_.size() + 1);

        if (::connect(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
            const std::string reason = "connect(" + path_ + "): " + std::strerror(errno);
            ::close(fd);
            emit(TransportEventType::Closed, reason);
            return;
        }

        {
            std::lock_guard<std::mutex> lock(fd_mutex_);
            fd_ = fd;
        }
        emit(TransportEventType::Opened, {});

        std::string buffer;
        char chunk[4096];
        std::string reason = "connection closed";
        while (!stop_requested_) {
            pollfd descriptor{fd, POLLIN, 0};
            const int ready = ::poll(&descriptor, 1, kPollIntervalMs);
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                reason = std::string("poll(): ") + std::strerror(errno);
                break;
            }
            if (ready == 0) {
                continue;
            }

            const auto received = ::recv(fd, chunk, sizeof(chunk), 0);
            if (received == 0) {
                break;
            }
            if (received < 0) {
                if (errno == EINTR || errno == EAGAIN) {
                    continue;
                }
                reason = std::string("recv(): ") + std::strerror(errno);
                break;
            }

            buffer.append(chunk, static_cast<std::size_t>(received));
            std::size_t start = 0;
            for (auto etx = buffer.find(kEtx); etx != std::string::npos;
                 etx = buffer.find(kEtx, start)) {
                emit(TransportEventType::Message, buffer.substr(start, etx - start));
                start = etx + 1;
            }
            buffer.erase(0, start);
        }

        {
            std::lock_guard<std::mutex> lock(fd_mutex_);
            ::close(fd_);
            fd_ = -1;
        }
        if (!stop_requested_) {
            emit(TransportEventType::Closed, reason);
        }
    }

    std::string path_;
    std::thread worker_;
    std::atomic<bool> stop_requested_{false};
    std::mutex fd_mutex_;
    int fd_{-1};
    std::mutex handler_mutex_;
    EventHandler handler_;
};

} // namespace

std::unique_ptr<Transport> create_unix_socket_transport(const std::string& socket_path) {
    return std::make_unique<UnixSocketTransport>(socket_path);
}

} // namespace moonraker

#endif

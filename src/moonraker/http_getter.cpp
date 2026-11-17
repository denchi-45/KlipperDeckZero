/*
 * SPDX-License-Identifier: MIT
 */

#include "http_getter.h"

#include <ixwebsocket/IXHttpClient.h>
#include <ixwebsocket/IXNetSystem.h>

#include <utility>

namespace moonraker {
namespace {

// Thumbnails are small; give up quickly so a missing host does not leave the page waiting.
constexpr int kConnectTimeoutSeconds = 5;
constexpr int kTransferTimeoutSeconds = 15;

} // namespace

HttpGetter::HttpGetter(std::string api_key) : api_key_(std::move(api_key)) {
    static std::once_flag once;
    std::call_once(once, [] { ix::initNetSystem(); });
    client_ = std::make_unique<ix::HttpClient>(true);
}

HttpGetter::~HttpGetter() = default;

void HttpGetter::get(const std::string& url, Handler handler) {
    const auto id = next_id_++;
    handlers_[id] = std::move(handler);

    auto args = client_->createRequest(url, ix::HttpClient::kGet);
    args->connectTimeout = kConnectTimeoutSeconds;
    args->transferTimeout = kTransferTimeoutSeconds;
    // Built without zlib, so a compressed body could not be decoded.
    args->compress = false;
    if (!api_key_.empty()) {
        args->extraHeaders["X-Api-Key"] = api_key_;
    }
    client_->performRequest(args, [this, id](const ix::HttpResponsePtr& response) {
        Finished finished{id, {}, {}};
        if (response->errorCode != ix::HttpErrorCode::Ok) {
            finished.error = response->errorMsg.empty() ? "request failed" : response->errorMsg;
        }
        else if (response->statusCode != 200) {
            finished.error = "HTTP " + std::to_string(response->statusCode);
        }
        else {
            finished.body = response->body;
        }
        std::lock_guard<std::mutex> lock(finished_mutex_);
        finished_.push_back(std::move(finished));
    });
}

void HttpGetter::poll() {
    std::deque<Finished> finished;
    {
        std::lock_guard<std::mutex> lock(finished_mutex_);
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

} // namespace moonraker

#include "WebServer.h"

#include <stdexcept>
#include <utility>

WebServer::WebServer(std::size_t serverId) : serverId_(serverId) {}

bool WebServer::isBusy() const noexcept {
    return currentRequest_.has_value();
}

bool WebServer::isIdle() const noexcept {
    return !isBusy();
}

bool WebServer::assign(const Request& request) {
    if (request.timeCycles <= 0) {
        throw std::invalid_argument("Request processing duration must be positive");
    }
    if (isBusy() || retiring_) {
        return false;
    }

    currentRequest_ = request;
    remainingCycles_ = request.timeCycles;
    return true;
}

std::optional<Request> WebServer::tick() {
    if (!currentRequest_) {
        return std::nullopt;
    }

    --remainingCycles_;
    ++busyCycles_;
    if (remainingCycles_ > 0) {
        return std::nullopt;
    }

    ++completedCount_;
    std::optional<Request> completed(std::move(currentRequest_));
    currentRequest_.reset();
    remainingCycles_ = 0;
    return completed;
}

void WebServer::retireWhenIdle() noexcept {
    retiring_ = true;
}

bool WebServer::isRetiring() const noexcept {
    return retiring_;
}

std::size_t WebServer::getServerId() const noexcept {
    return serverId_;
}

std::uint64_t WebServer::getCompletedCount() const noexcept {
    return completedCount_;
}

std::uint64_t WebServer::getBusyCycles() const noexcept {
    return busyCycles_;
}

ServerStats WebServer::stats() const noexcept {
    return ServerStats{serverId_, completedCount_, busyCycles_};
}

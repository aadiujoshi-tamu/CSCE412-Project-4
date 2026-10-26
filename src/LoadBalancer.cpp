#include "LoadBalancer.h"

#include <limits>
#include <stdexcept>

LoadBalancer::LoadBalancer(const LoadBalancerOptions& options)
    : options_(options),
      requestGenerator_(options.randomSeed,
                        options.minRequestDuration,
                        options.maxRequestDuration) {
    if (options_.initialServers == 0) {
        throw std::invalid_argument("Load balancer must start with at least one server");
    }
    if (options_.initialRequestsPerServer >
        std::numeric_limits<std::size_t>::max() / options_.initialServers) {
        throw std::length_error("Initial request count exceeds addressable memory");
    }

    servers_.reserve(options_.initialServers);
    for (std::size_t id = 1; id <= options_.initialServers; ++id) {
        servers_.push_back(std::make_unique<WebServer>(id));
    }

    const std::size_t initialRequestCount =
        options_.initialServers * options_.initialRequestsPerServer;
    for (std::size_t i = 0; i < initialRequestCount; ++i) {
        Request request = requestGenerator_.make(0);
        addRequest(request);
    }
}

void LoadBalancer::addRequest(const Request& request) {
    if (request.timeCycles <= 0) {
        throw std::invalid_argument("Request processing duration must be positive");
    }
    queue_.push(request);
    ++generatedRequests_;
    ++acceptedRequests_;
}

void LoadBalancer::assignRequests() {
    for (const auto& server : servers_) {
        if (queue_.empty()) {
            break;
        }
        if (server->isIdle() && !server->isRetiring()) {
            Request request = queue_.pop();
            if (!server->assign(request)) {
                // A server can only reject assignment if its state changed;
                // preserve FIFO order if that invariant is ever broken.
                queue_.push(request);
                throw std::logic_error("Idle server rejected a request assignment");
            }
        }
    }
}

void LoadBalancer::tickServers() {
    for (const auto& server : servers_) {
        if (server->tick().has_value()) {
            ++completedRequests_;
        }
    }
}

void LoadBalancer::step() {
    ++currentCycle_;
    tickServers();
    assignRequests();

    queueSizeSum_ += queue_.size();
    if (queue_.size() > peakQueueSize_) {
        peakQueueSize_ = queue_.size();
    }
}

void LoadBalancer::run() {
    while (currentCycle_ < options_.runCycles) {
        step();
    }
}

std::size_t LoadBalancer::getQueueSize() const noexcept {
    return queue_.size();
}

std::size_t LoadBalancer::getServerCount() const noexcept {
    return servers_.size();
}

std::uint64_t LoadBalancer::getCurrentCycle() const noexcept {
    return currentCycle_;
}

LBStats LoadBalancer::getStatistics() const {
    LBStats result;
    result.currentCycle = currentCycle_;
    result.generatedRequests = generatedRequests_;
    result.acceptedRequests = acceptedRequests_;
    result.completedRequests = completedRequests_;
    result.queuedRequests = queue_.size();
    result.serverCount = servers_.size();
    result.peakQueueSize = peakQueueSize_ > queue_.peakSize()
                               ? peakQueueSize_
                               : queue_.peakSize();
    result.queueSizeSum = queueSizeSum_;
    result.averageQueueSize = currentCycle_ == 0
                                  ? static_cast<double>(queue_.size())
                                  : static_cast<double>(queueSizeSum_) /
                                        static_cast<double>(currentCycle_);
    result.servers.reserve(servers_.size());
    for (const auto& server : servers_) {
        result.servers.push_back(server->stats());
        if (server->isBusy()) {
            ++result.inFlightRequests;
        }
    }
    return result;
}

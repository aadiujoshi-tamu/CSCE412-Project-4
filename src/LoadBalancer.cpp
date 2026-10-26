#include "LoadBalancer.h"

#include <iterator>
#include <limits>
#include <cmath>
#include <stdexcept>

LoadBalancer::LoadBalancer(const LoadBalancerOptions& options)
    : options_(options),
      requestGenerator_(options.randomSeed,
                        options.minRequestDuration,
                        options.maxRequestDuration) {
    if (options_.initialServers == 0) {
        throw std::invalid_argument("Load balancer must start with at least one server");
    }
    if (options_.minServers == 0 || options_.maxServers < options_.minServers ||
        options_.initialServers < options_.minServers ||
        options_.initialServers > options_.maxServers) {
        throw std::invalid_argument("Server counts must satisfy min <= initial <= max");
    }
    if (options_.minRequestsPerServer >= options_.maxRequestsPerServer) {
        throw std::invalid_argument("Queue lower threshold must be less than upper threshold");
    }
    if (!std::isfinite(options_.arrivalProbability) ||
        options_.arrivalProbability < 0.0 || options_.arrivalProbability > 1.0) {
        throw std::invalid_argument("Arrival probability must be between 0 and 1");
    }
    if (options_.arrivalBatchMin < 1 ||
        options_.arrivalBatchMin > options_.arrivalBatchMax) {
        throw std::invalid_argument("Arrival batch bounds must satisfy 1 <= min <= max");
    }
    if (options_.initialRequestsPerServer >
        std::numeric_limits<std::size_t>::max() / options_.initialServers) {
        throw std::length_error("Initial request count exceeds addressable memory");
    }

    servers_.reserve(options_.initialServers);
    for (std::size_t i = 0; i < options_.initialServers; ++i) {
        servers_.push_back(std::make_unique<WebServer>(nextServerId_++));
    }
    peakServerCount_ = servers_.size();

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

void LoadBalancer::generateArrivals() {
    if (!requestGenerator_.eventOccurs(options_.arrivalProbability)) {
        return;
    }

    ++arrivalEvents_;
    const int batchSize =
        requestGenerator_.uniformInt(options_.arrivalBatchMin, options_.arrivalBatchMax);
    for (int i = 0; i < batchSize; ++i) {
        addRequest(requestGenerator_.make(currentCycle_));
    }
}

void LoadBalancer::addServer() {
    servers_.push_back(std::make_unique<WebServer>(nextServerId_++));
    ++serversAdded_;
    if (servers_.size() > peakServerCount_) {
        peakServerCount_ = servers_.size();
    }
}

bool LoadBalancer::removeIdleServer() {
    // Prefer retiring the newest server so IDs of older capacity remain stable.
    for (auto it = servers_.rbegin(); it != servers_.rend(); ++it) {
        if ((*it)->isIdle()) {
            retiredServerStats_.push_back((*it)->stats());
            servers_.erase(std::next(it).base());
            ++serversRemoved_;
            return true;
        }
    }
    return false;
}

void LoadBalancer::checkScaling() {
    if (!options_.scalingEnabled) {
        return;
    }
    if (hasScaled_ && currentCycle_ - lastScalingCycle_ < options_.scalingCooldown) {
        return;
    }

    const std::size_t serverCount = servers_.size();
    const std::size_t queueSize = queue_.size();
    const std::size_t requestsPerServer = queueSize / serverCount;
    const bool aboveUpper = requestsPerServer > options_.maxRequestsPerServer ||
        (requestsPerServer == options_.maxRequestsPerServer &&
         queueSize % serverCount != 0);
    const bool belowLower = requestsPerServer < options_.minRequestsPerServer;

    if (aboveUpper) {
        if (serverCount >= options_.maxServers) {
            ++scaleUpLimitHits_;
            return;
        }
        addServer();
    } else if (belowLower && serverCount > options_.minServers) {
        if (!removeIdleServer()) {
            ++scaleDownDeferredBusy_;
            return;
        }
    } else {
        return;
    }

    hasScaled_ = true;
    lastScalingCycle_ = currentCycle_;
}

void LoadBalancer::step() {
    ++currentCycle_;
    tickServers();
    generateArrivals();
    checkScaling();
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
    result.randomSeed = options_.randomSeed;
    result.generatedRequests = generatedRequests_;
    result.acceptedRequests = acceptedRequests_;
    result.arrivalEvents = arrivalEvents_;
    result.completedRequests = completedRequests_;
    result.queuedRequests = queue_.size();
    result.serverCount = servers_.size();
    result.peakServerCount = peakServerCount_;
    result.serversAdded = serversAdded_;
    result.serversRemoved = serversRemoved_;
    result.scaleUpLimitHits = scaleUpLimitHits_;
    result.scaleDownDeferredBusy = scaleDownDeferredBusy_;
    result.peakQueueSize = peakQueueSize_ > queue_.peakSize()
                               ? peakQueueSize_
                               : queue_.peakSize();
    result.queueSizeSum = queueSizeSum_;
    result.averageQueueSize = currentCycle_ == 0
                                  ? static_cast<double>(queue_.size())
                                  : static_cast<double>(queueSizeSum_) /
                                        static_cast<double>(currentCycle_);
    result.servers.reserve(retiredServerStats_.size() + servers_.size());
    result.servers.insert(result.servers.end(),
                          retiredServerStats_.begin(), retiredServerStats_.end());
    for (const auto& server : servers_) {
        result.servers.push_back(server->stats());
        if (server->isBusy()) {
            ++result.inFlightRequests;
        }
    }
    return result;
}

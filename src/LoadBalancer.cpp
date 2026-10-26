#include "LoadBalancer.h"
#include "Logger.h"

#include <algorithm>
#include <iterator>
#include <limits>
#include <cmath>
#include <optional>
#include <stdexcept>

LoadBalancer::LoadBalancer(const LoadBalancerOptions& options)
    : options_(options),
      ipFilter_(options.blockedIPRanges),
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
    startingQueueSize_ = queue_.size();
    minimumQueueSize_ = startingQueueSize_;
    startingServerCount_ = servers_.size();
    minimumServerCount_ = servers_.size();
}

bool LoadBalancer::addRequest(const Request& request) {
    if (request.timeCycles <= 0) {
        throw std::invalid_argument("Request processing duration must be positive");
    }
    if (request.arrivalCycle > currentCycle_) {
        throw std::invalid_argument("Request arrival cycle cannot be in the future");
    }
    const bool blocked = ipFilter_.isBlocked(request.ipIn);
    ++generatedRequests_;
    if (blocked) {
        ++rejectedRequests_;
        if (logger_ != nullptr) {
            logger_->event(EventLevel::Block, currentCycle_,
                           "Request " + std::to_string(request.id) +
                               " rejected by source IP filter (" + request.ipIn + ")");
        }
        return false;
    }
    queue_.push(request);
    ++acceptedRequests_;
    return true;
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
            ++assignedRequests_;
            const std::uint64_t wait = currentCycle_ - request.arrivalCycle;
            queueWaitCycleSum_ += wait;
            if (wait > maximumQueueWaitCycles_) maximumQueueWaitCycles_ = wait;
            if (logger_ != nullptr) {
                logger_->event(EventLevel::Assign, currentCycle_,
                               "Request " + std::to_string(request.id) +
                                   " assigned to Server " +
                                   std::to_string(server->getServerId()));
            }
        }
    }
}

void LoadBalancer::tickServers() {
    availableServerCycles_ += servers_.size();
    for (const auto& server : servers_) {
        if (server->isBusy()) ++busyServerCycles_;
        const std::optional<Request> completed = server->tick();
        if (completed.has_value()) {
            ++completedRequests_;
            completedProcessingCycles_ += static_cast<std::uint64_t>(completed->timeCycles);
            if (logger_ != nullptr) {
                logger_->event(EventLevel::Complete, currentCycle_,
                               "Server " + std::to_string(server->getServerId()) +
                                   " completed Request " + std::to_string(completed->id));
            }
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
    if (logger_ != nullptr) {
        logger_->event(EventLevel::Arrival, currentCycle_,
                       "Generated arrival batch of " + std::to_string(batchSize) + " requests");
    }
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
    if (logger_ != nullptr) {
        logger_->event(EventLevel::ScaleUp, currentCycle_,
                       "Added Server " + std::to_string(servers_.back()->getServerId()) +
                           "; active servers=" + std::to_string(servers_.size()));
    }
}

bool LoadBalancer::removeIdleServer() {
    // Prefer retiring the newest server so IDs of older capacity remain stable.
    for (auto it = servers_.rbegin(); it != servers_.rend(); ++it) {
        if ((*it)->isIdle()) {
            const std::size_t removedId = (*it)->getServerId();
            retiredServerStats_.push_back((*it)->stats());
            servers_.erase(std::next(it).base());
            ++serversRemoved_;
            minimumServerCount_ = std::min(minimumServerCount_, servers_.size());
            if (logger_ != nullptr) {
                logger_->event(EventLevel::ScaleDown, currentCycle_,
                               "Removed idle Server " + std::to_string(removedId) +
                                   "; active servers=" + std::to_string(servers_.size()));
            }
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

    if (!aboveUpper) maxServerWarningLogged_ = false;
    if (!belowLower) busyScaleDownWarningLogged_ = false;

    if (aboveUpper) {
        if (serverCount >= options_.maxServers) {
            ++scaleUpLimitHits_;
            if (logger_ != nullptr && !maxServerWarningLogged_) {
                logger_->event(EventLevel::Warning, currentCycle_,
                               "Scale-up blocked at max_servers=" +
                                   std::to_string(options_.maxServers));
            }
            maxServerWarningLogged_ = true;
            return;
        }
        addServer();
    } else if (belowLower && serverCount > options_.minServers) {
        if (!removeIdleServer()) {
            ++scaleDownDeferredBusy_;
            if (logger_ != nullptr && !busyScaleDownWarningLogged_) {
                logger_->event(EventLevel::Warning, currentCycle_,
                               "Scale-down deferred; all servers are busy");
            }
            busyScaleDownWarningLogged_ = true;
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
    minimumQueueSize_ = std::min(minimumQueueSize_, queue_.size());
    const std::size_t currentServerCount = servers_.size();
    const std::size_t perServer = queue_.size() / currentServerCount;
    const bool below = perServer < options_.minRequestsPerServer;
    const bool above = perServer > options_.maxRequestsPerServer ||
        (perServer == options_.maxRequestsPerServer &&
         queue_.size() % currentServerCount != 0);
    if (below) ++cyclesBelowTarget_;
    else if (above) ++cyclesAboveTarget_;
    else ++cyclesWithinTarget_;
}

void LoadBalancer::run() {
    while (currentCycle_ < options_.runCycles) {
        step();
    }
}

void LoadBalancer::setLogger(Logger* logger) {
    logger_ = logger;
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
    result.rejectedRequests = rejectedRequests_;
    result.arrivalEvents = arrivalEvents_;
    result.completedRequests = completedRequests_;
    result.assignedRequests = assignedRequests_;
    result.completedProcessingCycles = completedProcessingCycles_;
    result.queuedRequests = queue_.size();
    result.serverCount = servers_.size();
    result.startingQueueSize = startingQueueSize_;
    result.minimumQueueSize = minimumQueueSize_;
    result.startingServerCount = startingServerCount_;
    result.minimumServerCount = minimumServerCount_;
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
    result.queueWaitCycleSum = queueWaitCycleSum_;
    result.maximumQueueWaitCycles = maximumQueueWaitCycles_;
    result.averageQueueWaitCycles = assignedRequests_ == 0
                                        ? 0.0
                                        : static_cast<double>(queueWaitCycleSum_) /
                                              static_cast<double>(assignedRequests_);
    result.busyServerCycles = busyServerCycles_;
    result.availableServerCycles = availableServerCycles_;
    result.cyclesBelowTarget = cyclesBelowTarget_;
    result.cyclesWithinTarget = cyclesWithinTarget_;
    result.cyclesAboveTarget = cyclesAboveTarget_;
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

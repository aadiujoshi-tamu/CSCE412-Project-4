#include "Switch.h"

#include <limits>
#include <ostream>
#include <stdexcept>
#include <string>

LoadBalancerOptions Switch::makeBalancerOptions(const LoadBalancerOptions& options,
                                                bool processing) {
    if (options.initialServers < 2) {
        throw std::invalid_argument("Switch requires at least two initial servers");
    }
    if (options.initialRequestsPerServer >
        std::numeric_limits<std::size_t>::max() / options.initialServers) {
        throw std::length_error("Switch initial request count exceeds addressable memory");
    }

    LoadBalancerOptions result = options;
    const std::size_t processingServers = options.initialServers / 2 + options.initialServers % 2;
    result.initialServers = processing ? processingServers
                                       : options.initialServers - processingServers;
    result.initialRequestsPerServer = 0;
    result.minServers = 1;
    result.arrivalProbability = 0.0; // arrivals are generated once by the switch
    result.randomSeed = options.randomSeed ^ (processing ? 0x50524F43ULL : 0x5354524DULL);
    return result;
}

Switch::Switch(const LoadBalancerOptions& options)
    : options_(options),
      processingBalancer_(makeBalancerOptions(options, true)),
      streamingBalancer_(makeBalancerOptions(options, false)),
      requestGenerator_(options.randomSeed,
                        options.minRequestDuration,
                        options.maxRequestDuration) {
    const std::size_t initialCount =
        options_.initialServers * options_.initialRequestsPerServer;
    for (std::size_t i = 0; i < initialCount; ++i) {
        (void)routeRequest(requestGenerator_.make(0));
    }
}

bool Switch::routeRequest(const Request& request) {
    if (request.jobType == 'P') {
        ++routedProcessing_;
        return processingBalancer_.addRequest(request);
    }
    if (request.jobType == 'S') {
        ++routedStreaming_;
        return streamingBalancer_.addRequest(request);
    }
    throw std::invalid_argument("Switch accepts only job types 'P' and 'S'");
}

void Switch::routeForNextStep(const Request& request,
                              std::vector<Request>& processing,
                              std::vector<Request>& streaming) {
    if (request.jobType == 'P') {
        ++routedProcessing_;
        processing.push_back(request);
    } else if (request.jobType == 'S') {
        ++routedStreaming_;
        streaming.push_back(request);
    } else {
        throw std::invalid_argument("Generated request has an invalid job type");
    }
}

void Switch::step() {
    const std::uint64_t nextCycle = currentCycle_ + 1;
    std::vector<Request> processingRequests;
    std::vector<Request> streamingRequests;

    if (requestGenerator_.eventOccurs(options_.arrivalProbability)) {
        ++arrivalEvents_;
        const int batchSize = requestGenerator_.uniformInt(options_.arrivalBatchMin,
                                                           options_.arrivalBatchMax);
        for (int i = 0; i < batchSize; ++i) {
            routeForNextStep(requestGenerator_.make(nextCycle),
                             processingRequests, streamingRequests);
        }
    }

    processingBalancer_.stepWithRequests(processingRequests);
    streamingBalancer_.stepWithRequests(streamingRequests);
    currentCycle_ = nextCycle;
}

void Switch::run() {
    while (currentCycle_ < options_.runCycles) {
        step();
    }
}

void Switch::setLogger(Logger* logger) {
    processingBalancer_.setLogger(logger);
    streamingBalancer_.setLogger(logger);
}

SwitchStats Switch::getStatistics() const {
    SwitchStats result;
    result.currentCycle = currentCycle_;
    result.arrivalEvents = arrivalEvents_;
    result.routedProcessing = routedProcessing_;
    result.routedStreaming = routedStreaming_;
    result.processing = processingBalancer_.getStatistics();
    result.streaming = streamingBalancer_.getStatistics();
    result.generatedRequests = result.processing.generatedRequests +
                              result.streaming.generatedRequests;
    result.acceptedRequests = result.processing.acceptedRequests +
                              result.streaming.acceptedRequests;
    result.rejectedRequests = result.processing.rejectedRequests +
                              result.streaming.rejectedRequests;
    result.assignedRequests = result.processing.assignedRequests +
                              result.streaming.assignedRequests;
    result.completedRequests = result.processing.completedRequests +
                               result.streaming.completedRequests;
    result.queuedRequests = result.processing.queuedRequests +
                           result.streaming.queuedRequests;
    result.inFlightRequests = result.processing.inFlightRequests +
                              result.streaming.inFlightRequests;
    result.serverCount = result.processing.serverCount + result.streaming.serverCount;
    return result;
}

void Switch::writeSummary(std::ostream& output) const {
    const SwitchStats stats = getStatistics();
    output << "==================================================\n"
           << "              SWITCH SIMULATION\n"
           << "==================================================\n"
           << "PROCESSING LOAD BALANCER\n"
           << "Generated:       " << stats.processing.generatedRequests << '\n'
           << "Completed:       " << stats.processing.completedRequests << '\n'
           << "Queue Remaining: " << stats.processing.queuedRequests << '\n'
           << "In Progress:     " << stats.processing.inFlightRequests << '\n'
           << "Active Servers:  " << stats.processing.serverCount << '\n'
           << "Scale Events:    " << stats.processing.serversAdded +
                                      stats.processing.serversRemoved << '\n'
           << "\nSTREAMING LOAD BALANCER\n"
           << "Generated:       " << stats.streaming.generatedRequests << '\n'
           << "Completed:       " << stats.streaming.completedRequests << '\n'
           << "Queue Remaining: " << stats.streaming.queuedRequests << '\n'
           << "In Progress:     " << stats.streaming.inFlightRequests << '\n'
           << "Active Servers:  " << stats.streaming.serverCount << '\n'
           << "Scale Events:    " << stats.streaming.serversAdded +
                                      stats.streaming.serversRemoved << '\n'
           << "\nCOMBINED TOTALS\n"
           << "Cycles:          " << stats.currentCycle << '\n'
           << "Requests Routed: " << stats.routedProcessing + stats.routedStreaming << '\n'
           << "Requests Done:   " << stats.completedRequests << '\n'
           << "Requests Waiting:" << stats.queuedRequests << '\n'
           << "In Progress:     " << stats.inFlightRequests << '\n'
           << "Rejected:        " << stats.rejectedRequests << '\n'
           << "Active Servers:  " << stats.serverCount << '\n'
           << "==================================================\n";
}

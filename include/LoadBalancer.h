#ifndef MERIDIAN_LOAD_BALANCER_H
#define MERIDIAN_LOAD_BALANCER_H

#include "RequestGenerator.h"
#include "RequestQueue.h"
#include "WebServer.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

/** Settings needed by the Phase 4 load balancer core. */
struct LoadBalancerOptions {
    std::size_t initialServers = 1;
    std::size_t initialRequestsPerServer = 100;
    std::uint64_t runCycles = 1000;
    std::uint64_t randomSeed = 1;
    int minRequestDuration = 5;
    int maxRequestDuration = 80;
};

/** Snapshot of aggregate load-balancer and server state. */
struct LBStats {
    std::uint64_t currentCycle = 0;
    std::uint64_t generatedRequests = 0;
    std::uint64_t acceptedRequests = 0;
    std::uint64_t completedRequests = 0;
    std::size_t queuedRequests = 0;
    std::size_t inFlightRequests = 0;
    std::size_t serverCount = 0;
    std::size_t peakQueueSize = 0;
    std::uint64_t queueSizeSum = 0;
    double averageQueueSize = 0.0;
    std::vector<ServerStats> servers;
};

/**
 * @brief Owns the FIFO request queue and server pool for one simulation.
 *
 * Phase 4 implements initial workload creation, FIFO scheduling, cycle
 * advancement, and basic accounting. Random arrivals, firewall checks, and
 * autoscaling are added in later project phases.
 */
class LoadBalancer {
public:
    explicit LoadBalancer(const LoadBalancerOptions& options);

    /** Add an already accepted request to the back of the FIFO queue. */
    void addRequest(const Request& request);

    /** Assign queued requests to idle servers in server-ID order. */
    void assignRequests();

    /** Advance the simulation by one cycle using the documented cycle order. */
    void step();

    /** Run exactly the configured number of cycles. */
    void run();

    /** @return Current waiting queue length. */
    std::size_t getQueueSize() const noexcept;

    /** @return Current number of servers. */
    std::size_t getServerCount() const noexcept;

    /** @return Current cycle number (zero before the first step). */
    std::uint64_t getCurrentCycle() const noexcept;

    /** @return Aggregate statistics and a per-server statistics snapshot. */
    LBStats getStatistics() const;

private:
    LoadBalancerOptions options_;
    RequestQueue queue_;
    std::vector<std::unique_ptr<WebServer>> servers_;
    RequestGenerator requestGenerator_;
    std::uint64_t currentCycle_ = 0;
    std::uint64_t generatedRequests_ = 0;
    std::uint64_t acceptedRequests_ = 0;
    std::uint64_t completedRequests_ = 0;
    std::uint64_t queueSizeSum_ = 0;
    std::size_t peakQueueSize_ = 0;

    void tickServers();
};

#endif // MERIDIAN_LOAD_BALANCER_H

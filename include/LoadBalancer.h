#ifndef MERIDIAN_LOAD_BALANCER_H
#define MERIDIAN_LOAD_BALANCER_H

#include "RequestGenerator.h"
#include "RequestQueue.h"
#include "WebServer.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

/** Settings for the load balancer simulation. */
struct LoadBalancerOptions {
    std::size_t initialServers = 1;
    std::size_t initialRequestsPerServer = 100;
    std::uint64_t runCycles = 1000;
    std::uint64_t randomSeed = 1;
    int minRequestDuration = 5;
    int maxRequestDuration = 80;
    std::size_t minServers = 1;
    std::size_t maxServers = 100;
    std::size_t minRequestsPerServer = 50;
    std::size_t maxRequestsPerServer = 80;
    std::uint64_t scalingCooldown = 100;
    bool scalingEnabled = true;
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
    std::size_t peakServerCount = 0;
    std::size_t serversAdded = 0;
    std::size_t serversRemoved = 0;
    std::size_t scaleUpLimitHits = 0;
    std::size_t scaleDownDeferredBusy = 0;
    std::size_t peakQueueSize = 0;
    std::uint64_t queueSizeSum = 0;
    double averageQueueSize = 0.0;
    std::vector<ServerStats> servers;
};

/**
 * @brief Owns the FIFO request queue and server pool for one simulation.
 *
 * Owns the initial workload, FIFO queue, and server pool. Each cycle advances
 * existing work, considers scaling, then assigns waiting work. Random arrivals,
 * firewall checks, configuration-file loading, and logging are added later.
 */
class LoadBalancer {
public:
    explicit LoadBalancer(const LoadBalancerOptions& options);

    /** Add an already accepted request to the back of the FIFO queue. */
    void addRequest(const Request& request);

    /** Assign queued requests to idle servers in server-ID order. */
    void assignRequests();

    /** Evaluate and apply at most one scaling action for the current cycle. */
    void checkScaling();

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
    std::size_t peakServerCount_ = 0;
    std::size_t serversAdded_ = 0;
    std::size_t serversRemoved_ = 0;
    std::size_t scaleUpLimitHits_ = 0;
    std::size_t scaleDownDeferredBusy_ = 0;
    std::uint64_t lastScalingCycle_ = 0;
    std::size_t nextServerId_ = 1;
    bool hasScaled_ = false;
    std::vector<ServerStats> retiredServerStats_;

    void tickServers();
    void addServer();
    bool removeIdleServer();
};

#endif // MERIDIAN_LOAD_BALANCER_H

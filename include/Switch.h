#ifndef MERIDIAN_SWITCH_H
#define MERIDIAN_SWITCH_H

#include "LoadBalancer.h"

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <vector>

/** Combined state for the optional processing/streaming switch simulation. */
struct SwitchStats {
    std::uint64_t currentCycle = 0;
    std::uint64_t arrivalEvents = 0;
    std::uint64_t routedProcessing = 0;
    std::uint64_t routedStreaming = 0;
    std::uint64_t generatedRequests = 0;
    std::uint64_t acceptedRequests = 0;
    std::uint64_t rejectedRequests = 0;
    std::uint64_t assignedRequests = 0;
    std::uint64_t completedRequests = 0;
    std::size_t queuedRequests = 0;
    std::size_t inFlightRequests = 0;
    std::size_t serverCount = 0;
    LBStats processing;
    LBStats streaming;
};

/**
 * @brief Routes processing and streaming jobs to independent load balancers.
 *
 * The two balancers advance one cycle on the same logical clock. Each owns its
 * own FIFO queue, server pool, scaling policy, and statistics.
 */
class Switch {
public:
    /**
     * Construct from shared settings. The initial server pool is split between
     * the two balancers; initial and runtime requests are routed by job type.
     */
    explicit Switch(const LoadBalancerOptions& options);

    /** Route a request into the P or S load balancer. */
    bool routeRequest(const Request& request);

    /** Advance both balancers and the switch by one shared cycle. */
    void step();

    /** Run both balancers for the configured shared number of cycles. */
    void run();

    /** Attach the same event logger to both balancers. */
    void setLogger(Logger* logger);

    /** @return Per-balancer and combined statistics. */
    SwitchStats getStatistics() const;

    /** Write separate and combined totals to a stream. */
    void writeSummary(std::ostream& output) const;

private:
    LoadBalancerOptions options_;
    LoadBalancer processingBalancer_;
    LoadBalancer streamingBalancer_;
    RequestGenerator requestGenerator_;
    std::uint64_t currentCycle_ = 0;
    std::uint64_t arrivalEvents_ = 0;
    std::uint64_t routedProcessing_ = 0;
    std::uint64_t routedStreaming_ = 0;

    static LoadBalancerOptions makeBalancerOptions(const LoadBalancerOptions& options,
                                                   bool processing);
    void routeForNextStep(const Request& request,
                          std::vector<Request>& processing,
                          std::vector<Request>& streaming);
};

#endif // MERIDIAN_SWITCH_H

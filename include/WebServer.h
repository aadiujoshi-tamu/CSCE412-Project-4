#ifndef MERIDIAN_WEB_SERVER_H
#define MERIDIAN_WEB_SERVER_H

#include "Request.h"

#include <cstddef>
#include <cstdint>
#include <optional>

/** @brief Cumulative work counters for one web server. */
struct ServerStats {
    std::size_t serverId = 0;
    std::uint64_t completedCount = 0;
    std::uint64_t busyCycles = 0;
};

/**
 * @brief Simulates one server that handles at most one request at a time.
 *
 * Assigning a request does not consume a processing cycle. Each subsequent
 * call to tick() consumes exactly one cycle. The request completes on the
 * tick that reduces its remaining duration to zero.
 */
class WebServer {
public:
    /** @param serverId Stable identifier assigned by the load balancer. */
    explicit WebServer(std::size_t serverId);

    /** @return true while this server has an active request. */
    bool isBusy() const noexcept;

    /** @return true when the server has no active request. */
    bool isIdle() const noexcept;

    /**
     * @brief Assign a request to an idle, non-retiring server.
     * @param request Request to process; its duration must be positive.
     * @return true if accepted; false if busy or marked for retirement.
     * @throws std::invalid_argument if request.timeCycles is not positive.
     */
    bool assign(const Request& request);

    /**
     * @brief Advance active work by one simulation cycle.
     * @return The completed request on its final processing tick, or
     *         std::nullopt if no request completed during this tick.
     *
     * Calling tick on an idle server has no effect.
     */
    std::optional<Request> tick();

    /**
     * @brief Prevent future assignments and request removal after current work.
     *
     * An idle server marked for retirement can be removed immediately by its
     * owner. A busy server finishes its current request before removal.
     */
    void retireWhenIdle() noexcept;

    /** @return Whether this server is marked for retirement. */
    bool isRetiring() const noexcept;

    /** @return Stable server identifier. */
    std::size_t getServerId() const noexcept;

    /** @return Number of requests completed by this server. */
    std::uint64_t getCompletedCount() const noexcept;

    /** @return Number of cycles spent processing requests. */
    std::uint64_t getBusyCycles() const noexcept;

    /** @return Snapshot of this server's cumulative statistics. */
    ServerStats stats() const noexcept;

private:
    std::size_t serverId_;
    std::optional<Request> currentRequest_;
    int remainingCycles_ = 0;
    std::uint64_t completedCount_ = 0;
    std::uint64_t busyCycles_ = 0;
    bool retiring_ = false;
};

#endif // MERIDIAN_WEB_SERVER_H

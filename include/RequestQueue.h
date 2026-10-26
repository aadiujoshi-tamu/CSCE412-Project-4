#ifndef MERIDIAN_REQUEST_QUEUE_H
#define MERIDIAN_REQUEST_QUEUE_H

#include "Request.h"

#include <cstddef>
#include <queue>

/** @brief FIFO queue of requests waiting for a web server. */
class RequestQueue {
public:
    /** Add a request at the back of the queue. */
    void push(const Request& request);

    /**
     * @brief Remove and return the oldest request.
     * @throws std::out_of_range when the queue is empty.
     */
    Request pop();

    /** @return Whether no requests are waiting. */
    bool empty() const noexcept;

    /** @return Number of requests currently waiting. */
    std::size_t size() const noexcept;

    /** @return Largest queue size observed since construction. */
    std::size_t peakSize() const noexcept;

private:
    std::queue<Request> items_;
    std::size_t peakSize_ = 0;
};

#endif // MERIDIAN_REQUEST_QUEUE_H

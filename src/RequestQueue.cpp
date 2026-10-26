#include "RequestQueue.h"

#include <stdexcept>

void RequestQueue::push(const Request& request) {
    items_.push(request);
    if (items_.size() > peakSize_) {
        peakSize_ = items_.size();
    }
}

Request RequestQueue::pop() {
    if (items_.empty()) {
        throw std::out_of_range("Cannot pop an empty request queue");
    }
    Request request = items_.front();
    items_.pop();
    return request;
}

bool RequestQueue::empty() const noexcept {
    return items_.empty();
}

std::size_t RequestQueue::size() const noexcept {
    return items_.size();
}

std::size_t RequestQueue::peakSize() const noexcept {
    return peakSize_;
}

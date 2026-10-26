#include "RequestGenerator.h"

#include <array>
#include <cmath>
#include <stdexcept>
#include <string>

RequestGenerator::RequestGenerator(std::uint64_t seed,
                                   int minDuration,
                                   int maxDuration)
    : engine_(seed), minDuration_(minDuration), maxDuration_(maxDuration) {
    if (minDuration_ <= 0 || maxDuration_ <= 0 || minDuration_ > maxDuration_) {
        throw std::invalid_argument(
            "Request duration bounds must be positive and minDuration <= maxDuration");
    }
}

Request RequestGenerator::make(std::uint64_t arrivalCycle) {
    if (nextId_ == 0) {
        throw std::overflow_error("Request ID space exhausted");
    }

    Request request;
    request.id = nextId_++;
    request.ipIn = makeIPv4();
    request.ipOut = makeIPv4();
    request.timeCycles = std::uniform_int_distribution<int>(minDuration_, maxDuration_)(engine_);
    request.jobType = std::uniform_int_distribution<int>(0, 1)(engine_) == 0 ? 'P' : 'S';
    request.arrivalCycle = arrivalCycle;
    return request;
}

bool RequestGenerator::eventOccurs(double probability) {
    if (!std::isfinite(probability) || probability < 0.0 || probability > 1.0) {
        throw std::invalid_argument("Probability must be finite and between 0 and 1");
    }
    return std::bernoulli_distribution(probability)(engine_);
}

int RequestGenerator::uniformInt(int minimum, int maximum) {
    if (minimum > maximum) {
        throw std::invalid_argument("Random integer minimum must not exceed maximum");
    }
    return std::uniform_int_distribution<int>(minimum, maximum)(engine_);
}

std::string RequestGenerator::makeIPv4() {
    std::array<int, 4> octets{};
    std::uniform_int_distribution<int> octet(0, 255);
    for (int& part : octets) {
        part = octet(engine_);
    }

    return std::to_string(octets[0]) + "." + std::to_string(octets[1]) + "." +
           std::to_string(octets[2]) + "." + std::to_string(octets[3]);
}

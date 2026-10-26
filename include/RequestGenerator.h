#ifndef MERIDIAN_REQUEST_GENERATOR_H
#define MERIDIAN_REQUEST_GENERATOR_H

#include "Request.h"

#include <cstdint>
#include <random>

/**
 * @brief Generates reproducible randomized requests from a single seeded PRNG.
 *
 * One generator should be created per simulation and reused. With the same
 * seed and the same sequence of calls, the generated requests are identical.
 */
class RequestGenerator {
public:
    /**
     * @param seed Seed for the generator's random engine.
     * @param minDuration Inclusive lower processing-time bound.
     * @param maxDuration Inclusive upper processing-time bound.
     * @throws std::invalid_argument if either duration is nonpositive or the
     *         minimum is greater than the maximum.
     */
    explicit RequestGenerator(std::uint64_t seed,
                              int minDuration = 5,
                              int maxDuration = 80);

    /**
     * @brief Create a request and assign its unique ID and arrival cycle.
     * @param arrivalCycle Cycle in which the request is created.
     * @return Request with valid IPv4 addresses, configured duration, and type.
     * @throws std::overflow_error if the ID space is exhausted.
     */
    Request make(std::uint64_t arrivalCycle);

    /** Return true with the supplied probability using the same seeded engine. */
    bool eventOccurs(double probability);

    /** Draw an inclusive integer using the same seeded engine. */
    int uniformInt(int minimum, int maximum);

private:
    std::mt19937_64 engine_;
    int minDuration_;
    int maxDuration_;
    std::uint64_t nextId_ = 1;

    std::string makeIPv4();
};

#endif // MERIDIAN_REQUEST_GENERATOR_H

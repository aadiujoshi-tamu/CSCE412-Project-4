#ifndef MERIDIAN_REQUEST_H
#define MERIDIAN_REQUEST_H

#include <cstdint>
#include <string>

/**
 * @brief One simulated request handled by Meridian Cloud.
 *
 * A request is a plain data object. Its processing duration is measured in
 * simulation cycles, and its job type is 'P' (processing) or 'S' (streaming).
 */
struct Request {
    std::uint64_t id = 0;             ///< Unique within the generating simulation.
    std::string ipIn;                 ///< IPv4 address of the requester.
    std::string ipOut;                ///< IPv4 destination address.
    int timeCycles = 1;               ///< Required processing ticks; must be positive.
    char jobType = 'P';               ///< 'P' for processing or 'S' for streaming.
    std::uint64_t arrivalCycle = 0;   ///< Cycle when the request entered the system.
};

#endif // MERIDIAN_REQUEST_H

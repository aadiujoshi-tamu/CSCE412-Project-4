#ifndef MERIDIAN_CONFIGURATION_H
#define MERIDIAN_CONFIGURATION_H

#include "LoadBalancer.h"

#include <string>

/** Console/file logging preference loaded here for the later Logger phase. */
enum class LogMode { Quiet, Verbose };

/**
 * @brief Parsed, validated runtime settings for the Meridian simulation.
 */
struct Configuration {
    LoadBalancerOptions simulation;
    LogMode logMode = LogMode::Quiet;
    std::string logFile = "logs/simulation.log";
    bool enableColors = true;

    /** Load key=value settings, apply defaults, and validate all values. */
    static Configuration load(const std::string& path);

    /** Load --config (or config/meridian.cfg), then apply CLI overrides. */
    static Configuration loadFromCommandLine(int argc, char* argv[]);

    /** Validate simulation settings, CIDR ranges, and logging paths/mode. */
    void validate() const;
};

#endif // MERIDIAN_CONFIGURATION_H

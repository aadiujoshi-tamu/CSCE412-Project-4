#ifndef MERIDIAN_LOGGER_H
#define MERIDIAN_LOGGER_H

#include "Configuration.h"
#include "LoadBalancer.h"

#include <cstdint>
#include <fstream>
#include <map>
#include <string>

/** Categories for meaningful simulation events. */
enum class EventLevel {
    Info,
    Arrival,
    Assign,
    Complete,
    Block,
    ScaleUp,
    ScaleDown,
    Warning,
    Error
};

/**
 * @brief Writes plain-text simulation logs and optional colored console output.
 */
class Logger {
public:
    Logger(LogMode mode, const std::string& path, bool enableColors);

    /** Record an event; quiet mode counts it for the final event summary. */
    void event(EventLevel level, std::uint64_t cycle, const std::string& message);

    /** Write settings and the initial queue/server state. */
    void startSummary(const Configuration& config, const LBStats& stats);

    /** Write aggregate counters, performance metrics, and accounting checks. */
    void finalSummary(const Configuration& config, const LBStats& stats);

private:
    LogMode mode_;
    std::ofstream file_;
    bool useConsoleColor_ = false;
    std::map<EventLevel, std::uint64_t> eventCounts_;

    void writeLine(const std::string& line, const std::string& color = "");
    static const char* levelName(EventLevel level) noexcept;
    static const char* levelColor(EventLevel level) noexcept;
};

#endif // MERIDIAN_LOGGER_H

#include "Logger.h"

#include <iomanip>
#include <iostream>
#include <cstdio>
#include <sstream>
#include <stdexcept>

#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace {

bool stdoutIsTerminal() {
#ifdef _WIN32
    return _isatty(_fileno(stdout)) != 0;
#else
    return isatty(STDOUT_FILENO) != 0;
#endif
}

std::string percentage(std::uint64_t numerator, std::uint64_t denominator) {
    if (denominator == 0) return "0.00%";
    std::ostringstream out;
    out << std::fixed << std::setprecision(2)
        << (100.0 * static_cast<double>(numerator) /
            static_cast<double>(denominator)) << '%';
    return out.str();
}

} // namespace

Logger::Logger(LogMode mode, const std::string& path, bool enableColors)
    : mode_(mode), file_(path, std::ios::out | std::ios::trunc),
      useConsoleColor_(enableColors && stdoutIsTerminal()) {
    if (!file_) {
        throw std::runtime_error("Cannot open log file: " + path);
    }
}

void Logger::writeLine(const std::string& line, const std::string& color) {
    file_ << line << '\n';
    if (!file_) {
        throw std::runtime_error("Failed while writing simulation log");
    }
    if (useConsoleColor_ && !color.empty()) {
        std::cout << color << line << "\033[0m\n";
    } else {
        std::cout << line << '\n';
    }
}

const char* Logger::levelName(EventLevel level) noexcept {
    switch (level) {
        case EventLevel::Info: return "INFO";
        case EventLevel::Arrival: return "ARRIVE";
        case EventLevel::Assign: return "ASSIGN";
        case EventLevel::Complete: return "DONE";
        case EventLevel::Block: return "BLOCK";
        case EventLevel::ScaleUp: return "SCALE+";
        case EventLevel::ScaleDown: return "SCALE-";
        case EventLevel::Warning: return "WARN";
        case EventLevel::Error: return "ERROR";
    }
    return "EVENT";
}

const char* Logger::levelColor(EventLevel level) noexcept {
    switch (level) {
        case EventLevel::Info: return "\033[36m";
        case EventLevel::Arrival: return "\033[36m";
        case EventLevel::Assign: return "\033[34m";
        case EventLevel::Complete: return "\033[32m";
        case EventLevel::Block: return "\033[31m";
        case EventLevel::ScaleUp: return "\033[33m";
        case EventLevel::ScaleDown: return "\033[35m";
        case EventLevel::Warning: return "\033[33m";
        case EventLevel::Error: return "\033[1;31m";
    }
    return "";
}

void Logger::event(EventLevel level, std::uint64_t cycle, const std::string& message) {
    ++eventCounts_[level];
    if (mode_ != LogMode::Verbose) return;

    std::ostringstream line;
    line << '[' << std::setw(6) << std::setfill('0') << cycle << "] ["
         << std::setfill(' ') << std::left << std::setw(7) << levelName(level)
         << "] " << message;
    writeLine(line.str(), levelColor(level));
}

void Logger::startSummary(const Configuration& config, const LBStats& stats) {
    std::ostringstream out;
    out << "========================================================\n"
        << "              LOAD BALANCER SIMULATION\n"
        << "                  " << (mode_ == LogMode::Quiet ? "QUIET" : "VERBOSE") << " MODE\n"
        << "========================================================\n"
        << "SIMULATION CONFIGURATION\n"
        << "Initial Servers:                 " << config.simulation.initialServers << '\n'
        << "Total Clock Cycles:              " << config.simulation.runCycles << '\n'
        << "Initial Requests per Server:     " << config.simulation.initialRequestsPerServer << '\n'
        << "Request Processing Range:        " << config.simulation.minRequestDuration
        << " - " << config.simulation.maxRequestDuration << " cycles\n"
        << "Lower Queue Threshold:           " << config.simulation.minRequestsPerServer
        << " requests/server\n"
        << "Upper Queue Threshold:           " << config.simulation.maxRequestsPerServer
        << " requests/server\n"
        << "Scaling Cooldown:                " << config.simulation.scalingCooldown << " cycles\n"
        << "Scaling Enabled:                 " << (config.simulation.scalingEnabled ? "YES" : "NO") << '\n'
        << "Random Seed:                     " << config.simulation.randomSeed << '\n'
        << "Arrival Probability:             " << config.simulation.arrivalProbability << '\n'
        << "Arrival Batch:                   " << config.simulation.arrivalBatchMin
        << " - " << config.simulation.arrivalBatchMax << '\n'
        << "Firewall:                        "
        << (config.simulation.blockedIPRanges.empty() ? "ENABLED (no blocked ranges)" : "ENABLED") << '\n'
        << "Blocked IP Ranges:                ";
    if (config.simulation.blockedIPRanges.empty()) out << "(none)";
    for (std::size_t i = 0; i < config.simulation.blockedIPRanges.size(); ++i) {
        if (i != 0) out << ", ";
        out << config.simulation.blockedIPRanges[i];
    }
    out << "\nLog File:                        " << config.logFile << '\n'
        << "\nINITIAL SYSTEM STATUS\n"
        << "Starting Queue Size:             " << stats.queuedRequests << '\n'
        << "Initial Requests Generated:      " << stats.generatedRequests << '\n'
        << "Initial Requests Accepted:       " << stats.acceptedRequests << '\n'
        << "Initial Requests Rejected:       " << stats.rejectedRequests << '\n'
        << "Starting Active Servers:         " << stats.serverCount << '\n'
        << "Starting Busy Servers:           " << stats.inFlightRequests << '\n'
        << "Starting Idle Servers:           " << stats.serverCount - stats.inFlightRequests << '\n'
        << "========================================================";
    writeLine(out.str(), "\033[1;36m");
}

void Logger::finalSummary(const Configuration& config, const LBStats& stats) {
    const std::uint64_t accounted = stats.rejectedRequests + stats.completedRequests +
                                    stats.queuedRequests + stats.inFlightRequests;
    const bool requestAccounting = accounted == stats.generatedRequests;
    const bool assignmentAccounting =
        stats.assignedRequests == stats.completedRequests + stats.inFlightRequests;
    const std::uint64_t totalScaleEvents = stats.serversAdded + stats.serversRemoved;
    const double utilization = stats.availableServerCycles == 0
                                  ? 0.0
                                  : 100.0 * static_cast<double>(stats.busyServerCycles) /
                                        static_cast<double>(stats.availableServerCycles);
    const double throughput = stats.currentCycle == 0
                                  ? 0.0
                                  : static_cast<double>(stats.completedRequests) /
                                        static_cast<double>(stats.currentCycle);
    const double averageProcessing = stats.completedRequests == 0
                                        ? 0.0
                                        : static_cast<double>(stats.completedProcessingCycles) /
                                              static_cast<double>(stats.completedRequests);

    std::ostringstream out;
    out << "\nREQUEST STATISTICS\n"
        << "Total Requests Generated:        " << stats.generatedRequests << '\n'
        << "Total Requests Accepted:         " << stats.acceptedRequests << '\n'
        << "Total Requests Rejected:         " << stats.rejectedRequests << '\n'
        << "Arrival Batch Events:            " << stats.arrivalEvents << '\n'
        << "Total Requests Assigned:         " << stats.assignedRequests << '\n'
        << "Total Requests Completed:        " << stats.completedRequests << '\n'
        << "Requests Still Processing:       " << stats.inFlightRequests << '\n'
        << "Requests Still in Queue:         " << stats.queuedRequests << '\n'
        << "\nQUEUE STATISTICS\n"
        << "Starting Queue Size:             " << stats.startingQueueSize << '\n'
        << "Ending Queue Size:               " << stats.queuedRequests << '\n'
        << "Minimum Queue Size:              " << stats.minimumQueueSize << '\n'
        << "Maximum Queue Size:              " << stats.peakQueueSize << '\n'
        << "Average Queue Size:              " << std::fixed << std::setprecision(2)
        << stats.averageQueueSize << '\n'
        << "Average Waiting Time:            " << stats.averageQueueWaitCycles << " cycles\n"
        << "Maximum Waiting Time:            " << stats.maximumQueueWaitCycles << " cycles\n"
        << "Cycles Below Target:             " << stats.cyclesBelowTarget << '\n'
        << "Cycles Within Target:            " << stats.cyclesWithinTarget << '\n'
        << "Cycles Above Target:             " << stats.cyclesAboveTarget << '\n'
        << "\nSERVER STATISTICS\n"
        << "Starting Servers:                " << stats.startingServerCount << '\n'
        << "Ending Servers:                  " << stats.serverCount << '\n'
        << "Minimum Active Servers:          " << stats.minimumServerCount << '\n'
        << "Maximum Active Servers:          " << stats.peakServerCount << '\n'
        << "Servers Created:                 " << stats.serversAdded << '\n'
        << "Servers Removed:                 " << stats.serversRemoved << '\n'
        << "Scale-Up Events:                 " << stats.serversAdded << '\n'
        << "Scale-Down Events:               " << stats.serversRemoved << '\n'
        << "Scale-Up Limit Hits:             " << stats.scaleUpLimitHits << '\n'
        << "Scale-Down Busy Deferrals:        " << stats.scaleDownDeferredBusy << '\n'
        << "Busy Servers at End:             " << stats.inFlightRequests << '\n'
        << "Idle Servers at End:             " << stats.serverCount - stats.inFlightRequests << '\n'
        << "Busy Server-Cycles:              " << stats.busyServerCycles << '\n'
        << "Available Server-Cycles:         " << stats.availableServerCycles << '\n'
        << "Average Utilization:              " << utilization << "%\n"
        << "\nFIREWALL STATISTICS\n"
        << "Rejected Requests:               " << stats.rejectedRequests << '\n'
        << "Rejection Rate:                  "
        << percentage(stats.rejectedRequests, stats.generatedRequests) << '\n'
        << "Blocked IP Ranges:                ";
    if (config.simulation.blockedIPRanges.empty()) out << "(none)";
    for (std::size_t i = 0; i < config.simulation.blockedIPRanges.size(); ++i) {
        if (i != 0) out << ", ";
        out << config.simulation.blockedIPRanges[i];
    }
    out << "\n\nPERFORMANCE SUMMARY\n"
        << "Throughput:                      " << throughput << " req/cycle\n"
        << "Average Processing Time:         " << averageProcessing << " cycles\n"
        << "\nPER-SERVER TOTALS\n";
    for (const ServerStats& server : stats.servers) {
        out << "Server " << server.serverId << ": completed=" << server.completedCount
            << ", busy cycles=" << server.busyCycles << '\n';
    }
    out
        << "\nEVENT COUNTS\n";
    for (const auto& entry : eventCounts_) {
        out << levelName(entry.first) << ": " << entry.second << '\n';
    }
    out << "Total Scaling Operations:         " << totalScaleEvents << '\n'
        << "\nFINAL SYSTEM STATUS\n"
        << "Final Clock Cycle:               " << stats.currentCycle << '\n'
        << "Remaining Requests in Queue:     " << stats.queuedRequests << '\n'
        << "Requests Still Processing:       " << stats.inFlightRequests << '\n'
        << "Active Servers:                  " << stats.serverCount << '\n'
        << "Idle Servers:                    " << stats.serverCount - stats.inFlightRequests << '\n'
        << "Rejected Requests:               " << stats.rejectedRequests << '\n'
        << "Request Accounting Check:         " << (requestAccounting ? "PASS" : "FAIL") << '\n'
        << "Assignment Accounting Check:      " << (assignmentAccounting ? "PASS" : "FAIL") << '\n'
        << "Simulation Completed:            "
        << (stats.currentCycle == config.simulation.runCycles ? "YES" : "NO") << '\n'
        << "========================================================\n"
        << "                 END OF SIMULATION\n"
        << "========================================================";
    writeLine(out.str(), "\033[1;37m");
    file_.flush();
}


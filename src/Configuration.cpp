#include "Configuration.h"

#include "IPFilter.h"

#include <charconv>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <system_error>

namespace {

std::string trim(const std::string& value) {
    const std::size_t first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

template <typename Integer>
Integer parseInteger(const std::string& value, const std::string& key) {
    Integer result{};
    const char* first = value.data();
    const char* last = first + value.size();
    const auto parsed = std::from_chars(first, last, result);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != last) {
        throw std::invalid_argument("Invalid integer for '" + key + "': " + value);
    }
    return result;
}

double parseProbability(const std::string& value, const std::string& key) {
    std::size_t consumed = 0;
    double result = 0.0;
    try {
        result = std::stod(value, &consumed);
    } catch (const std::exception&) {
        throw std::invalid_argument("Invalid number for '" + key + "': " + value);
    }
    if (consumed != value.size() || !std::isfinite(result) ||
        result < 0.0 || result > 1.0) {
        throw std::invalid_argument("'" + key + "' must be between 0 and 1");
    }
    return result;
}

bool parseBool(const std::string& value, const std::string& key) {
    if (value == "true" || value == "1") return true;
    if (value == "false" || value == "0") return false;
    throw std::invalid_argument("'" + key + "' must be true or false");
}

std::size_t parseSize(const std::string& value, const std::string& key) {
    const unsigned long long parsed = parseInteger<unsigned long long>(value, key);
    if (parsed > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument("Value for '" + key + "' is too large");
    }
    return static_cast<std::size_t>(parsed);
}

void appendCIDRs(std::vector<std::string>& ranges, const std::string& value) {
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t comma = value.find(',', start);
        const std::size_t end = comma == std::string::npos ? value.size() : comma;
        const std::string range = trim(value.substr(start, end - start));
        if (range.empty()) {
            throw std::invalid_argument("blocked_ranges contains an empty CIDR rule");
        }
        ranges.push_back(range);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
}

} // namespace

Configuration Configuration::load(const std::string& path) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Cannot open configuration file: " + path);
    }

    Configuration config;
    std::set<std::string> seen;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) line.erase(comment);
        line = trim(line);
        if (line.empty()) continue;

        const std::size_t equals = line.find('=');
        if (equals == std::string::npos || equals != line.rfind('=')) {
            throw std::invalid_argument("Expected key=value at " + path + ":" +
                                        std::to_string(lineNumber));
        }
        const std::string key = trim(line.substr(0, equals));
        const std::string value = trim(line.substr(equals + 1));
        if (key.empty() || value.empty()) {
            throw std::invalid_argument("Empty key or value at " + path + ":" +
                                        std::to_string(lineNumber));
        }
        if (key != "blocked_ranges" && key != "block_range" && !seen.insert(key).second) {
            throw std::invalid_argument("Duplicate configuration key '" + key +
                                        "' at line " + std::to_string(lineNumber));
        }

        try {
            if (key == "initial_servers") config.simulation.initialServers = parseSize(value, key);
            else if (key == "clock_cycles") config.simulation.runCycles = parseInteger<std::uint64_t>(value, key);
            else if (key == "initial_requests_per_server") config.simulation.initialRequestsPerServer = parseSize(value, key);
            else if (key == "request_time_min") config.simulation.minRequestDuration = parseInteger<int>(value, key);
            else if (key == "request_time_max") config.simulation.maxRequestDuration = parseInteger<int>(value, key);
            else if (key == "random_seed") config.simulation.randomSeed = parseInteger<std::uint64_t>(value, key);
            else if (key == "arrival_probability") config.simulation.arrivalProbability = parseProbability(value, key);
            else if (key == "arrival_batch_min") config.simulation.arrivalBatchMin = parseInteger<int>(value, key);
            else if (key == "arrival_batch_max") config.simulation.arrivalBatchMax = parseInteger<int>(value, key);
            else if (key == "lower_queue_threshold") config.simulation.minRequestsPerServer = parseSize(value, key);
            else if (key == "upper_queue_threshold") config.simulation.maxRequestsPerServer = parseSize(value, key);
            else if (key == "scaling_cooldown") config.simulation.scalingCooldown = parseInteger<std::uint64_t>(value, key);
            else if (key == "min_servers") config.simulation.minServers = parseSize(value, key);
            else if (key == "max_servers") config.simulation.maxServers = parseSize(value, key);
            else if (key == "scaling_enabled") config.simulation.scalingEnabled = parseBool(value, key);
            else if (key == "blocked_ranges" || key == "block_range") appendCIDRs(config.simulation.blockedIPRanges, value);
            else if (key == "log_mode") {
                if (value == "quiet") config.logMode = LogMode::Quiet;
                else if (value == "verbose") config.logMode = LogMode::Verbose;
                else throw std::invalid_argument("log_mode must be 'quiet' or 'verbose'");
            } else if (key == "log_file") config.logFile = value;
            else if (key == "enable_colors") config.enableColors = parseBool(value, key);
            else throw std::invalid_argument("Unknown configuration key: " + key);
        } catch (const std::exception& error) {
            throw std::invalid_argument(path + ":" + std::to_string(lineNumber) + ": " + error.what());
        }
    }
    if (input.bad()) {
        throw std::runtime_error("Error reading configuration file: " + path);
    }

    config.validate();
    return config;
}

Configuration Configuration::loadFromCommandLine(int argc, char* argv[]) {
    std::string configPath = "config/meridian.cfg";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config") {
            if (i + 1 >= argc) throw std::invalid_argument("--config requires a path");
            configPath = argv[++i];
        }
    }

    Configuration config = load(configPath);
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--config") {
            ++i;
        } else if (arg == "--servers" || arg == "--cycles" || arg == "--seed") {
            if (i + 1 >= argc) throw std::invalid_argument(arg + " requires a value");
            const std::string value = argv[++i];
            if (arg == "--servers") config.simulation.initialServers = parseSize(value, arg);
            else if (arg == "--cycles") config.simulation.runCycles = parseInteger<std::uint64_t>(value, arg);
            else config.simulation.randomSeed = parseInteger<std::uint64_t>(value, arg);
        } else if (arg == "--quiet") {
            config.logMode = LogMode::Quiet;
        } else if (arg == "--verbose") {
            config.logMode = LogMode::Verbose;
        } else {
            throw std::invalid_argument("Unknown command-line argument: " + arg);
        }
    }
    config.validate();
    return config;
}

void Configuration::validate() const {
    const LoadBalancerOptions& s = simulation;
    if (s.initialServers == 0 || s.minServers == 0 || s.maxServers < s.minServers ||
        s.initialServers < s.minServers || s.initialServers > s.maxServers) {
        throw std::invalid_argument("Server counts must satisfy 1 <= min <= initial <= max");
    }
    if (s.initialRequestsPerServer >
        std::numeric_limits<std::size_t>::max() / s.initialServers) {
        throw std::invalid_argument("Initial request count exceeds addressable memory");
    }
    if (s.runCycles == 0) throw std::invalid_argument("clock_cycles must be positive");
    if (s.minRequestDuration <= 0 || s.maxRequestDuration <= 0 ||
        s.minRequestDuration > s.maxRequestDuration) {
        throw std::invalid_argument("Request duration bounds must satisfy 1 <= min <= max");
    }
    if (!std::isfinite(s.arrivalProbability) ||
        s.arrivalProbability < 0.0 || s.arrivalProbability > 1.0) {
        throw std::invalid_argument("arrival_probability must be between 0 and 1");
    }
    if (s.arrivalBatchMin < 1 || s.arrivalBatchMin > s.arrivalBatchMax) {
        throw std::invalid_argument("Arrival batch bounds must satisfy 1 <= min <= max");
    }
    if (s.minRequestsPerServer >= s.maxRequestsPerServer) {
        throw std::invalid_argument("Queue thresholds must satisfy lower < upper");
    }
    if (logFile.empty()) throw std::invalid_argument("log_file cannot be empty");
    IPFilter validatedRanges(s.blockedIPRanges);
    (void)validatedRanges;
}

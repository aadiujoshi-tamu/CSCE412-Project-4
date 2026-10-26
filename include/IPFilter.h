#ifndef MERIDIAN_IP_FILTER_H
#define MERIDIAN_IP_FILTER_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

/** Inclusive numeric IPv4 interval represented by a CIDR rule. */
struct IPRange {
    std::uint32_t firstAddress = 0;
    std::uint32_t lastAddress = 0;
};

/**
 * @brief Checks IPv4 addresses against configured inclusive CIDR ranges.
 */
class IPFilter {
public:
    IPFilter() = default;
    explicit IPFilter(const std::vector<std::string>& cidrRanges);

    /** Add a rule such as "10.0.0.0/8" or "203.0.113.45/32". */
    void addRange(const std::string& cidr);

    /** @return true if the IPv4 address belongs to any configured range. */
    bool isBlocked(const std::string& ip) const;

    /** Convert dotted-decimal IPv4 to a host-independent 32-bit value. */
    static std::uint32_t parseIPv4(const std::string& ip);

    /** @return Number of configured CIDR rules. */
    std::size_t rangeCount() const noexcept;

private:
    std::vector<IPRange> ranges_;
};

#endif // MERIDIAN_IP_FILTER_H

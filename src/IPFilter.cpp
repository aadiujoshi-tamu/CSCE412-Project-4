#include "IPFilter.h"

#include <charconv>
#include <system_error>
#include <stdexcept>

IPFilter::IPFilter(const std::vector<std::string>& cidrRanges) {
    for (const std::string& cidr : cidrRanges) {
        addRange(cidr);
    }
}

std::uint32_t IPFilter::parseIPv4(const std::string& ip) {
    std::uint32_t address = 0;
    std::size_t start = 0;

    for (int octetIndex = 0; octetIndex < 4; ++octetIndex) {
        const std::size_t end = ip.find('.', start);
        const bool lastOctet = octetIndex == 3;
        if ((lastOctet && end != std::string::npos) ||
            (!lastOctet && end == std::string::npos)) {
            throw std::invalid_argument("Malformed IPv4 address: " + ip);
        }

        const std::size_t octetEnd = lastOctet ? ip.size() : end;
        if (octetEnd == start) {
            throw std::invalid_argument("Malformed IPv4 address: " + ip);
        }

        unsigned int octet = 0;
        const char* first = ip.data() + start;
        const char* last = ip.data() + octetEnd;
        const auto parsed = std::from_chars(first, last, octet);
        if (parsed.ec != std::errc{} || parsed.ptr != last || octet > 255) {
            throw std::invalid_argument("Malformed IPv4 address: " + ip);
        }

        address = static_cast<std::uint32_t>((address << 8) | octet);
        start = octetEnd + 1;
    }

    return address;
}

void IPFilter::addRange(const std::string& cidr) {
    const std::size_t slash = cidr.find('/');
    if (slash == std::string::npos || slash == 0 ||
        slash != cidr.rfind('/')) {
        throw std::invalid_argument("CIDR rule must have the form address/prefix: " + cidr);
    }

    const std::string addressText = cidr.substr(0, slash);
    const std::string prefixText = cidr.substr(slash + 1);
    if (prefixText.empty()) {
        throw std::invalid_argument("CIDR prefix is missing: " + cidr);
    }

    unsigned int prefix = 0;
    const char* first = prefixText.data();
    const char* last = first + prefixText.size();
    const auto parsed = std::from_chars(first, last, prefix);
    if (parsed.ec != std::errc{} || parsed.ptr != last || prefix > 32) {
        throw std::invalid_argument("CIDR prefix must be between 0 and 32: " + cidr);
    }

    const std::uint32_t address = parseIPv4(addressText);
    const std::uint32_t mask = prefix == 0
                                   ? 0u
                                   : static_cast<std::uint32_t>(0xFFFFFFFFu << (32 - prefix));
    const std::uint32_t network = address & mask;
    ranges_.push_back(IPRange{network, static_cast<std::uint32_t>(network | ~mask)});
}

bool IPFilter::isBlocked(const std::string& ip) const {
    const std::uint32_t address = parseIPv4(ip);
    for (const IPRange& range : ranges_) {
        if (address >= range.firstAddress && address <= range.lastAddress) {
            return true;
        }
    }
    return false;
}

std::size_t IPFilter::rangeCount() const noexcept {
    return ranges_.size();
}

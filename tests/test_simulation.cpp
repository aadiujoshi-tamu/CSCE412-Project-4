#include "Configuration.h"
#include "IPFilter.h"
#include "LoadBalancer.h"
#include "RequestGenerator.h"
#include "RequestQueue.h"
#include "WebServer.h"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

Request request(std::uint64_t id, int duration, const std::string& source = "203.0.113.9",
                std::uint64_t arrival = 0) {
    Request value;
    value.id = id;
    value.ipIn = source;
    value.ipOut = "192.0.2.1";
    value.timeCycles = duration;
    value.jobType = 'P';
    value.arrivalCycle = arrival;
    return value;
}

void testRequestGeneratorIsDeterministic() {
    RequestGenerator first(9876, 3, 9);
    RequestGenerator second(9876, 3, 9);
    for (std::uint64_t cycle = 0; cycle < 100; ++cycle) {
        const Request a = first.make(cycle);
        const Request b = second.make(cycle);
        assert(a.id == cycle + 1);
        assert(a.id == b.id);
        assert(a.ipIn == b.ipIn && a.ipOut == b.ipOut);
        assert(a.timeCycles == b.timeCycles);
        assert(a.timeCycles >= 3 && a.timeCycles <= 9);
        assert(a.jobType == b.jobType && (a.jobType == 'P' || a.jobType == 'S'));
        assert(a.arrivalCycle == cycle);
    }
}

void testIPv4CIDRBoundaries() {
    IPFilter filter({"203.0.113.0/24", "192.0.2.9/32", "0.0.0.0/0"});
    assert(filter.isBlocked("203.0.113.0"));
    assert(filter.isBlocked("203.0.113.255"));
    assert(filter.isBlocked("192.0.2.9"));
    assert(filter.isBlocked("8.8.8.8"));
    assert(filter.rangeCount() == 3);

    IPFilter narrow({"192.0.2.9/32"});
    assert(narrow.isBlocked("192.0.2.9"));
    assert(!narrow.isBlocked("192.0.2.8"));
    assert(IPFilter::parseIPv4("255.255.255.255") == 0xFFFFFFFFu);

    bool rejected = false;
    try { (void)IPFilter({"192.0.2.0/33"}); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
    rejected = false;
    try { (void)IPFilter::parseIPv4("192.0.2.256"); }
    catch (const std::invalid_argument&) { rejected = true; }
    assert(rejected);
}

void testRequestQueueIsFIFO() {
    RequestQueue queue;
    queue.push(request(4, 1));
    queue.push(request(5, 1));
    assert(queue.size() == 2 && queue.peakSize() == 2);
    assert(queue.pop().id == 4);
    assert(queue.pop().id == 5);
    assert(queue.empty());
}

void testWebServerUsesExactDuration() {
    WebServer server(17);
    assert(server.isIdle());
    assert(server.assign(request(1, 3)));
    assert(server.isBusy());
    assert(!server.assign(request(2, 1)));
    assert(!server.tick().has_value());
    assert(!server.tick().has_value());
    const auto completed = server.tick();
    assert(completed.has_value() && completed->id == 1);
    assert(server.isIdle());
    assert(server.getCompletedCount() == 1);
    assert(server.getBusyCycles() == 3);
}

void testConfigurationAndCLIOverrides() {
    const Configuration fileConfig = Configuration::load("config/meridian.cfg");
    assert(fileConfig.simulation.initialServers == 20);
    assert(fileConfig.simulation.runCycles == 100000);
    assert(fileConfig.simulation.blockedIPRanges.size() == 2);

    char arg0[] = "meridian";
    char arg1[] = "--config";
    char arg2[] = "config/meridian.cfg";
    char arg3[] = "--servers";
    char arg4[] = "4";
    char arg5[] = "--cycles";
    char arg6[] = "12";
    char arg7[] = "--verbose";
    char* argv[] = {arg0, arg1, arg2, arg3, arg4, arg5, arg6, arg7};
    const Configuration overridden = Configuration::loadFromCommandLine(8, argv);
    assert(overridden.simulation.initialServers == 4);
    assert(overridden.simulation.runCycles == 12);
    assert(overridden.logMode == LogMode::Verbose);
}

void testArrivalGenerationAndAccounting() {
    LoadBalancerOptions options;
    options.initialServers = 1;
    options.initialRequestsPerServer = 0;
    options.runCycles = 1;
    options.minRequestDuration = 1;
    options.maxRequestDuration = 1;
    options.arrivalProbability = 1.0;
    options.arrivalBatchMin = 2;
    options.arrivalBatchMax = 2;
    options.scalingEnabled = false;
    LoadBalancer balancer(options);
    balancer.run();
    const LBStats stats = balancer.getStatistics();
    assert(stats.arrivalEvents == 1);
    assert(stats.generatedRequests == 2);
    assert(stats.acceptedRequests == 2);
    assert(stats.assignedRequests == 1);
    assert(stats.queuedRequests == 1);
    assert(stats.inFlightRequests == 1);
    assert(stats.generatedRequests == stats.rejectedRequests + stats.completedRequests +
           stats.queuedRequests + stats.inFlightRequests);
}

void testFirewallRejectsBeforeQueueAdmission() {
    LoadBalancerOptions options;
    options.initialServers = 1;
    options.initialRequestsPerServer = 0;
    options.runCycles = 1;
    options.minRequestDuration = 1;
    options.maxRequestDuration = 1;
    options.scalingEnabled = false;
    options.blockedIPRanges = {"198.51.100.7/32"};
    LoadBalancer balancer(options);
    assert(!balancer.addRequest(request(8, 1, "198.51.100.7")));
    assert(balancer.addRequest(request(9, 1, "198.51.100.8")));
    assert(balancer.getQueueSize() == 1);
    balancer.run();
    const LBStats stats = balancer.getStatistics();
    assert(stats.generatedRequests == 2);
    assert(stats.acceptedRequests == 1);
    assert(stats.rejectedRequests == 1);
    assert(stats.generatedRequests == stats.rejectedRequests + stats.completedRequests +
           stats.queuedRequests + stats.inFlightRequests);
}

void testScalingUpAndServerLimit() {
    LoadBalancerOptions options;
    options.initialServers = 2;
    options.initialRequestsPerServer = 100;
    options.runCycles = 1;
    options.minRequestDuration = 10;
    options.maxRequestDuration = 10;
    options.minServers = 1;
    options.maxServers = 3;
    LoadBalancer balancer(options);
    balancer.run();
    LBStats stats = balancer.getStatistics();
    assert(stats.serversAdded == 1);
    assert(stats.serverCount == 3);
    assert(stats.peakServerCount == 3);

    options.maxServers = 2;
    LoadBalancer capped(options);
    capped.run();
    stats = capped.getStatistics();
    assert(stats.serverCount == 2);
    assert(stats.scaleUpLimitHits == 1);
}

void testScaleDownDefersWhenAllServersBusy() {
    LoadBalancerOptions options;
    options.initialServers = 2;
    options.initialRequestsPerServer = 1;
    options.runCycles = 2;
    options.minRequestDuration = 5;
    options.maxRequestDuration = 5;
    options.minServers = 1;
    options.maxServers = 2;
    options.minRequestsPerServer = 1;
    options.maxRequestsPerServer = 100;
    options.scalingCooldown = 0;
    LoadBalancer balancer(options);
    balancer.run();
    const LBStats stats = balancer.getStatistics();
    assert(stats.scaleDownDeferredBusy == 1);
    assert(stats.serversRemoved == 0);
    assert(stats.serverCount == 2);
    assert(stats.inFlightRequests == 2);
    assert(stats.assignedRequests == stats.completedRequests + stats.inFlightRequests);
}

} // namespace

int main() {
    testRequestGeneratorIsDeterministic();
    testIPv4CIDRBoundaries();
    testRequestQueueIsFIFO();
    testWebServerUsesExactDuration();
    testConfigurationAndCLIOverrides();
    testArrivalGenerationAndAccounting();
    testFirewallRejectsBeforeQueueAdmission();
    testScalingUpAndServerLimit();
    testScaleDownDefersWhenAllServersBusy();
    std::cout << "All Meridian simulation tests passed.\n";
}

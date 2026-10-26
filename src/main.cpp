#include "Configuration.h"
#include "LoadBalancer.h"
#include "Logger.h"

#include <cstdlib>
#include <exception>
#include <iostream>

int main(int argc, char* argv[]) {
    try {
        const Configuration config = Configuration::loadFromCommandLine(argc, argv);
        Logger logger(config.logMode, config.logFile, config.enableColors);
        LoadBalancer balancer(config.simulation);

        logger.startSummary(config, balancer.getStatistics());
        balancer.setLogger(&logger);
        balancer.run();

        const LBStats stats = balancer.getStatistics();
        logger.finalSummary(config, stats);

        const bool requestAccounting = stats.generatedRequests ==
            stats.rejectedRequests + stats.completedRequests +
            stats.queuedRequests + stats.inFlightRequests;
        const bool assignmentAccounting = stats.assignedRequests ==
            stats.completedRequests + stats.inFlightRequests;
        return requestAccounting && assignmentAccounting ? EXIT_SUCCESS : EXIT_FAILURE;
    } catch (const std::exception& error) {
        std::cerr << "Meridian Cloud simulation error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}

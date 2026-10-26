# Meridian Cloud Load Balancer — System Design

This document is the implementation contract for the mandatory C++ simulator. It completes Phase 1 of `project.md`. The design follows the Meridian Cloud identity used by the project website: navy, teal, and gold, with the tagline “Balanced traffic. Steady service.” The optional switch is intentionally outside the mandatory design.

## Goals and boundaries

The program simulates a single FIFO request queue, a dynamically sized pool of web servers, randomized request arrivals, an IPv4 range firewall, and event and summary logging. It does not open network connections. The required reference run is 20 initial servers and 100,000 cycles, with an initial queue of 2,000 requests, random arrivals, scaling and firewall enabled, and quiet logging.

## Components, ownership, and interfaces

`main` owns startup and shutdown orchestration. It loads and validates `Configuration`, constructs the `Logger`, `RequestGenerator`, `IPFilter`, and `LoadBalancer`, asks the balancer to run, and requests the final report. It does not implement scheduling or scaling.

| Component | Owned state | Public interface (proposed C++) | Responsibility |
|---|---|---|---|
| `Request` | `uint64_t id`, `string ipIn`, `string ipOut`, `int timeCycles`, `char jobType`, `uint64_t arrivalCycle` | Plain aggregate struct | Carries a request. `jobType` is `P` or `S`; `timeCycles` is positive. |
| `Configuration` | `LoadBalancerOptions`, log mode, log path, and color setting | `static Configuration load(const string& path)`; `static Configuration loadFromCommandLine(int argc, char* argv[])`; `void validate() const` | Parses key-value settings, applies CLI overrides, validates simulation ranges and CIDRs before simulation starts. |
| `RequestGenerator` | `mt19937_64` engine, inclusive duration bounds, and next request ID | `RequestGenerator(uint64_t seed, int minDuration, int maxDuration)`; `Request make(uint64_t cycle)`; `bool eventOccurs(double probability)`; `int uniformInt(int min, int max)` | Makes valid requests and provides arrival draws using one seeded engine. Identical seed and call order reproduce the same sequence. |
| `IPFilter` | Parsed inclusive IPv4 intervals | `explicit IPFilter(const vector<string>& cidrRanges)`; `void addRange(const string& cidr)`; `bool isBlocked(const string& ip) const`; `static uint32_t parseIPv4(const string& ip)` | Parses IPv4/CIDR as 32-bit values and checks source addresses. Supports `/0` through `/32`; malformed addresses/ranges throw before the simulation starts. |
| `RequestQueue` | `queue<Request>` and peak size | `void push(const Request&)`; `Request pop()`; `bool empty() const`; `size_t size() const`; `size_t peakSize() const` | FIFO storage for accepted requests awaiting assignment. `pop` throws if the queue is empty. |
| `WebServer` | ID, optional active request, remaining cycles, completed count, busy cycles, active/retiring state | `explicit WebServer(size_t id)`; `bool isBusy() const`; `bool isIdle() const`; `bool assign(const Request&)`; `optional<Request> tick()`; `void retireWhenIdle()`; `bool isRetiring() const`; `ServerStats stats() const` | Processes at most one request at a time. Returns a completed request from `tick`; never accesses the shared queue. A retiring server accepts no new work and is removed once idle. |
| `LoadBalancer` | `LoadBalancerOptions`, IP filter, queue, `vector<unique_ptr<WebServer>>`, retired-server stats, clock, generator, cooldown, and counters | `LoadBalancer(const LoadBalancerOptions&)`; `bool addRequest(const Request&)`; `void assignRequests()`; `void checkScaling()`; `void step()`; `void run()`; `size_t getQueueSize() const`; `size_t getServerCount() const`; `LBStats getStatistics() const` | Owns server lifetime and queue; filters generated requests by source IP before admission; provides FIFO assignment, service-cycle advancement, autoscaling, and core statistics. Receives validated options from `Configuration`. |
| `Logger` | Mode, output stream/file, color setting | `Logger(LogMode, const string& path, bool color)`; `void event(EventLevel, const string&)`; `void startSummary(const Configuration&)`; `void finalSummary(const LBStats&)` | Writes the same meaningful event stream to the log file and, as configured, the console. Quiet mode writes only start and final summaries; verbose mode also writes significant events. |

`LoadBalancer` owns each `WebServer` through `unique_ptr`; it owns `RequestQueue` by value, `IPFilter` by value, and `RequestGenerator` by value. `Configuration` produces the validated options passed to the balancer. `WebServer` owns its active request by value/optional. `RequestQueue` owns waiting requests by value. No server mutates the queue. The logger does not own simulation state.

### Relationships

```mermaid
classDiagram
  class main
  class Configuration
  class RequestGenerator
  class Request
  class RequestQueue
  class WebServer
  class IPFilter
  class Logger
  class LoadBalancer
  main --> Configuration
  main --> Logger
  main --> LoadBalancer
  LoadBalancer *-- RequestQueue
  LoadBalancer *-- WebServer
  LoadBalancer --> RequestGenerator
  LoadBalancer --> IPFilter
  LoadBalancer --> Logger
  RequestQueue o-- Request
  WebServer o-- Request
  RequestGenerator ..> Request
```

`LBStats` records the random seed, arrival-event count, generated, accepted, rejected, completed, queued-at-end, and in-flight-at-end requests; servers added/removed; peak and final server counts; queue peak and sum/average; scaling-limit/deferred-scale-down counters; and per-server completion and busy-cycle counts. “Generated” counts all created requests, “rejected” counts generated requests denied by the filter, and “accepted” counts requests placed in the queue. Therefore `generated = accepted + rejected` at every completed run. In-flight requests are reported separately from queued work and are not counted complete.

## Simulation contract

The clock begins at cycle 0. The initial queue is filled before cycle 1 with `initialServers * initialRequestsPerServer` generated requests stamped with arrival cycle 0. The configured queue multiplier defaults to 100. Until Phase 7 integrates the firewall, all generated requests are accepted.

The Phase 4 through 8 implementation provides the queue/server simulation, dynamic scaling, configurable random arrivals, the firewall, and configuration parsing. Every generated initial or runtime request is checked against the configured source-address ranges before admission. Logging and a complete executable entry point are still subsequent work.

For each cycle `c` from 1 through `runCycles`, execute these steps in order:

1. Advance every request that was already processing at the start of the cycle by one cycle. A duration-5 request assigned after cycle 1's tick completes on cycle 6's tick: it receives exactly five ticks, beginning on the cycle after assignment. A request completing in this step makes its server idle.
2. Draw one arrival event with the configured per-cycle probability. If it occurs, draw an inclusive batch size and generate that many requests stamped with cycle `c`. Check each request's source IP: blocked requests count as rejected and never enter the queue; allowed requests are enqueued and count as accepted.
3. Evaluate scaling at most once. Scaling uses the waiting queue and current server count. It is eligible when the cooldown has expired. If queue size is greater than `maxPerServer * serverCount`, add one server. Otherwise, if queue size is less than `minPerServer * serverCount` and more than the minimum server count exists, remove one idle server. High threshold is checked first; at most one action occurs per check. Comparisons are strict (`>` and `<`).
4. Assign requests FIFO to idle servers until either the queue is empty or no idle server remains. Newly assigned work does not receive a tick in the same cycle.
5. Record end-of-cycle queue size, server counts, and busy counts; update aggregate statistics; then advance the clock to `c`.

Cooldown is measured in cycles between successful server-count changes. Once a scale action occurs, no later scale action is permitted until `scalingCooldown` cycles have passed. An unsuccessful check (queue in range or a scaling limit reached) does not restart cooldown. Setting cooldown to zero allows a check each cycle, still limited to one action per cycle. Scale-down removes only an idle server; if every server is busy, the action is deferred and retried at the next eligible check. The pool never falls below `minServers` or grows above `maxServers`. At the configured final cycle, the simulation stops without draining. Remaining queued and in-flight requests are reported separately.

Random arrival settings define a per-cycle arrival probability and an inclusive batch-size range. A successful arrival event generates a batch; a failed event generates none. One seeded engine drives arrival decisions, batch size, and request attributes in a fixed order, so identical configuration and seed reproduce the same run. IPv4 addresses are sampled across all four octets. CIDR firewall rules are converted to inclusive 32-bit intervals and inspect the incoming source address (`ipIn`); `/0` and `/32` are handled without invalid shifts. For reliable firewall checks, construct requests inside and outside a rule instead of relying on random address generation to hit a narrow range.

## Logging and configuration decisions

Configuration is validated before any servers or requests are created. Counts and durations must be positive where required; the minimum duration cannot exceed the maximum; probabilities must be in `[0,1]`; lower scaling threshold must be below upper threshold; blocked ranges must be valid CIDR IPv4 ranges. CLI overrides are applied after file values (`--config`, `--servers`, `--cycles`, `--seed`, `--quiet`, and `--verbose`). The reference config uses quiet mode and a 20-server, 100,000-cycle run. Exact request-duration range and arrival settings used in a submitted log must be stated in that run's configuration and summary.

Quiet mode emits one startup configuration summary and one end summary to console and file. Verbose mode additionally records accepted/rejected requests, assignments, completions, and each server scale/retirement event; it does not print every idle cycle. Console colors are presentation only (teal/cyan headings, green scale-up, gold scale-down, red firewall rejection) and are disabled when output is not a terminal. The log file remains plain text.

## Implementation file map

```text
include/Request.h
include/IPFilter.h              src/IPFilter.cpp
include/RequestQueue.h          src/RequestQueue.cpp
include/Configuration.h        src/Configuration.cpp
include/RequestGenerator.h    src/RequestGenerator.cpp
include/WebServer.h           src/WebServer.cpp
include/Logger.h              src/Logger.cpp
include/LoadBalancer.h        src/LoadBalancer.cpp
src/main.cpp
config/meridian.cfg
logs/.gitkeep
Makefile
Doxyfile
```

The interfaces above are the agreed design surface; implementation may adjust syntax to fit the C++ standard selected by the Makefile, while preserving ownership and behavior. Bonus routing, multiple balancers, and the switch are deferred until the mandatory simulator is complete.

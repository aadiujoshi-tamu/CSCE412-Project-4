# Meridian Cloud Load Balancer — Project Documentation

## 1. Project overview

Meridian Cloud is a C++17 discrete-time load balancer simulation. It generates synthetic IPv4 web requests, admits or rejects them using configurable CIDR firewall rules, queues accepted work FIFO, and assigns requests to web servers. Each request uses its configured number of clock cycles. The load balancer adds or removes servers as the waiting queue changes and reports execution statistics to the terminal and a plain-text log file.

The simulation does not open sockets or serve real network traffic. Its clock models asynchronous request processing without external networking.

## 2. Main features

- Seeded, reproducible request generation with unique IDs, IPv4 addresses, processing durations, and P/S job types.
- Initial workload of `initial_servers × initial_requests_per_server` requests.
- Random per-cycle arrival events with a configurable inclusive batch-size range.
- FIFO request queue and one active request per server.
- Configurable queue-based scaling, cooldown, and server-count limits.
- IPv4 CIDR source filtering before queue admission.
- Quiet and verbose logging, with terminal-only ANSI colors and plain-text file logs.
- Request, queue, server, wait-time, utilization, throughput, and accounting statistics.
- Optional bonus `Switch` class with separate processing and streaming load balancers.

## 3. Source layout

| Path | Purpose |
|---|---|
| `include/Request.h` | Request data structure. |
| `include/RequestGenerator.h`, `src/RequestGenerator.cpp` | Seeded random request and arrival-event generation. |
| `include/RequestQueue.h`, `src/RequestQueue.cpp` | FIFO request storage. |
| `include/WebServer.h`, `src/WebServer.cpp` | Request execution and per-server counters. |
| `include/IPFilter.h`, `src/IPFilter.cpp` | IPv4 parsing and CIDR range checks. |
| `include/LoadBalancer.h`, `src/LoadBalancer.cpp` | Queue/server ownership, simulation loop, scaling, and statistics. |
| `include/Configuration.h`, `src/Configuration.cpp` | Key/value config parsing, validation, and CLI overrides. |
| `include/Logger.h`, `src/Logger.cpp` | Quiet/verbose event output and final summaries. |
| `include/Switch.h`, `src/Switch.cpp` | Optional P/S routing across two balancers on one logical clock. |
| `src/main.cpp` | Loads configuration, runs the mandatory simulation, and writes summaries. |
| `config/meridian.cfg` | Required 20-server, 100,000-cycle reference settings. |
| `config/demo.cfg` | Short verbose scenario for the video. |
| `logs/simulation.log` | Measured required reference-run output. |
| `tests/` | Automated test suite and test plan. |
| `docs/design.md` | System architecture and simulation contract. |
| `docs/tuning.md` | Test results and measured workload comparison. |
| `Doxyfile`, `Makefile` | API documentation and build targets. |

## 4. Request and server model

Every `Request` contains:

| Field | Meaning |
|---|---|
| `id` | Unique ID from the simulation's request generator. |
| `ipIn` | Source/client IPv4 address checked by the firewall. |
| `ipOut` | Destination IPv4 address. |
| `timeCycles` | Positive processing duration in clock cycles. |
| `jobType` | `P` processing or `S` streaming. |
| `arrivalCycle` | Cycle in which the request entered the system. |

`WebServer::assign()` accepts work only while idle. Assignment itself does not consume a cycle. Each subsequent `tick()` advances the active request by one cycle; the server returns the request exactly on its final processing tick. A completed server becomes idle and can receive more work on a later assignment pass.

## 5. Clock-cycle and admission behavior

The initial queue is created at cycle 0. For each cycle from 1 through the configured run length, the load balancer:

1. Advances requests that were already in service and records completions.
2. Draws the random arrival event. If it occurs, it generates an inclusive-size request batch.
3. Checks every generated request's source IP. A blocked request increments generated and rejected counts but never enters the queue. An allowed request is enqueued and increments accepted count.
4. Evaluates autoscaling at most once.
5. Assigns the oldest queued requests to idle servers.
6. Records end-of-cycle queue/server observations.

Newly assigned requests do not receive a processing tick during their assignment cycle. At the end of the configured run, waiting and in-progress requests remain visible in the summary; the simulator does not drain them implicitly.

## 6. Scaling policy

Let `Q` be the waiting queue size and `S` the current server count. The configured default target band is:

- Scale up when `Q > upper_queue_threshold × S`.
- Scale down when `Q < lower_queue_threshold × S` and an idle server can be removed.
- Keep the pool unchanged at either boundary or inside the band.

Only one server is added or removed per eligible check. A successful change starts the cooldown. The pool respects `min_servers` and `max_servers`. Scale-down removes idle servers only; if every server is busy, the check is deferred without abandoning active work.

## 7. Firewall behavior

The configuration accepts one or more CIDR IPv4 ranges. For example, `192.168.50.0/24` covers addresses from `192.168.50.0` through `192.168.50.255`. CIDR prefixes from `/0` through `/32` are supported. The filter converts each range to an inclusive 32-bit interval and checks `ipIn` before enqueueing. Invalid addresses/rules are rejected during configuration loading or request admission.

## 8. Configuration reference

The default file is `config/meridian.cfg`. Values can be edited without changing source code.

| Key | Meaning | Reference value |
|---|---|---:|
| `initial_servers` | Starting server count | `20` |
| `clock_cycles` | Number of simulation cycles | `100000` |
| `initial_requests_per_server` | Initial queue multiplier | `100` |
| `request_time_min`, `request_time_max` | Inclusive request duration range | `10`, `100` |
| `random_seed` | Reproducibility seed | `12345` |
| `arrival_probability` | Chance of a batch each cycle, from 0 to 1 | `0.10` |
| `arrival_batch_min`, `arrival_batch_max` | Inclusive batch-size range | `1`, `5` |
| `lower_queue_threshold` | Scale-down band edge per server | `50` |
| `upper_queue_threshold` | Scale-up band edge per server | `80` |
| `scaling_cooldown` | Cycles between successful scaling actions | `100` |
| `min_servers`, `max_servers` | Pool limits | `1`, `100` |
| `scaling_enabled` | Enables autoscaling | `true` |
| `log_mode` | `quiet` or `verbose` | `quiet` |
| `log_file` | Plain-text log destination | `logs/simulation.log` |
| `enable_colors` | Enables colors on terminal output | `true` |
| `blocked_ranges` | Comma-separated CIDR rules | `10.0.0.0/16,192.168.50.0/24` |

Configuration-file values are loaded first. CLI overrides are applied after them:

```text
--config PATH
--servers N
--cycles N
--seed N
--quiet
--verbose
```

For example:

```bash
./loadbalancer --config config/meridian.cfg --servers 5 --cycles 1000 --verbose
```

## 9. Build, run, and test

Build the application:

```bash
make clean
make
```

Run the reference configuration:

```bash
./loadbalancer --config config/meridian.cfg --quiet
```

Run the short demo profile:

```bash
./loadbalancer --config config/demo.cfg --servers 5 --cycles 120 --verbose
```

Run automated checks:

```bash
make test
```

The suite covers request generation, IPv4/CIDR boundaries, FIFO behavior, exact server timing, configuration and CLI overrides, arrivals, firewall rejection, scaling limits, busy-server scale-down, switch routing/shared cycles, and a CLI log smoke run.

## 10. Logs and statistics

Quiet mode writes a startup configuration/status summary and an end summary. Verbose mode additionally logs arrivals, assignments, completions, firewall rejections, scaling changes, and useful scaling warnings. ANSI colors are only used on terminal output; the file log remains plain text.

The end summary reports generated, accepted, rejected, assigned, completed, waiting, and in-progress request counts; queue minimum/maximum/average and wait times; server minimum/maximum/final counts; scaling actions; busy/available server-cycles and utilization; throughput; per-server totals; and request/assignment accounting checks.

Accounting invariants at shutdown:

```text
generated = rejected + completed + waiting + in_progress
assigned  = completed + in_progress
```

## 11. Reference-run results

The committed `logs/simulation.log` is the required 20-server, 100,000-cycle quiet run, using 10–100 cycle jobs and seed 12345. It generated 31,934 requests, completed 31,043, rejected 1, ended with 874 queued and 16 in progress, and finished with 16 servers. Both accounting checks passed. Full results and a 0.05 arrival-probability comparison are in [`tuning.md`](tuning.md).

## 12. Doxygen API reference

Generate HTML documentation with Doxygen installed:

```bash
make docs
```

The page is `docs/doxygen/html/index.html`. The configuration includes public headers, implementation files, this guide, and the design/tuning documents. Graphviz is optional; without it, Doxygen generates API pages without call graphs.

## 13. Optional switch

The `Switch` class is a library component rather than a mode in `main.cpp`. Construct it with `LoadBalancerOptions`; it splits the initial server pool between processing and streaming balancers, routes `P` and `S` requests to independent queues, advances both on one shared clock, and exposes separate plus combined statistics. The test suite verifies routing, synchronized cycle counts, and independent scaling. The main executable does not currently provide a `--switch` command-line flag.

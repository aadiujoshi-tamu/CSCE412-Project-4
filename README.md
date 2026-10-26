# Meridian Cloud Load Balancer

A C++17 discrete-time simulation of a cloud request load balancer. It models randomized requests, FIFO scheduling, request processing across multiple servers, queue-based server scaling, CIDR firewall rules, and quiet or verbose logs.

## Build

```bash
make
```

The executable is `./loadbalancer`. Object files are stored under `build/`.

## Run

The default configuration is `config/meridian.cfg`:

```bash
./loadbalancer
./loadbalancer --servers 20 --cycles 100000 --quiet
./loadbalancer --config config/meridian.cfg --servers 5 --cycles 1000 --verbose
```

Command-line values override configuration-file values. Logs are written to the configured path; the reference configuration writes `logs/simulation.log`.

Use `make run ARGS="--servers 5 --cycles 1000 --quiet"` to build and run in one command.

## Test

```bash
make test
```

The tests cover request generation, FIFO behavior, server timing, configuration, firewall ranges, arrivals, scaling, request accounting, and a short CLI/log run.

## Documentation

With Doxygen installed, generate the API reference with:

```bash
make docs
```

Generated HTML is placed in `docs/doxygen/html/`. The full project guide and recording script are in [`docs/project_documentation.md`](docs/project_documentation.md) and [`docs/demo_script.md`](docs/demo_script.md). Design and test/tuning notes are in [`docs/design.md`](docs/design.md) and [`docs/tuning.md`](docs/tuning.md). The branded project design site is in [`website/index.html`](website/index.html).

## Optional processing/streaming switch

`Switch` is the bonus logical-concurrency component. Construct it with shared `LoadBalancerOptions`, call `routeRequest()` for externally supplied requests, then call `run()` or advance it with `step()`. It creates randomized initial/runtime requests, sends `P` and `S` jobs to separate load balancers, advances both on the same clock, and exposes separate plus combined statistics with `getStatistics()` and `writeSummary()`.

## AI Disclaimer

AI was utilized as a pair programmer in implementing the source code files and execution scripts, along with project structure and code structure decisions.
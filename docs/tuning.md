# Meridian Cloud Simulation Test and Tuning Notes

## Automated checks

Run `bash tests/run_tests.sh`. It compiles with C++17 warnings enabled, runs the component/integration checks, then builds the CLI and performs a short quiet-mode run. The suite covers deterministic request generation, IPv4/CIDR boundaries and invalid inputs, FIFO behavior, exact processing durations, config parsing and CLI precedence, arrival accounting, firewall rejection, server scaling limits, and safe deferred scale-down. The CLI smoke check confirms both accounting checks pass and that the log contains no ANSI escape codes.

## Required reference run

The committed `logs/simulation.log` was generated with:

- Initial servers: 20
- Cycles: 100,000
- Initial requests: 100 per server (2,000 total)
- Processing time: 10–100 cycles
- Arrival probability: 0.10 per cycle
- Batch size: 1–5, average 3
- Scaling thresholds: 50–80 queued requests per server
- Scaling cooldown: 100 cycles
- Random seed: 12345
- Firewall: enabled with `10.0.0.0/16` and `192.168.50.0/24`
- Log mode: quiet

Measured results:

| Metric | Result |
|---|---:|
| Generated / accepted / rejected | 31,934 / 31,933 / 1 |
| Completed | 31,043 |
| Waiting / in progress at end | 874 / 16 |
| Minimum / maximum / average queue | 833 / 2,000 / 1,085.34 |
| Average / maximum queue wait | 3,451.41 / 4,413 cycles |
| Starting / minimum / maximum / ending servers | 20 / 16 / 25 / 16 |
| Servers added / removed | 6 / 10 |
| Busy / available server-cycles | 1,704,130 / 1,704,150 |
| Utilization | 100.00% (rounded) |
| Throughput | 0.3104 requests/cycle |

Both request accounting (`generated = rejected + completed + waiting + in progress`) and assignment accounting (`assigned = completed + in progress`) passed.

## Arrival-rate comparison

A second 100,000-cycle run used the same seed and settings with arrival probability reduced from `0.10` to `0.05`. Its log was temporary and is not part of the deliverable.

| Metric | 0.10 required config | 0.05 comparison |
|---|---:|---:|
| Generated | 31,934 | 17,162 |
| Completed | 31,043 | 16,696 |
| Queue at end | 874 | 457 |
| Average queue | 1,085.34 | 584.83 |
| Average queue wait | 3,451.41 | 3,451.93 cycles |
| Peak / ending servers | 25 / 16 | 24 / 9 |
| Utilization | 100.00% | 100.00% |

The lower arrival probability reduced total arrivals, average and final queue sizes, and ending server count. Utilization remained near 100% because both runs begin with 2,000 requests and scale down only one idle server per cooldown. Their average wait remains high because the initial backlog dominates the early simulation. The required settings end with about 54.6 queued requests per server, inside the configured 50–80 target band; the comparison ends near the lower edge at about 50.8 per server.

For the required submission, retain the assigned 20-server, 100,000-cycle run and report the 10–100 cycle processing range with the measured log. The lower arrival-rate run is useful as a comparison, but does not replace the required configuration.

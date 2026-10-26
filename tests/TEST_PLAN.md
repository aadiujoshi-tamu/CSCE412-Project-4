# Meridian Cloud Simulation Test Plan

Run the automated checks with `bash tests/run_tests.sh`. The runner builds into a temporary directory and removes the binary afterward.

The suite covers:

- Seeded request generation, unique IDs, valid duration bounds, and request types.
- IPv4 parsing, CIDR `/0`, `/24`, and `/32` boundaries, and malformed inputs.
- FIFO queue order and exact web-server processing duration.
- Configuration-file parsing and command-line override priority.
- Runtime arrivals and generated/accepted/queued/in-flight accounting.
- Firewall rejection before queue admission.
- Scale-up and maximum-server enforcement.
- Deferred scale-down when all servers are processing.
- End-to-end CLI startup, quiet-mode summaries, accounting checks, and plain-text log output.

#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
TEST_BUILD_DIR="$(mktemp -d)"
trap 'rm -rf "$TEST_BUILD_DIR"' EXIT

cd "$PROJECT_DIR"
g++ -std=c++17 -Wall -Wextra -Wpedantic -Iinclude \
    tests/test_simulation.cpp \
    src/Configuration.cpp src/IPFilter.cpp src/LoadBalancer.cpp src/Logger.cpp src/Switch.cpp \
    src/RequestGenerator.cpp src/RequestQueue.cpp src/WebServer.cpp \
    -o "$TEST_BUILD_DIR/meridian_tests"
"$TEST_BUILD_DIR/meridian_tests"

g++ -std=c++17 -Wall -Wextra -Wpedantic -Iinclude \
    src/main.cpp src/Configuration.cpp src/IPFilter.cpp src/LoadBalancer.cpp \
    src/Logger.cpp src/RequestGenerator.cpp src/RequestQueue.cpp src/Switch.cpp src/WebServer.cpp \
    -o "$TEST_BUILD_DIR/loadbalancer"
sed "s#^log_file=.*#log_file=$TEST_BUILD_DIR/smoke.log#" \
    config/meridian.cfg > "$TEST_BUILD_DIR/smoke.cfg"
"$TEST_BUILD_DIR/loadbalancer" --config "$TEST_BUILD_DIR/smoke.cfg" \
    --servers 1 --cycles 50 --quiet > /dev/null
grep -q "Request Accounting Check:         PASS" "$TEST_BUILD_DIR/smoke.log"
grep -q "Assignment Accounting Check:      PASS" "$TEST_BUILD_DIR/smoke.log"
if grep -q $'\033' "$TEST_BUILD_DIR/smoke.log"; then
    echo "ANSI color codes found in plain-text log" >&2
    exit 1
fi
echo "CLI and quiet-log smoke check passed."

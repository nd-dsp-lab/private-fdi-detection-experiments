#!/bin/bash
# bench_sgx.sh - SGX Smart Grid Benchmark (Updated for sum-based benchmarking)

set -euo pipefail

# Configuration
RATE=${RATE:-1000}
SERVER_HOST=${SERVER_HOST:-localhost}
SERVER_PORT=${SERVER_PORT:-8890}
SUMS=${SUMS:-100} # Number of power summations instead of duration
EXPECTED_DEVICES=${EXPECTED_DEVICES:-$((RATE / 10))}

# SGX server executable
SERVER_EXEC="./smart_grid_server_sgx"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

print_header() {
    echo -e "\n${CYAN}${BOLD}=================================="
    echo -e "   SMART GRID BENCHMARK (SGX)"
    echo -e "==================================${NC}\n"
}

print_config() {
    echo -e "${BLUE}Configuration:${NC}"
    echo -e "  Rate: ${BOLD}${RATE}${NC} messages/second"
    echo -e "  Target: ${BOLD}${SUMS}${NC} power summations"
    echo -e "  Expected readings per sum: ${BOLD}${EXPECTED_DEVICES}${NC}"
    echo -e "  Estimated total readings: ${BOLD}$((SUMS * EXPECTED_DEVICES))${NC}"
    echo -e "  Server: ${BOLD}${SERVER_HOST}:${SERVER_PORT}${NC}"
    echo -e "  ${YELLOW}Running in Intel SGX enclave${NC}"
    echo
}

check_sgx_support() {
    echo -e "${BLUE}[CHECK]${NC} Verifying SGX support..."
    if ! command -v gramine-sgx >/dev/null 2>&1; then
        echo -e "${RED}[ERROR]${NC} Gramine SGX not found"
        echo -e "${YELLOW}[INFO]${NC} Install Gramine: https://gramine.readthedocs.io/"
        return 1
    fi
    if [ ! -c /dev/sgx_enclave ] && [ ! -c /dev/isgx ]; then
        echo -e "${YELLOW}[WARNING]${NC} SGX device not found - may run in simulation mode"
    fi
    echo -e "${GREEN}[OK]${NC} SGX environment ready"
    return 0
}

check_dependencies() {
    echo -e "${BLUE}[CHECK]${NC} Verifying dependencies..."
    if ! check_sgx_support; then return 1; fi

    if [ ! -f "$SERVER_EXEC" ] || [ ! -f "smart_grid_server.manifest.sgx" ]; then
        echo -e "${YELLOW}[BUILD]${NC} Building SGX server..."
        if ! ./build_sgx.sh; then
            echo -e "${RED}[ERROR]${NC} SGX server build failed"
            return 1
        fi
    fi
    if ! python3 -c "import cryptography" 2>/dev/null; then
        echo -e "${RED}[ERROR]${NC} Missing Python dependency: cryptography"
        echo -e "${YELLOW}[INFO]${NC} Install with: pip install cryptography"
        return 1
    fi
    if netstat -tlnp 2>/dev/null | grep -q ":$SERVER_PORT "; then
        echo -e "${RED}[ERROR]${NC} Port $SERVER_PORT is already in use"
        return 1
    fi
    echo -e "${GREEN}[OK]${NC} All dependencies satisfied"
    return 0
}

start_sgx_server() {
    echo -e "${BLUE}[SERVER]${NC} Starting SGX server..."
    local sum_interval=$EXPECTED_DEVICES

    # Redirect server output to a log file for debugging
    gramine-sgx smart_grid_server \
        --port $SERVER_PORT \
        --devices $EXPECTED_DEVICES \
        --sum-interval $sum_interval \
        --benchmark-sums $SUMS \
        --metrics "metrics/benchmark_results.csv" \
        --quiet &> sgx_server.log &
    SERVER_PID=$!

    echo -e "${YELLOW}[WAIT]${NC} Waiting for SGX enclave initialization (up to 20s)..."
    local retries=10
    while [ $retries -gt 0 ]; do
        if ! kill -0 $SERVER_PID 2>/dev/null; then
            echo -e "${RED}[ERROR]${NC} SGX server failed to start. Check sgx_server.log."
            tail -n 20 sgx_server.log
            return 1
        fi
        if netstat -tlnp 2>/dev/null | grep -q ":$SERVER_PORT "; then
            break
        fi
        sleep 2
        retries=$((retries - 1))
    done

    if [ $retries -eq 0 ]; then
        echo -e "${RED}[ERROR]${NC} Server not listening on port $SERVER_PORT after timeout"
        echo -e "${YELLOW}[INFO]${NC} Check sgx_server.log for details:"
        tail -n 20 sgx_server.log
        kill $SERVER_PID 2>/dev/null
        return 1
    fi

    echo -e "${GREEN}[OK]${NC} SGX server started (PID: $SERVER_PID)"
    return 0
}

start_generator() {
    echo -e "${BLUE}[GENERATOR]${NC} Starting cipher generator..."
    python3 -m client.simulator \
        --rate $RATE \
        --host $SERVER_HOST \
        --port $SERVER_PORT &
    GENERATOR_PID=$!
    sleep 2
    if ! kill -0 $GENERATOR_PID 2>/dev/null; then
        echo -e "${RED}[ERROR]${NC} Generator failed to start"
        return 1
    fi
    echo -e "${GREEN}[OK]${NC} Generator started (PID: $GENERATOR_PID)"
    return 0
}

monitor_benchmark() {
    echo -e "${CYAN}[RUNNING]${NC} SGX benchmark active - waiting for ${SUMS} power summations..."
    echo -e "${YELLOW}[INFO]${NC} Press Ctrl+C to stop early"
    echo

    local start_time=$(date +%s)
    local last_check=0

    while kill -0 $SERVER_PID 2>/dev/null; do
        local current_time=$(date +%s)
        local elapsed=$((current_time - start_time))

        if [ $((elapsed % 10)) -eq 0 ] && [ $elapsed -ne $last_check ]; then
            local expected_messages=$((RATE * elapsed))
            echo -e "${CYAN}[PROGRESS]${NC} ${elapsed}s elapsed (expected: ${expected_messages} messages sent)"
            last_check=$elapsed
        fi

        if ! kill -0 $GENERATOR_PID 2>/dev/null; then
            echo -e "${YELLOW}[WARNING]${NC} Generator process died unexpectedly"
            break
        fi
        sleep 1
    done
    echo -e "${GREEN}[COMPLETE]${NC} SGX server finished (${SUMS} summations reached)"
}

cleanup() {
    echo -e "\n${YELLOW}[CLEANUP]${NC} Stopping SGX benchmark..."
    if [ -n "${GENERATOR_PID:-}" ] && kill -0 $GENERATOR_PID 2>/dev/null; then
        kill -TERM $GENERATOR_PID 2>/dev/null || true
    fi
    if [ -n "${SERVER_PID:-}" ] && kill -0 $SERVER_PID 2>/dev/null; then
        kill -TERM $SERVER_PID 2>/dev/null || true
    fi
    sleep 2
    if [ -n "${GENERATOR_PID:-}" ] && kill -0 $GENERATOR_PID 2>/dev/null; then
        kill -KILL $GENERATOR_PID 2>/dev/null || true
    fi
    if [ -n "${SERVER_PID:-}" ] && kill -0 $SERVER_PID 2>/dev/null; then
        kill -KILL $SERVER_PID 2>/dev/null || true
    fi
    echo -e "${GREEN}[COMPLETE]${NC} SGX cleanup finished"
}

show_results() {
    echo -e "\n${CYAN}${BOLD}=== SGX BENCHMARK RESULTS ===${NC}"
    local metrics_file="metrics/benchmark_results.csv"

    if [ -f "$metrics_file" ] && [ -s "$metrics_file" ]; then
        echo -e "${GREEN}[RESULTS]${NC} Metrics from: $metrics_file"
        echo
        python3 -c "
import csv, sys
try:
    with open('$metrics_file', 'r') as f:
        last_line = list(csv.DictReader(f))[-1]
    print(f\"Total Summations: {int(last_line['total_sums']):,}\")
    print(f\"Total Messages:   {int(last_line['total_readings']):,}\")
    print(f\"Duration:         {float(last_line['seconds']):.2f} seconds\")
    print(f\"Throughput:       {float(last_line['throughput_rps']):.2f} messages/second\")
    print(f\"Device Count:     {last_line['device_count']}\")
    print(f\"Thread Count:     {last_line['thread_count'] if last_line['thread_count'] != '0' else 'auto'}\")
    print(f\"Environment:      Intel SGX Enclave\")
except Exception as e:
    sys.stderr.write(f'Could not parse metrics from $metrics_file: {e}\\n')
    sys.stderr.write('\\n--- Raw CSV Content ---\\n')
    import os
    os.system('tail -n 5 $metrics_file')
"
    else
        echo -e "${YELLOW}[WARNING]${NC} No metrics file found at '$metrics_file'"
    fi
    echo
}

show_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo
    echo "Environment Variables:"
    echo "  RATE=N              Messages per second (default: 1000)"
    echo "  SUMS=N              Number of power summations to complete (default: 100)"
    echo "  SERVER_HOST=HOST    Server hostname (default: localhost)"
    echo "  SERVER_PORT=PORT    Server port (default: 8890)"
    echo "  EXPECTED_DEVICES=N  Expected device count for server sizing"
    echo
    echo "Examples:"
    echo "  $0                           # 1000 msg/s until 100 summations in SGX"
    echo "  RATE=2000 SUMS=50 $0         # 2000 msg/s until 50 summations in SGX"
    echo
    echo "Options:"
    echo "  -h, --help          Show this help"
}

main() {
    if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
        show_usage
        exit 0
    fi

    mkdir -p metrics
    print_header
    print_config
    trap cleanup SIGINT SIGTERM EXIT

    if ! check_dependencies; then exit 1; fi
    if ! start_sgx_server; then exit 1; fi
    if ! start_generator; then cleanup; exit 1; fi

    monitor_benchmark
    # cleanup is called by trap
    show_results
}

main "$@"

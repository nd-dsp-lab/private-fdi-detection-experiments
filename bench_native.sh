#!/bin/bash
# bench_native.sh - Updated for sum-based benchmarking

set -euo pipefail

# Configuration
RATE=${RATE:-1000}
SERVER_HOST=${SERVER_HOST:-localhost}
SERVER_PORT=${SERVER_PORT:-8890}
SUMS=${SUMS:-100}  # Number of power summations instead of duration
EXPECTED_DEVICES=${EXPECTED_DEVICES:-$((RATE / 10))}

SERVER_EXEC="./smart_grid_server"

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
    echo -e "  SMART GRID BENCHMARK (NATIVE)"
    echo -e "==================================${NC}\n"
}

print_config() {
    echo -e "${BLUE}Configuration:${NC}"
    echo -e "  Rate: ${BOLD}${RATE}${NC} messages/second"
    echo -e "  Target: ${BOLD}${SUMS}${NC} power summations"
    echo -e "  Expected readings per sum: ${BOLD}${EXPECTED_DEVICES}${NC}"
    echo -e "  Estimated total readings: ${BOLD}$((SUMS * EXPECTED_DEVICES))${NC}"
    echo -e "  Server: ${BOLD}${SERVER_HOST}:${SERVER_PORT}${NC}"
    echo
}

# --- START: ADDED/FIXED SECTION ---

check_dependencies() {
    echo -e "${BLUE}[CHECK]${NC} Verifying dependencies..."

    # Build native server if needed
    if [ ! -f "$SERVER_EXEC" ]; then
        echo -e "${YELLOW}[BUILD]${NC} Building native server..."
        if ! ./build_native.sh; then
            echo -e "${RED}[ERROR]${NC} Native server build failed"
            return 1
        fi
    fi

    # Check Python dependencies
    if ! python3 -c "import cryptography" 2>/dev/null; then
        echo -e "${RED}[ERROR]${NC} Missing Python dependencies"
        echo -e "${YELLOW}[INFO]${NC} Install with: pip install cryptography"
        return 1
    fi

    # Check if port is available
    if netstat -tlnp 2>/dev/null | grep -q ":$SERVER_PORT "; then
        echo -e "${RED}[ERROR]${NC} Port $SERVER_PORT is already in use"
        return 1
    fi

    echo -e "${GREEN}[OK]${NC} All dependencies satisfied"
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

# --- END: ADDED/FIXED SECTION ---

start_server() {
    echo -e "${BLUE}[SERVER]${NC} Starting native server..."

    local sum_interval=$EXPECTED_DEVICES

    $SERVER_EXEC \
        --port $SERVER_PORT \
        --devices $EXPECTED_DEVICES \
        --sum-interval $sum_interval \
        --benchmark-sums $SUMS \
        --metrics "metrics/benchmark_results.csv" \
        --quiet &

    SERVER_PID=$!
    sleep 3

    if ! kill -0 $SERVER_PID 2>/dev/null; then
        echo -e "${RED}[ERROR]${NC} Server failed to start"
        return 1
    fi

    local retries=10
    while [ $retries -gt 0 ]; do
        if timeout 1 bash -c "</dev/tcp/$SERVER_HOST/$SERVER_PORT" 2>/dev/null; then
            break
        fi
        echo -e "${YELLOW}[WAIT]${NC} Waiting for server to accept connections on port $SERVER_PORT..."
        sleep 1
        retries=$((retries - 1))
    done

    if [ $retries -eq 0 ]; then
        echo -e "${RED}[ERROR]${NC} Server not accepting connections on port $SERVER_PORT after timeout"
        kill $SERVER_PID 2>/dev/null
        return 1
    fi

    echo -e "${GREEN}[OK]${NC} Server started and accepting connections (PID: $SERVER_PID)"
    return 0
}

monitor_benchmark() {
    echo -e "${CYAN}[RUNNING]${NC} Benchmark active - waiting for ${SUMS} power summations..."
    echo -e "${YELLOW}[INFO]${NC} Press Ctrl+C to stop early"
    echo

    local start_time=$(date +%s)
    local last_check=0

    while true; do
        local current_time=$(date +%s)
        local elapsed=$((current_time - start_time))

        if [ $((elapsed % 10)) -eq 0 ] && [ $elapsed -ne $last_check ]; then
            local expected_messages=$((RATE * elapsed))
            echo -e "${CYAN}[PROGRESS]${NC} ${elapsed}s elapsed (expected: ${expected_messages} messages sent)"
            last_check=$elapsed
        fi

        if ! kill -0 $SERVER_PID 2>/dev/null; then
            echo -e "${GREEN}[COMPLETE]${NC} Server finished (${SUMS} summations reached)"
            break
        fi

        if ! kill -0 $GENERATOR_PID 2>/dev/null; then
            echo -e "${YELLOW}[WARNING]${NC} Generator process died"
            break
        fi

        sleep 1
    done
}

# --- START: ADDED/FIXED SECTION ---

cleanup() {
    echo -e "\n${YELLOW}[CLEANUP]${NC} Stopping benchmark..."

    if [ -n "${GENERATOR_PID:-}" ] && kill -0 $GENERATOR_PID 2>/dev/null; then
        echo -e "${BLUE}[CLEANUP]${NC} Stopping generator..."
        kill -TERM $GENERATOR_PID 2>/dev/null
        sleep 1
        kill -KILL $GENERATOR_PID 2>/dev/null
    fi

    if [ -n "${SERVER_PID:-}" ] && kill -0 $SERVER_PID 2>/dev/null; then
        echo -e "${BLUE}[CLEANUP]${NC} Stopping server..."
        kill -TERM $SERVER_PID 2>/dev/null
        sleep 2
        kill -KILL $SERVER_PID 2>/dev/null
    fi

    echo -e "${GREEN}[COMPLETE]${NC} Cleanup finished"
}

# --- END: ADDED/FIXED SECTION ---

show_results() {
    echo -e "\n${CYAN}${BOLD}=== BENCHMARK RESULTS ===${NC}"
    local metrics_file="metrics/benchmark_results.csv"

    if [ -f "$metrics_file" ] && [ -s "$metrics_file" ]; then
        echo -e "${GREEN}[RESULTS]${NC} Metrics from: $metrics_file"
        echo

        python3 -c "
import csv
try:
    with open('$metrics_file', 'r') as f:
        last_line = list(csv.DictReader(f))[-1]

    print(f\"Total Summations: {int(last_line['total_sums']):,}\")
    print(f\"Total Messages: {int(last_line['total_readings']):,}\")
    print(f\"Duration: {float(last_line['seconds']):.2f} seconds\")
    print(f\"Throughput: {float(last_line['throughput_rps']):.2f} messages/second\")
    print(f\"Device Count: {last_line['device_count']}\")
    print(f\"Thread Count: {last_line['thread_count'] if last_line['thread_count'] != '0' else 'auto'}\")
except Exception as e:
    print(f'Could not parse metrics from $metrics_file: {e}')
    print('\n--- Raw CSV Content ---')
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
    echo "  $0                           # 1000 msg/s until 100 summations"
    echo "  RATE=2000 SUMS=50 $0         # 2000 msg/s until 50 summations"
    echo "  SUMS=200 $0                  # 1000 msg/s until 200 summations"
    echo
    echo "Options:"
    echo "  -h, --help          Show this help"
}

# --- START: ADDED/FIXED SECTION ---

main() {
    while [[ $# -gt 0 ]]; do
        case $1 in
            -h|--help)
                show_usage
                exit 0
                ;;
            *)
                echo "Unknown option: $1"
                show_usage
                exit 1
                ;;
        esac
    done

    mkdir -p metrics

    print_header
    print_config

    trap cleanup SIGINT SIGTERM EXIT

    if ! check_dependencies; then
        exit 1
    fi

    if ! start_server; then
        exit 1
    fi

    if ! start_generator; then
        cleanup
        exit 1
    fi

    monitor_benchmark
    # Cleanup is handled by the trap
}

# This line executes the main function, starting the script
main "$@"
# --- END: ADDED/FIXED SECTION ---

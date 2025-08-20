#!/usr/bin/env bash
set -euo pipefail

# Smart Grid Benchmark Launcher
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

# Globals
PIDS=()
SERVER_PID=""
CLEANUP_DONE=false
TEMP_PID_FILE="/tmp/smart_grid_pids_$$"
SUMMARY_CSV=""
JQ_AVAILABLE=false
MODE="native"

# Defaults
SERVER_BIN="./smart_grid_server"
NUM_DEVICES=100
INTERVAL=1
PORT=8890
SUM_INTERVAL=0
BENCH_TARGET=10000
THREADS=0
QUIET=false
SWEEP=""              # "START:END:STEP"
TRIALS=1
NO_BUILD=false
METRICS_DIR="./metrics"
OUT_CSV="./benchmark_results.csv"

print_banner() {
  echo -e "\n${CYAN}${BOLD}=============================================="
  echo -e "        SMART GRID BENCHMARK LAUNCHER"
  echo -e "==============================================${NC}\n"
}

print_usage() {
  echo "Usage: $0 [OPTIONS]"
  echo ""
  echo "Core:"
  echo "  --server-bin PATH     Path to server binary (default: ./smart_grid_server)"
  echo "  -d, --devices NUM     Number of devices (default: 100)"
  echo "  -i, --interval SEC    Client reporting interval (default: 1)"
  echo "  -p, --port PORT       Server port (default: 8890)"
  echo "  -s, --sum NUM         Sum every N readings (default: devices/10, min 10)"
  echo "  -b, --bench NUM       Benchmark target total readings (server exits at N) (default: 10000)"
  echo "  -t, --threads NUM     Server thread count (0=auto) (default: 0)"
  echo "  -q, --quiet           Suppress periodic server logs"
  echo "      --no-build        Do not build even if server binary missing"
  echo ""
  echo "Benchmark sweep:"
  echo "      --sweep A:B:C     Sweep device counts from A to B inclusive step C"
  echo "      --trials N        Trials per device count (default: 1)"
  echo ""
  echo "Output:"
  echo "      --metrics-dir DIR Directory for metrics JSON (default: ./metrics)"
  echo "      --out-csv FILE    CSV summary output (default: ./benchmark_results.csv)"
  echo ""
  echo "Examples:"
  echo "  $0 --devices 2000 --bench 200000 --quiet"
  echo "  $0 --sweep 100:2000:300 --bench 200000 --trials 2 --quiet"
}

cleanup() {
  if [ "$CLEANUP_DONE" = true ]; then
    return
  fi
  CLEANUP_DONE=true

  echo -e "\n${YELLOW}[SHUTDOWN]${NC} Stopping all processes..."

  if [ -f "$TEMP_PID_FILE" ]; then
    while read -r pid; do
      if [ -n "${pid:-}" ] && kill -0 "$pid" 2>/dev/null; then
        kill "$pid" 2>/dev/null || true
      fi
    done < "$TEMP_PID_FILE"
    rm -f "$TEMP_PID_FILE"
  fi

  for pid in "${PIDS[@]:-}"; do
    if [ -n "${pid:-}" ] && kill -0 "$pid" 2>/dev/null; then
      kill "$pid" 2>/dev/null || true
    fi
  done

  if [ -n "${SERVER_PID:-}" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill "$SERVER_PID" 2>/dev/null || true
  fi

  # Best-effort cleanup for any stragglers
  pkill -f "client/main.py --device meter_" 2>/dev/null || true
  pkill -f "client.main --device meter_" 2>/dev/null || true

  sleep 2

  if [ -f "$TEMP_PID_FILE" ]; then
    while read -r pid; do
      if [ -n "${pid:-}" ] && kill -0 "$pid" 2>/dev/null; then
        kill -9 "$pid" 2>/dev/null || true
      fi
    done < "$TEMP_PID_FILE"
    rm -f "$TEMP_PID_FILE"
  fi

  for pid in "${PIDS[@]:-}"; do
    if [ -n "${pid:-}" ] && kill -0 "$pid" 2>/dev/null; then
      kill -9 "$pid" 2>/dev/null || true
    fi
  done

  if [ -n "${SERVER_PID:-}" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
    kill -9 "$SERVER_PID" 2>/dev/null || true
  fi

  echo -e "${GREEN}[COMPLETE]${NC} Shutdown complete"
}

trap cleanup SIGINT SIGTERM EXIT

check_deps() {
  echo -e "${BLUE}[INIT]${NC} Checking dependencies..."

  if ! command -v python3 >/dev/null 2>&1; then
    echo -e "${RED}[ERROR]${NC} python3 not found"
    return 1
  fi

  if ! python3 -c "import cryptography, numpy" 2>/dev/null; then
    echo -e "${RED}[ERROR]${NC} Missing Python deps"
    echo -e "${YELLOW}[INFO]${NC} Install with: pip install cryptography numpy"
    return 1
  fi

  if command -v jq >/dev/null 2>&1; then
    JQ_AVAILABLE=true
  fi

  if [ ! -x "$SERVER_BIN" ]; then
    if $NO_BUILD; then
      echo -e "${RED}[ERROR]${NC} Server binary not found and --no-build set: $SERVER_BIN"
      return 1
    fi
    echo -e "${YELLOW}[BUILD]${NC} Building C++ server..."
    if ! cmake . && make -j; then
      echo -e "${RED}[ERROR]${NC} Failed to build C++ server"
      return 1
    fi
  fi

  mkdir -p "$METRICS_DIR"

  echo -e "${GREEN}[OK]${NC} All dependencies satisfied"
  return 0
}

start_server() {
  local devices=$1
  local port=$2
  local sum_interval=$3
  local bench_target=$4
  local metrics_file=$5
  local threads=$6
  local quiet_flag=$7

  echo -e "${BLUE}[SERVER]${NC} Starting server (devices=$devices, port=$port, sum=$sum_interval, target=$bench_target)..."

  local args=( "$SERVER_BIN"
               --port "$port"
               --devices "$devices"
               --sum-interval "$sum_interval"
               --benchmark-readings "$bench_target"
               --metrics "$metrics_file" )

  if [ "$threads" -gt 0 ]; then
    args+=( --threads "$threads" )
  fi
  if [ "$quiet_flag" = true ]; then
    args+=( --quiet )
  fi

  "${args[@]}" &
  SERVER_PID=$!
  PIDS+=("$SERVER_PID")
  echo "$SERVER_PID" >> "$TEMP_PID_FILE"

  # Give server time to bind
  sleep 1.5

  if kill -0 "$SERVER_PID" 2>/dev/null; then
    echo -e "${GREEN}[OK]${NC} Server started (PID: $SERVER_PID)"
    return 0
  else
    echo -e "${RED}[ERROR]${NC} Failed to start server"
    return 1
  fi
}

start_devices() {
  local num_devices=$1
  local interval=$2
  local port=$3

  echo -e "${BLUE}[DEVICES]${NC} Starting $num_devices edge devices..."

  local batch_size=50
  local batches=$(( num_devices / batch_size ))
  local remainder=$(( num_devices % batch_size ))

  for ((batch=0; batch<=batches; batch++)); do
    local start_idx=$((batch * batch_size))
    local end_idx=$((start_idx + batch_size))
    if [ $batch -eq $batches ]; then
      end_idx=$((start_idx + remainder))
    fi
    if [ $start_idx -ge $num_devices ]; then
      break
    fi

    for ((i=start_idx; i<end_idx; i++)); do
      device_id=$(printf "meter_%06d" "$i")
      python3 -m client.main --device "$device_id" --host localhost --port "$port" --interval "$interval" \
        >/dev/null 2>&1 &
      local pid=$!
      PIDS+=("$pid")
      echo "$pid" >> "$TEMP_PID_FILE"
      if [ $((i % 50)) -eq 0 ]; then
        sleep 0.01
      fi
    done

    if [ $batch -lt $batches ] || [ $remainder -gt 0 ]; then
      echo -e "${CYAN}[PROGRESS]${NC} Started batch $((batch + 1)): devices $start_idx-$((end_idx - 1))"
      sleep 0.2
    fi
  done

  echo -e "${GREEN}[OK]${NC} Started $num_devices devices"
}

wait_for_completion() {
  local metrics_file=$1

  echo -e "${CYAN}[RUNNING]${NC} Waiting for server (PID $SERVER_PID) to reach target and exit..."
  wait "$SERVER_PID" || true

  echo -e "${BLUE}[CLEANUP]${NC} Stopping clients..."
  pkill -f "client/main.py --device meter_" 2>/dev/null || true
  pkill -f "client.main --device meter_" 2>/dev/null || true

  # Let filesystem flush metrics
  local deadline=$(( $(date +%s) + 3 ))
  while [ ! -f "$metrics_file" ] && [ "$(date +%s)" -lt "$deadline" ]; do
    sleep 0.05
  done

  if [ -f "$metrics_file" ]; then
    if $JQ_AVAILABLE; then
      local line
      line=$(jq -r '"devices=\(.device_count) threads=\(.thread_count) target=\(.benchmark_target) readings=\(.total_readings) time=\(.seconds) s throughput=\(.throughput_rps) rps"' "$metrics_file")
      echo -e "${GREEN}[RESULT]${NC} $line"
      # Append to CSV if configured
      if [ -n "$SUMMARY_CSV" ]; then
        if [ ! -f "$SUMMARY_CSV" ]; then
          echo "mode,devices,threads,target,total_readings,seconds,throughput_rps,metrics_file" > "$SUMMARY_CSV"
        fi
        jq -r '[.device_count, .thread_count, .benchmark_target, .total_readings, .seconds, .throughput_rps] | @csv' "$metrics_file" \
          | awk -v m="$MODE" -v f="$metrics_file" '{print m","$0","f}' >> "$SUMMARY_CSV"
      fi
    else
      echo -e "${YELLOW}[WARN]${NC} jq not found; raw metrics:"
      cat "$metrics_file"
    fi
  else
    echo -e "${RED}[ERROR]${NC} Metrics file not found: $metrics_file"
  fi
}

run_single() {
  local devices=$1
  local trial=$2

  local sum_interval_local=$SUM_INTERVAL
  if [ "$sum_interval_local" -eq 0 ]; then
    sum_interval_local=$(( devices ))
    if [ "$sum_interval_local" -lt 10 ]; then
      sum_interval_local=10
    fi
  fi

  local metrics_file="${METRICS_DIR}/metrics_devices_${devices}_trial_${trial}.json"
  rm -f "$metrics_file"

  echo -e "${CYAN}[CONFIG]${NC} Devices: $devices, Interval: ${INTERVAL}s, Port: $PORT, Sum every: $sum_interval_local, Target: $BENCH_TARGET, Threads: $THREADS"

  start_server "$devices" "$PORT" "$sum_interval_local" "$BENCH_TARGET" "$metrics_file" "$THREADS" "$QUIET"
  start_devices "$devices" "$INTERVAL" "$PORT"
  wait_for_completion "$metrics_file"

  # brief pause to avoid port reuse issues
  sleep 1
}

main() {
  touch "$TEMP_PID_FILE"

  # Parse args
  while [[ $# -gt 0 ]]; do
    case "$1" in
      --server-bin) SERVER_BIN="$2"; shift 2 ;;
      -d|--devices) NUM_DEVICES="$2"; shift 2 ;;
      -i|--interval) INTERVAL="$2"; shift 2 ;;
      -p|--port) PORT="$2"; shift 2 ;;
      -s|--sum) SUM_INTERVAL="$2"; shift 2 ;;
      -b|--bench) BENCH_TARGET="$2"; shift 2 ;;
      -t|--threads) THREADS="$2"; shift 2 ;;
      -q|--quiet) QUIET=true; shift ;;
      --no-build) NO_BUILD=true; shift ;;
      --sweep) SWEEP="$2"; shift 2 ;;
      --trials) TRIALS="$2"; shift 2 ;;
      --metrics-dir) METRICS_DIR="$2"; shift 2 ;;
      --out-csv) OUT_CSV="$2"; shift 2 ;;
      -h|--help) print_usage; exit 0 ;;
      *) echo "Unknown option: $1"; print_usage; exit 1 ;;
    esac
  done

  SUMMARY_CSV="$OUT_CSV"

  print_banner
  if ! check_deps; then
    exit 1
  fi

  echo -e "${CYAN}[OUTPUT]${NC} Metrics directory: $METRICS_DIR"
  echo -e "${CYAN}[OUTPUT]${NC} Summary CSV: $SUMMARY_CSV"

  if [ -n "$SWEEP" ]; then
    IFS=':' read -r START END STEP <<< "$SWEEP"
    if [ -z "${START:-}" ] || [ -z "${END:-}" ] || [ -z "${STEP:-}" ]; then
      echo -e "${RED}[ERROR]${NC} Invalid --sweep format. Use START:END:STEP"
      exit 1
    fi
    for (( d=START; d<=END; d+=STEP )); do
      for (( trial=1; trial<=TRIALS; trial++ )); do
        run_single "$d" "$trial"
      done
    done
  else
    for (( trial=1; trial<=TRIALS; trial++ )); do
      run_single "$NUM_DEVICES" "$trial"
    done
  fi

  echo -e "${GREEN}[DONE]${NC} Benchmark runs complete."
}

main "$@"

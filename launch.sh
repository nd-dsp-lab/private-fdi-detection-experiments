#!/bin/bash

# Smart Grid Simulation Launcher v2.1
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

PIDS=()
SERVER_PID=""
CLEANUP_DONE=false
TEMP_PID_FILE="/tmp/smart_grid_pids_$$"

print_banner() {
    echo -e "\n${CYAN}${BOLD}=============================================="
    echo -e "    SMART GRID SIMULATION LAUNCHER v2.1"
    echo -e "==============================================${NC}\n"
}

cleanup() {
    if [ "$CLEANUP_DONE" = true ]; then
        return
    fi
    CLEANUP_DONE=true

    echo -e "\n${YELLOW}[SHUTDOWN]${NC} Stopping all processes..."

    if [ -f "$TEMP_PID_FILE" ]; then
        while read -r pid; do
            if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
                kill "$pid" 2>/dev/null
            fi
        done < "$TEMP_PID_FILE"
        rm -f "$TEMP_PID_FILE"
    fi

    for pid in "${PIDS[@]}"; do
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            kill "$pid" 2>/dev/null
        fi
    done

    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID" 2>/dev/null
    fi

    pkill -f "client/main.py --device meter_" 2>/dev/null

    sleep 3

    if [ -f "$TEMP_PID_FILE" ]; then
        while read -r pid; do
            if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
                kill -9 "$pid" 2>/dev/null
            fi
        done < "$TEMP_PID_FILE"
        rm -f "$TEMP_PID_FILE"
    fi

    for pid in "${PIDS[@]}"; do
        if [ -n "$pid" ] && kill -0 "$pid" 2>/dev/null; then
            kill -9 "$pid" 2>/dev/null
        fi
    done

    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill -9 "$SERVER_PID" 2>/dev/null
    fi

    pkill -f "client.main --device meter_" 2>/dev/null
    pkill -9 -f "client.main --device meter_" 2>/dev/null

    echo -e "${GREEN}[COMPLETE]${NC} Shutdown complete"
    exit 0
}

trap cleanup SIGINT SIGTERM EXIT
trap cleanup SIGQUIT SIGKILL

check_deps() {
    echo -e "${BLUE}[INIT]${NC} Checking dependencies..."

    if [ ! -f "./smart_grid_server" ]; then
        echo -e "${YELLOW}[BUILD]${NC} Building C++ server..."
        if ! cmake . && make; then
            echo -e "${RED}[ERROR]${NC} Failed to build C++ server"
            return 1
        fi
    fi

    if ! python3 -c "import cryptography, numpy" 2>/dev/null; then
        echo -e "${RED}[ERROR]${NC} Missing Python dependencies"
        echo -e "${YELLOW}[INFO]${NC} Install with: pip install cryptography numpy"
        return 1
    fi

    echo -e "${GREEN}[OK]${NC} All dependencies satisfied"
    return 0
}

start_server() {
    local devices=$1
    local port=$2
    local sum_interval=$3

    echo -e "${BLUE}[SERVER]${NC} Starting C++ server..."

    ./smart_grid_server --port "$port" --devices "$devices" --sum-interval "$sum_interval" &
    SERVER_PID=$!
    PIDS+=($SERVER_PID)
    echo "$SERVER_PID" >> "$TEMP_PID_FILE"

    sleep 3

    if kill -0 "$SERVER_PID" 2>/dev/null; then
        echo -e "${GREEN}[OK]${NC} C++ server started (PID: $SERVER_PID)"
        return 0
    else
        echo -e "${RED}[ERROR]${NC} Failed to start C++ server"
        return 1
    fi
}

start_devices() {
    local num_devices=$1
    local interval=$2
    local port=$3

    echo -e "${BLUE}[DEVICES]${NC} Starting $num_devices edge devices..."

    local batch_size=50
    local batches=$((num_devices / batch_size))
    local remainder=$((num_devices % batch_size))

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
            device_id=$(printf "meter_%06d" $i)
            python3 -m client.main --device "$device_id" --host localhost --port "$port" --interval "$interval" &
            local pid=$!
            PIDS+=($pid)
            echo "$pid" >> "$TEMP_PID_FILE"

            if [ $((i % 25)) -eq 0 ]; then
                sleep 0.05
            fi
        done

        if [ $batch -lt $batches ] || [ $remainder -gt 0 ]; then
            echo -e "${CYAN}[PROGRESS]${NC} Started batch $((batch + 1)): devices $start_idx-$((end_idx - 1))"
            sleep 0.5
        fi
    done

    echo -e "${GREEN}[OK]${NC} Started $num_devices devices"
}

monitor() {
    echo -e "${CYAN}[RUNNING]${NC} Simulation active - Press Ctrl+C to stop"

    while true; do
        sleep 30

        local active=0
        for pid in "${PIDS[@]}"; do
            if kill -0 "$pid" 2>/dev/null; then
                ((active++))
            fi
        done

        local timestamp=$(date '+%H:%M:%S')
        echo -e "${CYAN}[$timestamp] [SUMMARY]${NC} Active processes: $active"

        if [ -n "$SERVER_PID" ] && ! kill -0 "$SERVER_PID" 2>/dev/null; then
            echo -e "${RED}[ERROR]${NC} Server process died"
            break
        fi
    done
}

print_usage() {
    echo "Usage: $0 <num_devices> <interval_seconds> [port]"
    echo "       $0 [OPTIONS]"
    echo ""
    echo "Positional arguments:"
    echo "  num_devices      Number of devices to simulate"
    echo "  interval_seconds Reporting interval in seconds"
    echo "  port            Server port (optional, default: 8890)"
    echo ""
    echo "Options:"
    echo "  -d, --devices NUM    Number of devices (default: 100)"
    echo "  -i, --interval SEC   Reporting interval (default: 1)"
    echo "  -p, --port PORT      Server port (default: 8890)"
    echo "  -s, --sum NUM        Sum every N readings (default: devices/10)"
    echo "  -h, --help          Show this help"
    echo ""
    echo "Examples:"
    echo "  $0 100000 1          # 100k devices, 1 second interval"
    echo "  $0 --devices 50000 --interval 2 --port 9000"
}

main() {
    local num_devices=100
    local interval=1
    local port=8890
    local sum_interval=0

    touch "$TEMP_PID_FILE"

    if [[ $# -ge 2 && "$1" =~ ^[0-9]+$ && "$2" =~ ^[0-9]+$ ]]; then
        num_devices=$1
        interval=$2
        if [[ $# -ge 3 && "$3" =~ ^[0-9]+$ ]]; then
            port=$3
        fi
    else
        while [[ $# -gt 0 ]]; do
            case $1 in
                -d|--devices)
                    num_devices="$2"
                    shift 2
                    ;;
                -i|--interval)
                    interval="$2"
                    shift 2
                    ;;
                -p|--port)
                    port="$2"
                    shift 2
                    ;;
                -s|--sum)
                    sum_interval="$2"
                    shift 2
                    ;;
                -h|--help)
                    print_usage
                    exit 0
                    ;;
                *)
                    echo "Unknown option: $1"
                    print_usage
                    exit 1
                    ;;
            esac
        done
    fi

    if [ $sum_interval -eq 0 ]; then
        sum_interval=$((num_devices / 10))
        if [ $sum_interval -lt 10 ]; then
            sum_interval=10
        fi
    fi

    print_banner

    echo -e "${CYAN}[CONFIG]${NC} Devices: $num_devices, Interval: ${interval}s, Port: $port, Sum every: $sum_interval"

    if ! check_deps; then
        exit 1
    fi

    if ! start_server "$num_devices" "$port" "$sum_interval"; then
        exit 1
    fi

    start_devices "$num_devices" "$interval" "$port"
    monitor
}

main "$@"

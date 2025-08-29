#!/bin/bash
# bench_sgx.sh (fixed arguments)

# Configuration
NUM_DEVICES=${NUM_DEVICES:-1000}
SERVER_HOST=${SERVER_HOST:-localhost}
SERVER_PORT=${SERVER_PORT:-8890}
INTERVAL=${INTERVAL:-3}
BATCH_SIZE=${BATCH_SIZE:-200}
CHUNK_SIZE=${CHUNK_SIZE:-50}

# SGX server executable path
SERVER_EXEC="./smart_grid_server_sgx"

# Safety warnings
if [ "$NUM_DEVICES" -gt 5000 ]; then
    echo "WARNING: Large device count ($NUM_DEVICES). Monitor system resources!"
    echo "Consider reducing NUM_DEVICES or BATCH_SIZE for shared systems."
    read -p "Continue? (y/N): " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

if [ "$BATCH_SIZE" -gt 500 ] && [ "$NUM_DEVICES" -gt 2000 ]; then
    echo "WARNING: High batch size ($BATCH_SIZE) with many devices ($NUM_DEVICES)"
    echo "This may impact shared systems. Consider BATCH_SIZE=200 or lower."
fi

echo "=== Smart Grid Benchmark (SGX) ==="
echo "Devices: $NUM_DEVICES"
echo "Batch Size: $BATCH_SIZE"
echo "Chunk Size: $CHUNK_SIZE"
echo "Interval: ${INTERVAL}s"
echo "Server: $SERVER_HOST:$SERVER_PORT"
echo

# Build SGX version if needed
if [ ! -f "$SERVER_EXEC" ]; then
    echo "Building SGX server..."
    if [ -f "./build_sgx.sh" ]; then
        ./build_sgx.sh
    else
        echo "ERROR: build_sgx.sh not found. Please build SGX version manually."
        exit 1
    fi
    BUILD_RESULT=$?

    if [ $BUILD_RESULT -ne 0 ]; then
        echo "ERROR: SGX server build failed"
        exit 1
    fi
fi

# Verify server executable exists
if [ ! -f "$SERVER_EXEC" ]; then
    echo "ERROR: SGX server executable not found at $SERVER_EXEC"
    echo "Build may have failed. Check for errors above."
    exit 1
fi

# Start SGX server
echo "Starting SGX server..."
gramine-sgx smart_grid_server --port $SERVER_PORT --devices $NUM_DEVICES --quiet &
SERVER_PID=$!

# Wait for server to start (SGX might need more time)
sleep 5

# Check if server started successfully
if ! kill -0 $SERVER_PID 2>/dev/null; then
    echo "ERROR: SGX server failed to start"
    echo "Check if port $SERVER_PORT is available:"
    echo "  netstat -tlnp | grep $SERVER_PORT"
    exit 1
fi

echo "SGX server started (PID: $SERVER_PID)"

# Start device simulator with correct arguments
echo "Starting device simulator..."
python3 -m client.simulator \
    --devices $NUM_DEVICES \
    --host $SERVER_HOST \
    --port $SERVER_PORT \
    --interval $INTERVAL \
    --batch-size $BATCH_SIZE \
    --chunk-size $CHUNK_SIZE &

SIMULATOR_PID=$!

echo "Device simulator started (PID: $SIMULATOR_PID)"
echo
echo "=== SGX Benchmark Running ==="
echo "Press Ctrl+C to stop..."

# Cleanup function
cleanup() {
    echo
    echo "Stopping SGX benchmark..."

    if [ -n "$SIMULATOR_PID" ] && kill -0 $SIMULATOR_PID 2>/dev/null; then
        echo "Stopping device simulator..."
        kill -TERM $SIMULATOR_PID 2>/dev/null
        sleep 3
        kill -KILL $SIMULATOR_PID 2>/dev/null
    fi

    if [ -n "$SERVER_PID" ] && kill -0 $SERVER_PID 2>/dev/null; then
        echo "Stopping SGX server..."
        kill -TERM $SERVER_PID 2>/dev/null
        sleep 2
        kill -KILL $SERVER_PID 2>/dev/null
    fi

    echo "Cleanup complete"
    exit 0
}

trap cleanup SIGINT SIGTERM
wait $SIMULATOR_PID $SERVER_PID

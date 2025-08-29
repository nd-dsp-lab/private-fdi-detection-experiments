#!/bin/bash
# bench_native.sh (fixed)

# Configuration
NUM_DEVICES=${NUM_DEVICES:-1000}
SERVER_HOST=${SERVER_HOST:-localhost}
SERVER_PORT=${SERVER_PORT:-8890}
INTERVAL=${INTERVAL:-1}
BATCH_SIZE=${BATCH_SIZE:-100}
CHUNK_SIZE=${CHUNK_SIZE:-25}

# Server executable path
SERVER_EXEC="./smart_grid_server"

echo "=== Smart Grid Benchmark (Native) ==="
echo "Devices: $NUM_DEVICES"
echo "Batch Size: $BATCH_SIZE"
echo "Chunk Size: $CHUNK_SIZE"
echo "Interval: ${INTERVAL}s"
echo "Server: $SERVER_HOST:$SERVER_PORT"
echo

# Build if needed
if [ ! -f "$SERVER_EXEC" ]; then
    echo "Building server..."
    ./build_native.sh
    if [ $? -ne 0 ]; then
        echo "ERROR: Server build failed"
        exit 1
    fi
fi

# Start server with correct arguments (based on server/main.cpp)
echo "Starting server..."
$SERVER_EXEC --port $SERVER_PORT --devices $NUM_DEVICES --quiet &
SERVER_PID=$!

# Wait longer for server to fully initialize
sleep 5

# Verify server is listening
if ! netstat -tlnp 2>/dev/null | grep -q ":$SERVER_PORT "; then
    echo "ERROR: Server not listening on port $SERVER_PORT"
    kill $SERVER_PID 2>/dev/null
    exit 1
fi

echo "Server started (PID: $SERVER_PID) on port $SERVER_PORT"

# Start device simulator with conservative settings
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
echo "Press Ctrl+C to stop..."

# Cleanup function
cleanup() {
    echo
    echo "Stopping benchmark..."

    if [ -n "$SIMULATOR_PID" ] && kill -0 $SIMULATOR_PID 2>/dev/null; then
        kill -TERM $SIMULATOR_PID 2>/dev/null
        sleep 2
        kill -KILL $SIMULATOR_PID 2>/dev/null
    fi

    if [ -n "$SERVER_PID" ] && kill -0 $SERVER_PID 2>/dev/null; then
        kill -TERM $SERVER_PID 2>/dev/null
        sleep 2
        kill -KILL $SERVER_PID 2>/dev/null
    fi

    echo "Cleanup complete"
    exit 0
}

trap cleanup SIGINT SIGTERM
wait $SIMULATOR_PID $SERVER_PID

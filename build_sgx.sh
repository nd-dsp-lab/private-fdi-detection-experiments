#!/bin/bash
set -e

echo "Building SGX Smart Grid Server..."

# Create SGX server directory
mkdir -p server_sgx

# Build the application
cmake .
make smart_grid_server_sgx

# Check if binary exists and is executable
if [ ! -x "./smart_grid_server_sgx" ]; then
    echo "Error: smart_grid_server_sgx binary not found or not executable"
    exit 1
fi

# Generate Gramine manifest
echo "Generating Gramine manifest..."
gramine-manifest \
    -Dlog_level=debug \
    -Dentrypoint=./smart_grid_server_sgx \
    -Darch_libdir=/lib/x86_64-linux-gnu \
    smart_grid_server.manifest.template \
    smart_grid_server.manifest

# Generate SGX signature
echo "Generating SGX signature..."
gramine-sgx-sign \
    --manifest smart_grid_server.manifest \
    --output smart_grid_server.manifest.sgx

echo "SGX build complete!"
echo "Binary location: $(realpath smart_grid_server_sgx)"
echo "Run with: gramine-sgx smart_grid_server"

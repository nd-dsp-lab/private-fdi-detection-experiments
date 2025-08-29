#!/bin/bash
# cleanup.sh - Smart Grid Project Cleanup Script

set -euo pipefail

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m'

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

print_header() {
    echo -e "\n${CYAN}${BOLD}=================================="
    echo -e "    SMART GRID PROJECT CLEANUP"
    echo -e "==================================${NC}\n"
}

cleanup_build_artifacts() {
    echo -e "${BLUE}[CLEANUP]${NC} Removing build artifacts..."

    # CMake generated files
    rm -f CMakeCache.txt cmake_install.cmake Makefile
    rm -rf CMakeFiles/

    # Build directory (if using out-of-source builds)
    if [ -d "build" ]; then
        rm -rf build/
        echo -e "${GREEN}[OK]${NC} Removed build/ directory"
    fi

    # Compiled binaries
    rm -f smart_grid_server smart_grid_server_sgx

    # Object files and libraries
    find . -name "*.o" -delete 2>/dev/null || true
    find . -name "*.a" -delete 2>/dev/null || true
    find . -name "*.so" -delete 2>/dev/null || true

    echo -e "${GREEN}[OK]${NC} Build artifacts cleaned"
}

cleanup_python_cache() {
    echo -e "${BLUE}[CLEANUP]${NC} Removing Python cache files..."

    # Python bytecode
    find . -name "*.pyc" -delete 2>/dev/null || true
    find . -name "*.pyo" -delete 2>/dev/null || true
    find . -name "__pycache__" -type d -exec rm -rf {} + 2>/dev/null || true

    # Python egg info
    find . -name "*.egg-info" -type d -exec rm -rf {} + 2>/dev/null || true

    echo -e "${GREEN}[OK]${NC} Python cache cleaned"
}

cleanup_temp_files() {
    echo -e "${BLUE}[CLEANUP]${NC} Removing temporary files..."

    # Temporary PID files
    rm -f /tmp/smart_grid_pids_*

    # Editor temporary files
    find . -name "*~" -delete 2>/dev/null || true
    find . -name "*.swp" -delete 2>/dev/null || true
    find . -name "*.swo" -delete 2>/dev/null || true
    find . -name ".*.swp" -delete 2>/dev/null || true

    # OS temporary files
    find . -name ".DS_Store" -delete 2>/dev/null || true
    find . -name "Thumbs.db" -delete 2>/dev/null || true

    # Core dumps
    find . -name "core" -delete 2>/dev/null || true
    find . -name "core.*" -delete 2>/dev/null || true

    echo -e "${GREEN}[OK]${NC} Temporary files cleaned"
}

cleanup_logs() {
    echo -e "${BLUE}[CLEANUP]${NC} Removing log files..."

    # Common log patterns
    find . -name "*.log" -delete 2>/dev/null || true
    find . -name "*.out" -delete 2>/dev/null || true
    find . -name "*.err" -delete 2>/dev/null || true

    echo -e "${GREEN}[OK]${NC} Log files cleaned"
}

cleanup_gramine_sgx() {
    echo -e "${BLUE}[CLEANUP]${NC} Removing SGX/Gramine artifacts..."

    # SGX signature files
    rm -f smart_grid_server.manifest smart_grid_server.manifest.sgx smart_grid_server.sig

    # Gramine temporary files
    find . -name "*.token" -delete 2>/dev/null || true
    find . -name "*.sig" -delete 2>/dev/null || true

    echo -e "${GREEN}[OK]${NC} SGX/Gramine artifacts cleaned"
}

cleanup_metrics() {
    local keep_metrics=false

    if [ -d "metrics" ] && [ "$(ls -A metrics 2>/dev/null)" ]; then
        echo -e "${YELLOW}[PROMPT]${NC} Metrics directory contains files:"
        ls -la metrics/
        echo -e "${YELLOW}[PROMPT]${NC} Keep metrics files? (y/N): "
        read -r response
        if [[ "$response" =~ ^[Yy]$ ]]; then
            keep_metrics=true
        fi
    fi

    if [ "$keep_metrics" = false ]; then
        rm -f metrics/*.json 2>/dev/null || true
        echo -e "${GREEN}[OK]${NC} Metrics files cleaned"
    else
        echo -e "${CYAN}[SKIP]${NC} Metrics files preserved"
    fi
}

cleanup_weird_files() {
    echo -e "${BLUE}[CLEANUP]${NC} Removing unusual files..."

    # The weird filename from your directory listing
    rm -f "O\312\205\317\ Y" 2>/dev/null || true

    # Other potential weird files
    find . -name "*\$*" -delete 2>/dev/null || true
    find . -name "*\?*" -delete 2>/dev/null || true

    echo -e "${GREEN}[OK]${NC} Unusual files cleaned"
}

stop_running_processes() {
    echo -e "${BLUE}[CLEANUP]${NC} Stopping any running processes..."

    # Stop smart grid processes
    pkill -f "smart_grid_server" 2>/dev/null || true
    pkill -f "client.simulator" 2>/dev/null || true
    pkill -f "client/simulator.py" 2>/dev/null || true

    # Wait a moment for graceful shutdown
    sleep 2

    # Force kill if still running
    pkill -9 -f "smart_grid_server" 2>/dev/null || true
    pkill -9 -f "client.simulator" 2>/dev/null || true

    echo -e "${GREEN}[OK]${NC} Processes stopped"
}

show_usage() {
    echo "Usage: $0 [OPTIONS]"
    echo ""
    echo "Options:"
    echo "  --all          Clean everything (default)"
    echo "  --build        Clean only build artifacts"
    echo "  --python       Clean only Python cache"
    echo "  --temp         Clean only temporary files"
    echo "  --logs         Clean only log files"
    echo "  --sgx          Clean only SGX/Gramine files"
    echo "  --metrics      Clean only metrics files"
    echo "  --processes    Stop only running processes"
    echo "  --dry-run      Show what would be cleaned"
    echo "  -h, --help     Show this help"
}

dry_run() {
    echo -e "${CYAN}[DRY RUN]${NC} Files that would be cleaned:"
    echo ""

    echo "Build artifacts:"
    ls -la CMakeCache.txt cmake_install.cmake Makefile smart_grid_server smart_grid_server_sgx 2>/dev/null || echo "  (none found)"
    find . -name "*.o" -o -name "*.a" -o -name "*.so" 2>/dev/null | head -10

    echo -e "\nPython cache:"
    find . -name "__pycache__" -o -name "*.pyc" 2>/dev/null | head -10

    echo -e "\nTemporary files:"
    find . -name "*~" -o -name "*.swp" -o -name ".DS_Store" 2>/dev/null | head -10

    echo -e "\nSGX files:"
    ls -la *.manifest *.sig 2>/dev/null || echo "  (none found)"

    echo -e "\nMetrics:"
    ls -la metrics/*.json 2>/dev/null || echo "  (none found)"
}

main() {
    local clean_all=true
    local clean_build=false
    local clean_python=false
    local clean_temp=false
    local clean_logs=false
    local clean_sgx=false
    local clean_metrics=false
    local clean_processes=false
    local dry_run_mode=false

    # Parse arguments
    while [[ $# -gt 0 ]]; do
        case $1 in
            --all)
                clean_all=true
                shift
                ;;
            --build)
                clean_all=false
                clean_build=true
                shift
                ;;
            --python)
                clean_all=false
                clean_python=true
                shift
                ;;
            --temp)
                clean_all=false
                clean_temp=true
                shift
                ;;
            --logs)
                clean_all=false
                clean_logs=true
                shift
                ;;
            --sgx)
                clean_all=false
                clean_sgx=true
                shift
                ;;
            --metrics)
                clean_all=false
                clean_metrics=true
                shift
                ;;
            --processes)
                clean_all=false
                clean_processes=true
                shift
                ;;
            --dry-run)
                dry_run_mode=true
                shift
                ;;
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

    print_header

    if [ "$dry_run_mode" = true ]; then
        dry_run
        exit 0
    fi

    # Execute cleanup based on flags
    if [ "$clean_all" = true ]; then
        stop_running_processes
        cleanup_build_artifacts
        cleanup_python_cache
        cleanup_temp_files
        cleanup_logs
        cleanup_gramine_sgx
        cleanup_metrics
        cleanup_weird_files
    else
        [ "$clean_processes" = true ] && stop_running_processes
        [ "$clean_build" = true ] && cleanup_build_artifacts
        [ "$clean_python" = true ] && cleanup_python_cache
        [ "$clean_temp" = true ] && cleanup_temp_files
        [ "$clean_logs" = true ] && cleanup_logs
        [ "$clean_sgx" = true ] && cleanup_gramine_sgx
        [ "$clean_metrics" = true ] && cleanup_metrics
    fi

    echo -e "\n${GREEN}${BOLD}[COMPLETE]${NC} Cleanup finished!"
    echo -e "${CYAN}[INFO]${NC} Project is now clean and ready for fresh builds"
}

main "$@"

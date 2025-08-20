#include "../server/server.hpp"
#include "../server/logger.hpp"
#include <iostream>

// SGX-specific initialization
void sgx_init() {
    Logger::info("Initializing SGX enclave environment");
    // Any SGX-specific setup can go here
}

void print_usage(const char* program_name) {
    std::cout << "SGX Smart Grid Server\n"
              << "Usage: " << program_name << " [OPTIONS]\n"
              << "Options:\n"
              << "  -p, --port PORT             Server port (default: 8890)\n"
              << "  -d, --devices NUM           Expected number of devices (default: 100)\n"
              << "  -s, --sum-interval NUM      Sum every N readings (default: devices/10)\n"
              << "      --benchmark-readings N  Stop after N readings and write metrics\n"
              << "      --metrics FILE          Write JSON metrics to FILE\n"
              << "      --threads N             Use N worker threads (default: auto)\n"
              << "      --quiet                 Suppress periodic logs\n"
              << "  -h, --help                  Show this help\n";
}

int main(int argc, char* argv[]) {
    // Initialize SGX environment
    sgx_init();

    int port = 8890;
    size_t expected_devices = 100;
    size_t sum_interval = 0;
    size_t benchmark_readings = 0;
    std::string metrics_file;
    size_t threads = 0;
    bool quiet = false;

    // Parse arguments (same as original)
    for (int i = 1; i < argc; i++) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if ((arg == "-d" || arg == "--devices") && i + 1 < argc) {
            expected_devices = std::stoull(argv[++i]);
        } else if ((arg == "-s" || arg == "--sum-interval") && i + 1 < argc) {
            sum_interval = std::stoull(argv[++i]);
        } else if (arg == "--benchmark-readings" && i + 1 < argc) {
            benchmark_readings = std::stoull(argv[++i]);
        } else if (arg == "--metrics" && i + 1 < argc) {
            metrics_file = argv[++i];
        } else if (arg == "--threads" && i + 1 < argc) {
            threads = std::stoull(argv[++i]);
        } else if (arg == "--quiet") {
            quiet = true;
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }

    if (sum_interval == 0) {
        sum_interval = std::max(static_cast<size_t>(10), expected_devices / 10);
    }

    std::cout << CYAN BOLD "SGX Smart Grid Server v2.1" RESET << std::endl;
    std::cout << GREEN "Running inside Intel SGX enclave" RESET << std::endl;
    std::cout << CYAN "Configuration: Port=" << port
              << ", Expected Devices=" << expected_devices
              << ", Sum Interval=" << sum_interval
              << ", Threads=" << (threads ? std::to_string(threads) : std::string("auto"))
              << RESET << std::endl;

    SmartGridServer server(port, expected_devices, sum_interval,
                           benchmark_readings, metrics_file, quiet, threads);
    if (!server.start()) return 1;

    server.run();
    return 0;
}

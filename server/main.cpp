// server/main.cpp
#include "server.hpp"
#include "logger.hpp"
#include <iostream>
#include <string>
#include <csignal>
#include <memory>

std::unique_ptr<SmartGridServer> server_instance;

void signal_handler(int signal) {
    Logger::info("Received signal " + std::to_string(signal) + " - shutting down gracefully");
    if (server_instance) {
        server_instance->stop();
    }
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n"
              << "Options:\n"
              << "  -p, --port PORT             Server port (default: 8890)\n"
              << "  -d, --devices NUM           Expected number of devices (default: 100)\n"
              << "  -s, --sum-interval NUM      Sum every N readings (default: devices/10)\n"
              << "      --benchmark-readings N  Stop after N readings and write metrics\n"
              << "      --benchmark-sums N      Stop after N power summations and write metrics\n"
              << "      --metrics FILE          Write CSV metrics to FILE\n"
              << "      --threads N             Use N worker threads (default: auto)\n"
              << "      --quiet                 Suppress periodic logs\n"
              << "  -h, --help                  Show this help\n";
}

int main(int argc, char* argv[]) {
    int port = 8890;
    size_t expected_devices = 100;
    size_t sum_interval = 0;
    size_t benchmark_readings = 0;
    size_t benchmark_sums = 0;
    std::string metrics_file;
    size_t threads = 0;
    bool quiet = false;

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
        } else if (arg == "--benchmark-sums" && i + 1 < argc) {
            benchmark_sums = std::stoull(argv[++i]);
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
        sum_interval = expected_devices;
    }

    std::cout << CYAN BOLD "Smart Grid Server v2.1" RESET << std::endl;
    std::cout << CYAN "Configuration: Port=" << port
              << ", Expected Devices=" << expected_devices
              << ", Sum Interval=" << sum_interval
              << ", Threads=" << (threads ? std::to_string(threads) : std::string("auto"))
              << RESET << std::endl;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    SmartGridServer server(port, expected_devices, sum_interval,
                           benchmark_readings, benchmark_sums, metrics_file, quiet, threads);
    if (!server.start()) return 1;

    global_server.store(&server);
    server.run();
    global_server.store(nullptr);

    return 0;
}

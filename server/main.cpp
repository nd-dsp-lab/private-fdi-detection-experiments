#include "server.hpp"
#include "logger.hpp"
#include <iostream>

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS]\n"
              << "Options:\n"
              << "  -p, --port PORT        Server port (default: 8890)\n"
              << "  -d, --devices NUM      Expected number of devices (default: 100)\n"
              << "  -s, --sum-interval NUM Sum every N readings (default: devices/10)\n"
              << "  -h, --help            Show this help\n";
}

int main(int argc, char* argv[]) {
    int port = 8890;
    size_t expected_devices = 100;
    size_t sum_interval = 0;

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
        } else {
            std::cerr << "Unknown option: " << arg << std::endl;
            print_usage(argv[0]);
            return 1;
        }
    }

    if (sum_interval == 0) {
        sum_interval = std::max(static_cast<size_t>(10), expected_devices / 10);
    }

    std::cout << CYAN BOLD "Smart Grid Server v2.0" RESET << std::endl;
    std::cout << CYAN "Configuration: Port=" << port
              << ", Expected Devices=" << expected_devices
              << ", Sum Interval=" << sum_interval << RESET << std::endl;

    SmartGridServer server(port, expected_devices, sum_interval);
    if (!server.start()) return 1;

    server.run();
    return 0;
}

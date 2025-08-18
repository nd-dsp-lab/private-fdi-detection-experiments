// server/server.hpp
#pragma once
#include "aes_manager.hpp"
#include "power_processor.hpp"
#include "thread_pool.hpp"
#include "../common/protocol.hpp"
#include <atomic>
#include <chrono>
#include <string>

class SmartGridServer {
private:
    int server_fd{-1};
    int port;
    AESManager aes_manager;
    PowerSumProcessor processor;
    ThreadPool thread_pool;

    std::atomic<size_t> total_readings{0};

    // Timing
    std::atomic<bool> started{false};
    std::chrono::steady_clock::time_point first_read_time;

    // Existing (constructor used to set start_time)
    std::chrono::steady_clock::time_point start_time;

    size_t expected_devices;
    size_t log_interval;

    // Benchmark additions
    size_t benchmark_target{0};           // Stop after this many readings if > 0
    std::string metrics_file;             // Write JSON metrics here if not empty
    std::atomic<bool> done{false};        // Signal shutdown
    bool quiet{false};                    // Suppress periodic logs
    size_t thread_count{0};               // 0 => auto (existing behavior)

    void handle_client(int client_fd);
    MeterReading parse_binary_reading(const std::vector<uint8_t>& data);
    std::string read_line(int fd);
    ssize_t read_full(int fd, void* buf, size_t count);
    void process_reading(const MeterReading& reading);

    void finalize_benchmark(double seconds);
    void stop_accept_loop();

public:
    SmartGridServer(int p, size_t devices, size_t sum_interval,
                    size_t benchmark_target = 0,
                    std::string metrics_file = "",
                    bool quiet = false,
                    size_t threads = 0);

    bool start();
    void run();
};

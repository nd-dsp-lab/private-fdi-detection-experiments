#pragma once
#include "aes_manager.hpp"
#include "power_processor.hpp"
#include "thread_pool.hpp"
#include "../common/protocol.hpp"
#include <atomic>
#include <chrono>

class SmartGridServer {
private:
    int server_fd;
    int port;
    AESManager aes_manager;
    PowerSumProcessor processor;
    ThreadPool thread_pool;
    std::atomic<size_t> total_readings{0};
    std::chrono::steady_clock::time_point start_time;
    size_t expected_devices;
    size_t log_interval;

    void handle_client(int client_fd);
    MeterReading parse_binary_reading(const std::vector<uint8_t>& data);
    std::string read_line(int fd);
    ssize_t read_full(int fd, void* buf, size_t count);
    void process_reading(const MeterReading& reading);

public:
    SmartGridServer(int p, size_t devices, size_t sum_interval);
    bool start();
    void run();
};

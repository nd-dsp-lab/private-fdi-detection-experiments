// server/server.cpp
#include "server.hpp"
#include "logger.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <thread>

// Global server pointer for signal handling
std::atomic<SmartGridServer*> global_server{nullptr};

SmartGridServer::SmartGridServer(int p, size_t devices, size_t sum_interval,
                                 size_t bench_target,
                                 size_t bench_sum_target,
                                 std::string metrics,
                                 bool q,
                                 size_t threads)
    : port(p),
      processor(sum_interval),
      expected_devices(devices),
      log_interval(std::max(static_cast<size_t>(100), devices / 100)),
      benchmark_target(bench_target),
      benchmark_sum_target(bench_sum_target),
      metrics_file(std::move(metrics)),
      quiet(q),
      thread_count(threads) {

    start_time = std::chrono::steady_clock::now();

    // Initialize thread pool
    size_t num_threads = threads > 0
        ? threads
        : std::min(static_cast<size_t>(std::thread::hardware_concurrency() * 2), static_cast<size_t>(120));
    thread_pool = std::make_unique<ThreadPool>(num_threads);

    // Set up sum-based benchmark callback
    if (benchmark_sum_target > 0) {
        processor.set_benchmark_target(benchmark_sum_target, [this]() {
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(now - first_read_time).count();
            finalize_benchmark(elapsed);
        });
    }
}

SmartGridServer::~SmartGridServer() {
    stop();
}

bool SmartGridServer::start() {
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        Logger::error("Socket creation failed");
        return false;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));

    struct sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
        Logger::error("Bind failed on port " + std::to_string(port));
        return false;
    }

    int backlog = std::min(static_cast<int>(expected_devices), 1024);
    if (listen(server_fd, backlog) < 0) {
        Logger::error("Listen failed");
        return false;
    }

    std::stringstream ss;
    ss << "Smart Grid Server listening on port " << port
       << " (expecting " << expected_devices << " devices"
       << ", threads=" << (thread_count ? std::to_string(thread_count) : std::string("auto"));
    if (benchmark_sum_target > 0) {
        ss << ", benchmark target: " << benchmark_sum_target << " summations";
    }
    ss << ")";
    Logger::success(ss.str());
    return true;
}

void SmartGridServer::run() {
    if (server_fd == -1) {
        Logger::error("Server not started - call start() first");
        return;
    }

    Logger::info("Server running - waiting for connections...");

    while (!done.load()) {
        struct sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);

        int client_socket = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_socket < 0) {
            if (!done.load()) {
                Logger::error("Accept failed");
            }
            continue;
        }

        connected_devices++;

        // Handle client in thread pool
        thread_pool->enqueue([this, client_socket]() {
            handle_client(client_socket);
        });
    }

    Logger::info("Server stopped accepting connections");
}

void SmartGridServer::handle_client(int client_socket) {
    std::string buffer;
    buffer.reserve(1024);

    while (!done.load()) {
        // Read device ID and length line
        std::string header = read_line(client_socket);
        if (header.empty()) break;

        // Parse header: "device_id:length"
        size_t colon_pos = header.find(':');
        if (colon_pos == std::string::npos) continue;

        std::string device_id = header.substr(0, colon_pos);
        size_t data_length = std::stoull(header.substr(colon_pos + 1));

        // Read encrypted data
        std::vector<uint8_t> encrypted_data(data_length);
        if (read_full(client_socket, encrypted_data.data(), data_length) != static_cast<ssize_t>(data_length)) {
            break;
        }

        try {
            // Decrypt and parse reading
            auto decrypted = aes_manager.decrypt_data(device_id, encrypted_data);
            MeterReading reading = parse_binary_reading(decrypted);
            reading.device_id = device_id;
            process_reading(reading);
        } catch (const std::exception& e) {
            Logger::error("Failed to process reading from " + device_id + ": " + e.what());
        }
    }

    close(client_socket);
    connected_devices--;
}

std::string SmartGridServer::read_line(int fd) {
    std::string line;
    char c;
    while (recv(fd, &c, 1, 0) == 1) {
        if (c == '\n') break;
        if (c != '\r') line += c;
    }
    return line;
}

ssize_t SmartGridServer::read_full(int fd, void* buf, size_t count) {
    size_t total = 0;
    char* ptr = static_cast<char*>(buf);

    while (total < count) {
        ssize_t n = recv(fd, ptr + total, count - total, 0);
        if (n <= 0) return n;
        total += n;
    }
    return total;
}

MeterReading SmartGridServer::parse_binary_reading(const std::vector<uint8_t>& data) {
    if (data.size() < 11) {
        throw std::runtime_error("Invalid binary reading size");
    }

    MeterReading reading;

    // Parse binary format: timestamp(4) + device_num(2) + voltage(2) + current(2) + power(2) + frequency(1)
    uint32_t timestamp = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
    uint16_t device_num = (data[4] << 8) | data[5];
    uint16_t voltage = (data[6] << 8) | data[7];
    uint16_t current = (data[8] << 8) | data[9];
    uint16_t power = (data[10] << 8) | data[11];
    uint8_t frequency = data[12];

    reading.timestamp = timestamp;
    reading.voltage = voltage / 10.0;  // Convert from decidegrees
    reading.current = current / 100.0; // Convert from centiamps
    reading.power = power;
    reading.frequency = frequency;

    return reading;
}

void SmartGridServer::process_reading(const MeterReading& reading) {
    // Set first read time
    auto now = std::chrono::steady_clock::now();
    if (!started.exchange(true)) {
        first_read_time = now;
        Logger::info("First reading received - benchmark timer started");
    }

    // Process the reading (this may trigger the benchmark callback)
    processor.add_reading(reading.power);

    size_t total = ++total_readings;

    // Regular logging
    if (!quiet && total % log_interval == 0) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - first_read_time);
        double rate = elapsed.count() > 0 ? static_cast<double>(total) / elapsed.count() : 0.0;
        std::stringstream ss;
        ss << "Processed " << total << " readings ("
           << std::fixed << std::setprecision(1) << rate << " readings/sec, "
           << processor.get_total_sums() << " sums completed)";
        Logger::info(ss.str());
    }

    // Anomaly detection
    if (reading.power < 0 || reading.voltage < 100 || reading.voltage > 140) {
        std::stringstream ss;
        ss << "Anomaly detected - Device: " << reading.device_id
           << ", Power: " << std::fixed << std::setprecision(1) << reading.power << "W"
           << ", Voltage: " << reading.voltage << "V";
        Logger::alert(ss.str());
    }

    // Legacy reading-based benchmark (if no sum target set)
    if (benchmark_target > 0 && benchmark_sum_target == 0 && total >= benchmark_target && !done.load()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(now - first_read_time).count();
        finalize_benchmark(elapsed);
    }
}

void SmartGridServer::finalize_benchmark(double seconds) {
    static std::atomic<bool> finalized{false};
    if (finalized.exchange(true)) return;

    if (done.exchange(true)) return;

    double throughput = seconds > 0.0 ? static_cast<double>(total_readings.load()) / seconds : 0.0;
    size_t total_sums = processor.get_total_sums();

    if (!metrics_file.empty()) {
        Logger::info("Writing metrics to: " + metrics_file);

        // The system() call is not safe in SGX and is redundant.
        // The benchmark script already creates the 'metrics' directory.
        /*
        size_t last_slash = metrics_file.find_last_of('/');
        if (last_slash != std::string::npos) {
            std::string dir = metrics_file.substr(0, last_slash);
            int result = system(("mkdir -p " + dir).c_str());
            (void)result; // Suppress warning
        }
        */

        bool file_exists = std::ifstream(metrics_file).good();
        std::ofstream out(metrics_file, std::ios::app);
        if (out) {
            if (!file_exists) {
                out << "device_count,thread_count,benchmark_target,benchmark_sum_target,total_readings,total_sums,seconds,throughput_rps,timestamp\n";
            }
            auto now = std::chrono::system_clock::now();
            auto time_t = std::chrono::system_clock::to_time_t(now);
            out << expected_devices << ","
                << (thread_count ? thread_count : 0) << ","
                << benchmark_target << ","
                << benchmark_sum_target << ","
                << total_readings.load() << ","
                << total_sums << ","
                << std::fixed << std::setprecision(6) << seconds << ","
                << std::fixed << std::setprecision(2) << throughput << ","
                << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S") << "\n";
            out.flush();
        } else {
            Logger::error("Failed to open metrics file: " + metrics_file);
        }
    }
}

void SmartGridServer::stop_accept_loop() {
    done.store(true);
}

void SmartGridServer::stop() {
    done.store(true);
    if (server_fd != -1) {
        close(server_fd);
        server_fd = -1;
    }
}

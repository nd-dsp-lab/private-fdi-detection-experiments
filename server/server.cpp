#include "server.hpp"
#include "logger.hpp"
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <thread>

SmartGridServer::SmartGridServer(int p, size_t devices, size_t sum_interval)
    : port(p), processor(sum_interval),
      thread_pool(std::min(static_cast<size_t>(std::thread::hardware_concurrency() * 2), static_cast<size_t>(256))),
      expected_devices(devices), log_interval(std::max(static_cast<size_t>(100), devices / 100)) {
    start_time = std::chrono::steady_clock::now();
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

    struct sockaddr_in address;
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

    Logger::success("Smart Grid Server listening on port " + std::to_string(port) +
                   " (expecting " + std::to_string(expected_devices) + " devices)");
    return true;
}

void SmartGridServer::run() {
    while (true) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);

        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) continue;

        thread_pool.enqueue([this, client_fd] {
            handle_client(client_fd);
        });
    }
}

void SmartGridServer::handle_client(int client_fd) {
    try {
        std::string header = read_line(client_fd);
        size_t colon_pos = header.find(':');
        if (colon_pos == std::string::npos) {
            close(client_fd);
            return;
        }

        std::string device_id = header.substr(0, colon_pos);
        int data_length = std::stoi(header.substr(colon_pos + 1));

        if (data_length != 16) {
            close(client_fd);
            return;
        }

        std::vector<uint8_t> encrypted_data(data_length);
        if (read_full(client_fd, encrypted_data.data(), data_length) != data_length) {
            close(client_fd);
            return;
        }

        auto decrypted_data = aes_manager.decrypt_data(device_id, encrypted_data);
        MeterReading reading = parse_binary_reading(decrypted_data);
        process_reading(reading);

        close(client_fd);

    } catch (const std::exception& e) {
        close(client_fd);
    }
}

MeterReading SmartGridServer::parse_binary_reading(const std::vector<uint8_t>& data) {
    if (data.size() < 13) {
        throw std::runtime_error("Insufficient data for binary reading");
    }

    uint32_t timestamp_int;
    uint16_t device_num, voltage_int, current_int, power_int;
    uint8_t frequency_int;

    timestamp_int = (static_cast<uint32_t>(data[0]) << 24) |
                   (static_cast<uint32_t>(data[1]) << 16) |
                   (static_cast<uint32_t>(data[2]) << 8) |
                   static_cast<uint32_t>(data[3]);

    device_num = (static_cast<uint16_t>(data[4]) << 8) | static_cast<uint16_t>(data[5]);
    voltage_int = (static_cast<uint16_t>(data[6]) << 8) | static_cast<uint16_t>(data[7]);
    current_int = (static_cast<uint16_t>(data[8]) << 8) | static_cast<uint16_t>(data[9]);
    power_int = (static_cast<uint16_t>(data[10]) << 8) | static_cast<uint16_t>(data[11]);
    frequency_int = data[12];

    MeterReading reading;
    reading.device_id = "meter_" + std::to_string(device_num);
    reading.timestamp = static_cast<double>(timestamp_int);
    reading.voltage = voltage_int / 10.0;
    reading.current = current_int / 10.0;
    reading.power = static_cast<double>(power_int);
    reading.frequency = frequency_int / 10.0;

    return reading;
}

std::string SmartGridServer::read_line(int fd) {
    std::string line;
    char c;
    while (read(fd, &c, 1) == 1 && c != '\n') {
        if (c != '\r') line += c;
    }
    return line;
}

ssize_t SmartGridServer::read_full(int fd, void* buf, size_t count) {
    size_t bytes_read = 0;
    char* ptr = static_cast<char*>(buf);

    while (bytes_read < count) {
        ssize_t result = read(fd, ptr + bytes_read, count - bytes_read);
        if (result <= 0) break;
        bytes_read += result;
    }
    return bytes_read;
}

void SmartGridServer::process_reading(const MeterReading& reading) {
    processor.add_reading(reading.power);

    size_t total = ++total_readings;
    if (total % log_interval == 0) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time);
        double rate = elapsed.count() > 0 ? static_cast<double>(total) / elapsed.count() : 0;

        std::stringstream ss;
        ss << "Processed " << total << " readings ("
           << std::fixed << std::setprecision(1) << rate << " readings/sec)";
        Logger::info(ss.str());
    }

    if (reading.power < 0 || reading.voltage < 100 || reading.voltage > 140) {
        std::stringstream ss;
        ss << "Anomaly detected - Device: " << reading.device_id
           << ", Power: " << std::fixed << std::setprecision(1) << reading.power << "W"
           << ", Voltage: " << reading.voltage << "V";
        Logger::alert(ss.str());
    }
}

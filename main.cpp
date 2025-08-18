#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <shared_mutex>  // Add this missing header
#include <condition_variable>
#include <queue>
#include <atomic>
#include <memory>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ANSI color codes
#define RESET   "\033[0m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define CYAN    "\033[36m"
#define BOLD    "\033[1m"

struct MeterReading {
    std::string device_id;
    double timestamp;
    double voltage;
    double current;
    double power;
    double frequency;
};

class Logger {
public:
    static std::string timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time_t), "%H:%M:%S");
        return ss.str();
    }

    static void info(const std::string& msg) {
        std::cout << CYAN "[" << timestamp() << "] [INFO]" RESET " " << msg << std::endl;
    }

    static void success(const std::string& msg) {
        std::cout << GREEN "[" << timestamp() << "] [OK]" RESET " " << msg << std::endl;
    }

    static void warning(const std::string& msg) {
        std::cout << YELLOW "[" << timestamp() << "] [WARN]" RESET " " << msg << std::endl;
    }

    static void error(const std::string& msg) {
        std::cout << RED "[" << timestamp() << "] [ERROR]" RESET " " << msg << std::endl;
    }

    static void alert(const std::string& msg) {
        std::cout << RED BOLD "[" << timestamp() << "] [ALERT]" RESET " " << msg << std::endl;
    }

    static void sum_result(const std::string& msg) {
        std::cout << GREEN BOLD "[" << timestamp() << "] [SUM]" RESET " " << msg << std::endl;
    }
};

class AESManager {
private:
    std::unordered_map<std::string, std::vector<uint8_t>> device_keys;
    mutable std::shared_mutex keys_mutex;

    std::vector<uint8_t> pbkdf2_sha256(const std::string& password, const std::string& salt, int iterations, int key_length) {
        std::vector<uint8_t> key(key_length);
        if (PKCS5_PBKDF2_HMAC(password.c_str(), password.length(),
                              (const unsigned char*)salt.c_str(), salt.length(),
                              iterations, EVP_sha256(),
                              key_length, key.data()) != 1) {
            throw std::runtime_error("PBKDF2 key derivation failed");
        }
        return key;
    }

public:
    std::vector<uint8_t> get_or_generate_key(const std::string& device_id) {
        // Try read lock first
        {
            std::shared_lock<std::shared_mutex> lock(keys_mutex);
            auto it = device_keys.find(device_id);
            if (it != device_keys.end()) {
                return it->second;
            }
        }

        // Need to generate key - upgrade to write lock
        std::unique_lock<std::shared_mutex> lock(keys_mutex);

        // Double-check pattern
        auto it = device_keys.find(device_id);
        if (it != device_keys.end()) {
            return it->second;
        }

        std::string salt = device_id;
        salt.resize(16, '0');
        std::string password = "smart_meter_" + device_id;

        auto key = pbkdf2_sha256(password, salt, 100000, 32);
        device_keys[device_id] = key;

        Logger::info("Generated key for device " + device_id);
        return key;
    }

    std::vector<uint8_t> decrypt_data(const std::string& device_id, const std::vector<uint8_t>& encrypted_data) {
        if (encrypted_data.size() != 16) {
            throw std::runtime_error("Expected exactly 16 bytes, got " + std::to_string(encrypted_data.size()));
        }

        auto key = get_or_generate_key(device_id);

        // Regenerate deterministic IV
        unsigned char hash[32];
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (!ctx) {
            throw std::runtime_error("Failed to create hash context");
        }

        EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
        EVP_DigestUpdate(ctx, key.data(), key.size());
        EVP_DigestUpdate(ctx, device_id.c_str(), device_id.length());
        EVP_DigestFinal_ex(ctx, hash, nullptr);
        EVP_MD_CTX_free(ctx);

        std::vector<uint8_t> iv(hash, hash + 16);

        std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)> cipher_ctx(
            EVP_CIPHER_CTX_new(), EVP_CIPHER_CTX_free);

        if (!cipher_ctx) {
            throw std::runtime_error("Failed to create cipher context");
        }

        if (EVP_DecryptInit_ex(cipher_ctx.get(), EVP_aes_256_cbc(), nullptr, key.data(), iv.data()) != 1) {
            throw std::runtime_error("Failed to initialize decryption");
        }

        std::vector<uint8_t> plaintext(32);
        int len, plaintext_len = 0;

        if (EVP_DecryptUpdate(cipher_ctx.get(), plaintext.data(), &len, encrypted_data.data(), encrypted_data.size()) != 1) {
            throw std::runtime_error("Failed to decrypt data");
        }
        plaintext_len = len;

        if (EVP_DecryptFinal_ex(cipher_ctx.get(), plaintext.data() + len, &len) != 1) {
            throw std::runtime_error("Failed to finalize decryption");
        }
        plaintext_len += len;

        plaintext.resize(plaintext_len);
        return plaintext;
    }
};

class PowerSumProcessor {
private:
    std::vector<double> power_readings;
    std::mutex readings_mutex;
    const size_t target_count;
    std::atomic<size_t> current_count{0};

public:
    PowerSumProcessor(size_t count = 100) : target_count(count) {
        power_readings.reserve(target_count);
        Logger::info("PowerSumProcessor initialized - will sum every " + std::to_string(target_count) + " readings");
    }

    bool add_reading(double power) {
        std::lock_guard<std::mutex> lock(readings_mutex);

        power_readings.push_back(power);
        size_t count = ++current_count;

        if (count >= target_count) {
            double sum = 0.0;
            for (double p : power_readings) {
                sum += p;
            }

            std::stringstream ss;
            ss << "Sum of " << target_count << " power readings: "
               << std::fixed << std::setprecision(2) << sum << " WATTS";
            Logger::sum_result(ss.str());

            power_readings.clear();
            current_count = 0;
            return true;
        }
        return false;
    }
};

class ThreadPool {
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    std::atomic<bool> stop{false};

public:
    ThreadPool(size_t threads = std::thread::hardware_concurrency()) {
        for (size_t i = 0; i < threads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex);
                        condition.wait(lock, [this] { return stop || !tasks.empty(); });

                        if (stop && tasks.empty()) return;

                        task = std::move(tasks.front());
                        tasks.pop();
                    }
                    task();
                }
            });
        }
    }

    template<class F>
    void enqueue(F&& f) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if (stop) return;
            tasks.emplace(std::forward<F>(f));
        }
        condition.notify_one();
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread &worker : workers) {
            worker.join();
        }
    }
};

class SmartGridServer {
private:
    int server_fd;
    int port;
    AESManager aes_manager;
    PowerSumProcessor processor;
    ThreadPool thread_pool;
    std::atomic<size_t> total_readings{0};
    std::chrono::steady_clock::time_point start_time;

public:
    SmartGridServer(int p = 8890) : port(p), processor(10), thread_pool(8) {
        start_time = std::chrono::steady_clock::now();
    }

    bool start() {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == 0) {
            Logger::error("Socket creation failed");
            return false;
        }

        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
            Logger::error("Bind failed on port " + std::to_string(port));
            return false;
        }

        if (listen(server_fd, 128) < 0) {
            Logger::error("Listen failed");
            return false;
        }

        Logger::success("Smart Grid Server listening on port " + std::to_string(port));
        return true;
    }

    void run() {
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

private:
    void handle_client(int client_fd) {
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
                Logger::warning("Invalid data length from " + device_id + ": " + std::to_string(data_length));
                close(client_fd);
                return;
            }

            std::vector<uint8_t> encrypted_data(data_length);
            if (read_full(client_fd, encrypted_data.data(), data_length) != data_length) {
                close(client_fd);
                return;
            }

            auto decrypted_data = aes_manager.decrypt_data(device_id, encrypted_data);

            // Parse binary data
            MeterReading reading = parse_binary_reading(decrypted_data);
            process_reading(reading);

            close(client_fd);

        } catch (const std::exception& e) {
            Logger::error("Error handling client: " + std::string(e.what()));
            close(client_fd);
        }
    }

    MeterReading parse_binary_reading(const std::vector<uint8_t>& data) {
        if (data.size() < 13) {
            throw std::runtime_error("Insufficient data for binary reading");
        }

        uint32_t timestamp_int;
        uint16_t device_num, voltage_int, current_int, power_int;
        uint8_t frequency_int;

        // Unpack binary data (big-endian)
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

    std::string read_line(int fd) {
        std::string line;
        char c;
        while (read(fd, &c, 1) == 1 && c != '\n') {
            if (c != '\r') line += c;
        }
        return line;
    }

    ssize_t read_full(int fd, void* buf, size_t count) {
        size_t bytes_read = 0;
        char* ptr = static_cast<char*>(buf);

        while (bytes_read < count) {
            ssize_t result = read(fd, ptr + bytes_read, count - bytes_read);
            if (result <= 0) break;
            bytes_read += result;
        }
        return bytes_read;
    }

    void process_reading(const MeterReading& reading) {
        processor.add_reading(reading.power);

        size_t total = ++total_readings;
        if (total % 100 == 0) {
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
};

int main() {
    std::cout << CYAN BOLD "Smart Grid Server v1.0" RESET << std::endl;
    std::cout << CYAN "Expecting 128-bit (16 byte) encrypted messages" RESET << std::endl;

    SmartGridServer server(8890);
    if (!server.start()) return 1;

    server.run();
    return 0;
}

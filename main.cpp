#include <iostream>
#include <string>
#include <vector>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <openssl/evp.h>
#include <openssl/aes.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct MeterReading {
    std::string device_id;
    double timestamp;
    double voltage;
    double current;
    double power;
    double frequency;
};

class AESManager {
private:
    std::unordered_map<std::string, std::vector<uint8_t>> device_keys;
    std::mutex keys_mutex;

    // PBKDF2 implementation to match Python exactly
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
    std::vector<uint8_t> generate_device_key(const std::string& device_id) {
        std::lock_guard<std::mutex> lock(keys_mutex);

        // Match Python's salt generation exactly
        std::string salt = device_id;
        salt.resize(16, '0');  // Pad or truncate to 16 bytes

        std::string password = "smart_meter_" + device_id;

        // Use same parameters as Python: 100000 iterations, 32 byte key
        auto key = pbkdf2_sha256(password, salt, 100000, 32);

        device_keys[device_id] = key;

        std::cout << "Generated key for device " << device_id << std::endl;
        return key;
    }

    std::vector<uint8_t> decrypt_data(const std::string& device_id, const std::vector<uint8_t>& encrypted_data) {
        std::lock_guard<std::mutex> lock(keys_mutex);

        if (device_keys.find(device_id) == device_keys.end()) {
            generate_device_key(device_id);
        }

        const auto& key = device_keys[device_id];

        if (encrypted_data.size() < 16) {
            throw std::runtime_error("Encrypted data too short");
        }

        // Extract IV (first 16 bytes) and ciphertext
        std::vector<uint8_t> iv(encrypted_data.begin(), encrypted_data.begin() + 16);
        std::vector<uint8_t> ciphertext(encrypted_data.begin() + 16, encrypted_data.end());

        std::cout << "Decrypting " << ciphertext.size() << " bytes for device " << device_id << std::endl;

        // Decrypt using AES-256-CBC
        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        if (!ctx) {
            throw std::runtime_error("Failed to create cipher context");
        }

        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv.data()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Failed to initialize decryption");
        }

        std::vector<uint8_t> plaintext(ciphertext.size() + AES_BLOCK_SIZE);
        int len;
        int plaintext_len;

        if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext.data(), ciphertext.size()) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Failed to decrypt data");
        }
        plaintext_len = len;

        if (EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len) != 1) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("Failed to finalize decryption");
        }
        plaintext_len += len;

        EVP_CIPHER_CTX_free(ctx);

        plaintext.resize(plaintext_len);

        std::cout << "Successfully decrypted " << plaintext_len << " bytes" << std::endl;
        return plaintext;
    }
};

class PowerSumProcessor {
private:
    std::vector<double> power_readings;
    std::mutex readings_mutex;
    size_t target_count;
    size_t current_count;

public:
    PowerSumProcessor(size_t count = 100) : target_count(count), current_count(0) {
        power_readings.reserve(target_count);
        std::cout << "PowerSumProcessor initialized - will sum every " << target_count << " readings" << std::endl;
    }

    bool add_reading(double power) {
        std::lock_guard<std::mutex> lock(readings_mutex);

        power_readings.push_back(power);
        current_count++;

        std::cout << "Added reading " << current_count << "/" << target_count
                  << " (Power: " << std::fixed << std::setprecision(1) << power << "W)" << std::endl;

        if (current_count >= target_count) {
            double sum = compute_sum();
            std::cout << "\n*** COMPUTED SUM OF " << target_count << " POWER READINGS: "
                     << std::fixed << std::setprecision(2) << sum << " WATTS ***\n" << std::endl;

            // Reset for next batch
            power_readings.clear();
            current_count = 0;
            return true;
        }
        return false;
    }

private:
    double compute_sum() {
        double sum = 0.0;
        for (double power : power_readings) {
            sum += power;
        }
        return sum;
    }
};

class SmartGridServer {
private:
    int server_fd;
    int port;
    AESManager aes_manager;
    PowerSumProcessor processor;
    std::mutex stats_mutex;
    size_t total_readings;
    std::chrono::steady_clock::time_point start_time;

public:
    SmartGridServer(int p = 8889) : port(p), total_readings(0), processor(10) {  // Changed to 10 for faster testing
        start_time = std::chrono::steady_clock::now();
    }

    bool start() {
        server_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd == 0) {
            std::cerr << "Socket creation failed" << std::endl;
            return false;
        }

        int opt = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(port);

        if (bind(server_fd, (struct sockaddr*)&address, sizeof(address)) < 0) {
            std::cerr << "Bind failed" << std::endl;
            return false;
        }

        if (listen(server_fd, 10) < 0) {
            std::cerr << "Listen failed" << std::endl;
            return false;
        }

        std::cout << "C++ Smart Grid Server listening on port " << port << std::endl;
        return true;
    }

    void run() {
        while (true) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);

            int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd < 0) {
                std::cerr << "Accept failed" << std::endl;
                continue;
            }

            std::thread client_thread(&SmartGridServer::handle_client, this, client_fd);
            client_thread.detach();
        }
    }

private:
    void handle_client(int client_fd) {
        try {
            std::cout << "\n--- New client connection ---" << std::endl;

            std::string header = read_line(client_fd);
            std::cout << "Received header: " << header << std::endl;

            size_t colon_pos = header.find(':');
            if (colon_pos == std::string::npos) {
                std::cout << "Invalid header format" << std::endl;
                close(client_fd);
                return;
            }

            std::string device_id = header.substr(0, colon_pos);
            int data_length = std::stoi(header.substr(colon_pos + 1));

            std::cout << "Device: " << device_id << ", Data length: " << data_length << std::endl;

            std::vector<uint8_t> encrypted_data(data_length);
            int bytes_read = 0;
            while (bytes_read < data_length) {
                int result = read(client_fd, encrypted_data.data() + bytes_read,
                                data_length - bytes_read);
                if (result <= 0) break;
                bytes_read += result;
            }

            if (bytes_read != data_length) {
                std::cerr << "Failed to read complete data: got " << bytes_read
                         << " expected " << data_length << std::endl;
                close(client_fd);
                return;
            }

            // Decrypt data
            auto decrypted_data = aes_manager.decrypt_data(device_id, encrypted_data);

            // Parse JSON
            std::string json_str(decrypted_data.begin(), decrypted_data.end());
            std::cout << "Decrypted JSON: " << json_str << std::endl;

            json reading_json = json::parse(json_str);

            MeterReading reading;
            reading.device_id = reading_json["device_id"];
            reading.timestamp = reading_json["timestamp"];
            reading.voltage = reading_json["voltage"];
            reading.current = reading_json["current"];
            reading.power = reading_json["power"];
            reading.frequency = reading_json["frequency"];

            process_reading(reading);

            close(client_fd);

        } catch (const std::exception& e) {
            std::cerr << "Error handling client: " << e.what() << std::endl;
            close(client_fd);
        }
    }

    std::string read_line(int fd) {
        std::string line;
        char c;
        while (read(fd, &c, 1) == 1 && c != '\n') {
            if (c != '\r') {
                line += c;
            }
        }
        return line;
    }

    void process_reading(const MeterReading& reading) {
        processor.add_reading(reading.power);

        {
            std::lock_guard<std::mutex> lock(stats_mutex);
            total_readings++;

            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time);
            double rate = elapsed.count() > 0 ? static_cast<double>(total_readings) / elapsed.count() : 0;

            std::cout << "Total processed: " << total_readings << " readings "
                     << "(" << std::fixed << std::setprecision(1) << rate << " readings/sec)"
                     << std::endl;
        }

        if (reading.power < 0 || reading.voltage < 100 || reading.voltage > 140) {
            std::cout << "*** ANOMALY DETECTED *** Device " << reading.device_id << ": "
                     << "Power=" << std::fixed << std::setprecision(1) << reading.power << "W, "
                     << "Voltage=" << reading.voltage << "V" << std::endl;
        }
    }
};

int main() {
    SmartGridServer server(8890);

    if (!server.start()) {
        return 1;
    }

    std::cout << "Starting C++ server for smart grid data processing..." << std::endl;
    std::cout << "Will sum every 10 power readings received (for faster testing)." << std::endl;

    server.run();

    return 0;
}

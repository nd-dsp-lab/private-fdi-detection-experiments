#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <shared_mutex>
#include <memory>
#include <openssl/evp.h>

class AESManager {
private:
    std::unordered_map<std::string, std::vector<uint8_t>> device_keys;
    mutable std::shared_mutex keys_mutex;

    std::vector<uint8_t> pbkdf2_sha256(const std::string& password, const std::string& salt,
                                       int iterations, int key_length);

public:
    std::vector<uint8_t> get_or_generate_key(const std::string& device_id);
    std::vector<uint8_t> decrypt_data(const std::string& device_id,
                                      const std::vector<uint8_t>& encrypted_data);
};
#include "aes_manager.hpp"
#include <stdexcept>
#include <mutex>
#include <openssl/rand.h>

std::vector<uint8_t> AESManager::pbkdf2_sha256(const std::string& password, const std::string& salt,
                                                int iterations, int key_length) {
    std::vector<uint8_t> key(key_length);
    if (PKCS5_PBKDF2_HMAC(password.c_str(), password.length(),
                          (const unsigned char*)salt.c_str(), salt.length(),
                          iterations, EVP_sha256(),
                          key_length, key.data()) != 1) {
        throw std::runtime_error("PBKDF2 key derivation failed");
    }
    return key;
}

std::vector<uint8_t> AESManager::get_or_generate_key(const std::string& device_id) {
    {
        std::shared_lock<std::shared_mutex> lock(keys_mutex);
        auto it = device_keys.find(device_id);
        if (it != device_keys.end()) {
            return it->second;
        }
    }

    std::unique_lock<std::shared_mutex> lock(keys_mutex);
    auto it = device_keys.find(device_id);
    if (it != device_keys.end()) {
        return it->second;
    }

    std::string salt = device_id;
    salt.resize(16, '0');
    std::string password = "smart_meter_" + device_id;

    auto key = pbkdf2_sha256(password, salt, 100000, 32);
    device_keys[device_id] = key;

    return key;
}

std::vector<uint8_t> AESManager::decrypt_data(const std::string& device_id,
                                              const std::vector<uint8_t>& encrypted_data) {
    if (encrypted_data.size() != 16) {
        throw std::runtime_error("Expected exactly 16 bytes, got " + std::to_string(encrypted_data.size()));
    }

    auto key = get_or_generate_key(device_id);

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

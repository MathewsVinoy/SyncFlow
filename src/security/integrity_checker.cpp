#include "syncflow/security/integrity_checker.h"

#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>

namespace syncflow::security {

std::string IntegrityChecker::bytes_to_hex(const std::vector<unsigned char>& bytes) {
    std::ostringstream oss;
    for (unsigned char b : bytes) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return oss.str();
}

std::string IntegrityChecker::compute_sha256(const void* data, std::size_t data_len) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    SHA256_Update(&sha256, data, data_len);
    SHA256_Final(hash, &sha256);

    std::vector<unsigned char> hash_vec(hash, hash + SHA256_DIGEST_LENGTH);
    return bytes_to_hex(hash_vec);
}

std::string IntegrityChecker::compute_file_sha256(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file) return {};

    SHA256_CTX sha256;
    SHA256_Init(&sha256);

    std::vector<char> buffer(4096);
    while (file.read(buffer.data(), buffer.size())) {
        SHA256_Update(&sha256, buffer.data(), file.gcount());
    }

    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &sha256);

    std::vector<unsigned char> hash_vec(hash, hash + SHA256_DIGEST_LENGTH);
    return bytes_to_hex(hash_vec);
}

std::string IntegrityChecker::compute_hmac_sha256(const std::string& key, const void* data, std::size_t data_len) {
    unsigned char hmac[EVP_MAX_MD_SIZE];
    unsigned int hmac_len = 0;

    HMAC(EVP_sha256(), key.c_str(), static_cast<int>(key.size()),
         static_cast<const unsigned char*>(data), data_len, hmac, &hmac_len);

    std::vector<unsigned char> hmac_vec(hmac, hmac + hmac_len);
    return bytes_to_hex(hmac_vec);
}

std::string IntegrityChecker::compute_file_hmac(const std::string& key, const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file) return {};

    HMAC_CTX* ctx = HMAC_CTX_new();
    if (!ctx) return {};

    HMAC_Init_ex(ctx, key.c_str(), static_cast<int>(key.size()), EVP_sha256(), nullptr);

    std::vector<char> buffer(4096);
    while (file.read(buffer.data(), buffer.size())) {
        HMAC_Update(ctx, reinterpret_cast<const unsigned char*>(buffer.data()), file.gcount());
    }

    unsigned char hmac[EVP_MAX_MD_SIZE];
    unsigned int hmac_len = 0;
    HMAC_Final(ctx, hmac, &hmac_len);
    HMAC_CTX_free(ctx);

    std::vector<unsigned char> hmac_vec(hmac, hmac + hmac_len);
    return bytes_to_hex(hmac_vec);
}

bool IntegrityChecker::verify_hmac(const std::string& key, const void* data, std::size_t data_len, 
                                   const std::string& expected_hmac) {
    const std::string computed = compute_hmac_sha256(key, data, data_len);
    return computed == expected_hmac;
}

}  // namespace syncflow::security

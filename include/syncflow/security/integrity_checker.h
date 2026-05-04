#pragma once

#include <string>
#include <cstdint>
#include <vector>

namespace syncflow::security {

class IntegrityChecker {
public:
    // Compute HMAC-SHA256 hash of data
    static std::string compute_hmac_sha256(const std::string& key, const void* data, std::size_t data_len);

    // Compute HMAC-SHA256 of a file
    static std::string compute_file_hmac(const std::string& key, const std::string& file_path);

    // Verify HMAC-SHA256
    static bool verify_hmac(const std::string& key, const void* data, std::size_t data_len, 
                           const std::string& expected_hmac);

    // Compute SHA256 hash of data (for fingerprinting)
    static std::string compute_sha256(const void* data, std::size_t data_len);

    // Compute SHA256 of a file
    static std::string compute_file_sha256(const std::string& file_path);

    // Format hash as hex string
    static std::string bytes_to_hex(const std::vector<unsigned char>& bytes);
};

}  // namespace syncflow::security

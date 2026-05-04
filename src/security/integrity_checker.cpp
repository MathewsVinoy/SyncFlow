#include "syncflow/security/integrity_checker.h"

#include <openssl/evp.h>
#include <openssl/err.h>
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
    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx) return {};

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;

    if (!EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr) ||
        !EVP_DigestUpdate(mdctx, data, data_len) ||
        !EVP_DigestFinal_ex(mdctx, hash, &hash_len)) {
        EVP_MD_CTX_free(mdctx);
        return {};
    }

    EVP_MD_CTX_free(mdctx);
    std::vector<unsigned char> hash_vec(hash, hash + hash_len);
    return bytes_to_hex(hash_vec);
}

std::string IntegrityChecker::compute_file_sha256(const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file) return {};

    EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
    if (!mdctx) return {};

    if (!EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr)) {
        EVP_MD_CTX_free(mdctx);
        return {};
    }

    std::vector<char> buffer(4096);
    while (file.read(buffer.data(), buffer.size())) {
        if (!EVP_DigestUpdate(mdctx, buffer.data(), file.gcount())) {
            EVP_MD_CTX_free(mdctx);
            return {};
        }
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    if (!EVP_DigestFinal_ex(mdctx, hash, &hash_len)) {
        EVP_MD_CTX_free(mdctx);
        return {};
    }

    EVP_MD_CTX_free(mdctx);
    std::vector<unsigned char> hash_vec(hash, hash + hash_len);
    return bytes_to_hex(hash_vec);
}

std::string IntegrityChecker::compute_hmac_sha256(const std::string& key, const void* data, std::size_t data_len) {
    EVP_MAC* mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
    if (!mac) return {};

    EVP_MAC_CTX* mctx = EVP_MAC_CTX_new(mac);
    EVP_MAC_free(mac);
    if (!mctx) return {};

    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string("digest", const_cast<char*>("SHA256"), 0),
        OSSL_PARAM_construct_end()
    };

    if (!EVP_MAC_init(mctx, reinterpret_cast<const unsigned char*>(key.c_str()),
                      key.size(), params)) {
        EVP_MAC_CTX_free(mctx);
        return {};
    }

    if (!EVP_MAC_update(mctx, static_cast<const unsigned char*>(data), data_len)) {
        EVP_MAC_CTX_free(mctx);
        return {};
    }

    unsigned char hmac[EVP_MAX_MD_SIZE];
    size_t hmac_len = 0;
    if (!EVP_MAC_final(mctx, hmac, &hmac_len, EVP_MAX_MD_SIZE)) {
        EVP_MAC_CTX_free(mctx);
        return {};
    }

    EVP_MAC_CTX_free(mctx);
    std::vector<unsigned char> hmac_vec(hmac, hmac + hmac_len);
    return bytes_to_hex(hmac_vec);
}

std::string IntegrityChecker::compute_file_hmac(const std::string& key, const std::string& file_path) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file) return {};

    EVP_MAC* mac = EVP_MAC_fetch(nullptr, "HMAC", nullptr);
    if (!mac) return {};

    EVP_MAC_CTX* mctx = EVP_MAC_CTX_new(mac);
    EVP_MAC_free(mac);
    if (!mctx) return {};

    OSSL_PARAM params[] = {
        OSSL_PARAM_construct_utf8_string("digest", const_cast<char*>("SHA256"), 0),
        OSSL_PARAM_construct_end()
    };

    if (!EVP_MAC_init(mctx, reinterpret_cast<const unsigned char*>(key.c_str()),
                      key.size(), params)) {
        EVP_MAC_CTX_free(mctx);
        return {};
    }

    std::vector<char> buffer(4096);
    while (file.read(buffer.data(), buffer.size())) {
        if (!EVP_MAC_update(mctx, reinterpret_cast<const unsigned char*>(buffer.data()),
                           file.gcount())) {
            EVP_MAC_CTX_free(mctx);
            return {};
        }
    }

    unsigned char hmac[EVP_MAX_MD_SIZE];
    size_t hmac_len = 0;
    if (!EVP_MAC_final(mctx, hmac, &hmac_len, EVP_MAX_MD_SIZE)) {
        EVP_MAC_CTX_free(mctx);
        return {};
    }

    EVP_MAC_CTX_free(mctx);
    std::vector<unsigned char> hmac_vec(hmac, hmac + hmac_len);
    return bytes_to_hex(hmac_vec);
}

bool IntegrityChecker::verify_hmac(const std::string& key, const void* data, std::size_t data_len, 
                                   const std::string& expected_hmac) {
    const std::string computed = compute_hmac_sha256(key, data, data_len);
    return computed == expected_hmac;
}

}  // namespace syncflow::security

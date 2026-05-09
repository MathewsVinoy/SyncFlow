#ifndef CRYPTO_UTILS_HPP
#define CRYPTO_UTILS_HPP

#include <string>
#include <vector>

namespace syncflow {
namespace crypto {

std::string sha256(const std::string& data);
std::string sha256_file(const std::string& filepath);

struct KeyPair {
    std::string private_key;
    std::string public_key;
    std::string certificate;
};

KeyPair generate_key_pair();
bool verify_certificate(const std::string& cert_der, const std::string& root_cert_der);

} // namespace crypto
} // namespace syncflow

#endif // CRYPTO_UTILS_HPP

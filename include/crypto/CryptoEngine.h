#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace syncflow::crypto {

class CryptoEngine {
public:
	explicit CryptoEngine(std::string keyMaterial);

	std::vector<std::byte> encrypt(const std::vector<std::byte>& plaintext, std::uint64_t nonce) const;
	std::vector<std::byte> decrypt(const std::vector<std::byte>& ciphertext, std::uint64_t nonce) const;
	std::string sign(const std::string& message) const;
	bool verify(const std::string& message, const std::string& signature) const;

private:
	std::vector<std::byte> applyStream(const std::vector<std::byte>& in, std::uint64_t nonce) const;
	std::uint64_t seed() const;

	std::string keyMaterial_;
};

}  // namespace syncflow::crypto

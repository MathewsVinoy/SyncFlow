#include "crypto/CryptoEngine.h"

#include "sync_engine/HashUtils.h"

#include <array>
#include <cstdint>
#include <sstream>

namespace syncflow::crypto {

CryptoEngine::CryptoEngine(std::string keyMaterial) : keyMaterial_(std::move(keyMaterial)) {}

std::vector<std::byte> CryptoEngine::encrypt(const std::vector<std::byte>& plaintext, std::uint64_t nonce) const {
	return applyStream(plaintext, nonce);
}

std::vector<std::byte> CryptoEngine::decrypt(const std::vector<std::byte>& ciphertext, std::uint64_t nonce) const {
	return applyStream(ciphertext, nonce);
}

std::string CryptoEngine::sign(const std::string& message) const {
	const std::string combined = keyMaterial_ + "|" + message;
	const auto digest = syncflow::hash::fnv1a64(combined.data(), combined.size());
	return syncflow::hash::toHex(digest);
}

bool CryptoEngine::verify(const std::string& message, const std::string& signature) const {
	return sign(message) == signature;
}

std::vector<std::byte> CryptoEngine::applyStream(const std::vector<std::byte>& in, std::uint64_t nonce) const {
	std::vector<std::byte> out(in.size());
	std::uint64_t x = seed() ^ nonce;
	for (std::size_t i = 0; i < in.size(); ++i) {
		x ^= x << 13;
		x ^= x >> 7;
		x ^= x << 17;
		const auto keyByte = static_cast<unsigned char>(x & 0xFFU);
		const auto inByte = static_cast<unsigned char>(in[i]);
		out[i] = static_cast<std::byte>(inByte ^ keyByte);
	}
	return out;
}

std::uint64_t CryptoEngine::seed() const {
	return syncflow::hash::fnv1a64(keyMaterial_.data(), keyMaterial_.size());
}

}  // namespace syncflow::crypto

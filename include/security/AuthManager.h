#pragma once

#include "crypto/CryptoEngine.h"

#include <cstdint>
#include <optional>
#include <string>

namespace syncflow::security {

struct AuthToken {
	std::string deviceId;
	std::int64_t issuedAtUnixSeconds = 0;
	std::uint64_t nonce = 0;
	std::string signature;
};

class AuthManager {
public:
	explicit AuthManager(std::string sharedSecret, std::int64_t tokenTtlSeconds = 60);

	AuthToken issue(const std::string& deviceId, std::int64_t unixSecondsNow, std::uint64_t nonce) const;
	bool verify(const AuthToken& token, std::int64_t unixSecondsNow) const;

	static std::string toWire(const AuthToken& token);
	static std::optional<AuthToken> fromWire(const std::string& text);

private:
	std::string canonicalMessage(const AuthToken& token) const;

	syncflow::crypto::CryptoEngine crypto_;
	std::int64_t tokenTtlSeconds_;
};

}  // namespace syncflow::security

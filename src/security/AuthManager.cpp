#include "security/AuthManager.h"

#include <sstream>
#include <vector>

namespace syncflow::security {

AuthManager::AuthManager(std::string sharedSecret, std::int64_t tokenTtlSeconds)
	: crypto_(std::move(sharedSecret)), tokenTtlSeconds_(tokenTtlSeconds) {}

AuthToken AuthManager::issue(const std::string& deviceId, std::int64_t unixSecondsNow, std::uint64_t nonce) const {
	AuthToken token;
	token.deviceId = deviceId;
	token.issuedAtUnixSeconds = unixSecondsNow;
	token.nonce = nonce;
	token.signature = crypto_.sign(canonicalMessage(token));
	return token;
}

bool AuthManager::verify(const AuthToken& token, std::int64_t unixSecondsNow) const {
	if (token.deviceId.empty()) {
		return false;
	}

	if (token.issuedAtUnixSeconds > unixSecondsNow + 5) {
		return false;
	}

	if ((unixSecondsNow - token.issuedAtUnixSeconds) > tokenTtlSeconds_) {
		return false;
	}

	return crypto_.verify(canonicalMessage(token), token.signature);
}

std::string AuthManager::toWire(const AuthToken& token) {
	std::ostringstream out;
	out << token.deviceId << '|'
	    << token.issuedAtUnixSeconds << '|'
	    << token.nonce << '|'
	    << token.signature;
	return out.str();
}

std::optional<AuthToken> AuthManager::fromWire(const std::string& text) {
	std::stringstream ss(text);
	std::string part;
	std::vector<std::string> tokens;
	while (std::getline(ss, part, '|')) {
		tokens.push_back(part);
	}
	if (tokens.size() != 4) {
		return std::nullopt;
	}

	AuthToken token;
	token.deviceId = tokens[0];
	token.signature = tokens[3];
	try {
		token.issuedAtUnixSeconds = std::stoll(tokens[1]);
		token.nonce = static_cast<std::uint64_t>(std::stoull(tokens[2]));
	} catch (...) {
		return std::nullopt;
	}

	return token;
}

std::string AuthManager::canonicalMessage(const AuthToken& token) const {
	std::ostringstream out;
	out << token.deviceId << ':' << token.issuedAtUnixSeconds << ':' << token.nonce;
	return out.str();
}

}  // namespace syncflow::security

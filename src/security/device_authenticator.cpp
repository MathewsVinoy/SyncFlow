#include "syncflow/security/device_authenticator.h"
#include "syncflow/security/integrity_checker.h"

#include <chrono>
#include <ctime>

namespace syncflow::security {

DeviceAuthenticator::DeviceAuthenticator(const std::string& local_cert_fingerprint)
    : local_fingerprint_(local_cert_fingerprint) {}

bool DeviceAuthenticator::authenticate_peer(const std::string& peer_name,
                                            const std::string& peer_fingerprint,
                                            const std::string& peer_ip,
                                            bool is_trusted) const {
    // Suppress unused parameter warnings
    (void)peer_name;
    (void)peer_ip;
    
    // Reject if fingerprints match (same device)
    if (peer_fingerprint == local_fingerprint_) {
        return false;
    }

    // Reject if device is not trusted
    if (!is_trusted) {
        return false;  // Device must be explicitly approved
    }

    // Additional validation can be added here
    return true;
}

std::string DeviceAuthenticator::generate_auth_challenge() {
    // Generate a random challenge token
    unsigned char challenge_bytes[32];
    for (int i = 0; i < 32; ++i) {
        challenge_bytes[i] = static_cast<unsigned char>(rand() % 256);
    }

    return IntegrityChecker::bytes_to_hex(
        std::vector<unsigned char>(challenge_bytes, challenge_bytes + 32)
    );
}

bool DeviceAuthenticator::verify_auth_response(const std::string& challenge,
                                               const std::string& response) const {
    // Verify that response is the HMAC of challenge with local fingerprint as key
    const std::string expected = IntegrityChecker::compute_hmac_sha256(
        local_fingerprint_, challenge.c_str(), challenge.size()
    );

    return expected == response;
}

}  // namespace syncflow::security

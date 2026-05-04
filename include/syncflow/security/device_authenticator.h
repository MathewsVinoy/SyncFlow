#pragma once

#include <string>
#include <memory>

namespace syncflow::security {

class DeviceAuthenticator {
public:
    explicit DeviceAuthenticator(const std::string& local_cert_fingerprint);

    // Authenticate a peer using their certificate fingerprint and trusted device list
    bool authenticate_peer(const std::string& peer_name, 
                          const std::string& peer_fingerprint,
                          const std::string& peer_ip,
                          bool is_trusted) const;

    // Generate a challenge-response token for authentication
    std::string generate_auth_challenge();

    // Verify authentication response
    bool verify_auth_response(const std::string& challenge, const std::string& response) const;

    // Get local device fingerprint
    std::string get_local_fingerprint() const { return local_fingerprint_; }

private:
    std::string local_fingerprint_;
};

}  // namespace syncflow::security

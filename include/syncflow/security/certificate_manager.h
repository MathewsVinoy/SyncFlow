#pragma once

#include <filesystem>
#include <string>
#include <memory>

namespace syncflow::security {

class CertificateManager {
public:
    explicit CertificateManager(const std::filesystem::path& cert_dir);

    // Generate self-signed certificate and key for this device
    bool generate_self_signed_cert(const std::string& device_name);

    // Load certificate and key from disk
    bool load_certificate();

    // Get paths to cert and key files
    std::filesystem::path get_cert_path() const { return cert_path_; }
    std::filesystem::path get_key_path() const { return key_path_; }

    // Get certificate fingerprint (SHA256) for device identification
    std::string get_cert_fingerprint() const { return cert_fingerprint_; }

    // Check if certificate exists
    bool has_certificate() const;

    // Verify a peer certificate (basic validation)
    bool verify_peer_cert(const std::string& peer_cert_pem) const;

private:
    std::filesystem::path cert_dir_;
    std::filesystem::path cert_path_;
    std::filesystem::path key_path_;
    std::string cert_fingerprint_;

    std::string compute_cert_fingerprint(const std::filesystem::path& cert_path);
};

}  // namespace syncflow::security

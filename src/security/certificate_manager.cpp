#include "syncflow/security/certificate_manager.h"
#include "syncflow/security/integrity_checker.h"

#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <fstream>
#include <ctime>
#include <filesystem>
#include <stdexcept>

namespace syncflow::security {

CertificateManager::CertificateManager(const std::filesystem::path& cert_dir)
    : cert_dir_(cert_dir),
      cert_path_(cert_dir / "device.crt"),
      key_path_(cert_dir / "device.key") {
    std::error_code ec;
    std::filesystem::create_directories(cert_dir, ec);
    load_certificate();
}

bool CertificateManager::has_certificate() const {
    return std::filesystem::exists(cert_path_) && std::filesystem::exists(key_path_);
}

bool CertificateManager::load_certificate() {
    if (!has_certificate()) return false;

    cert_fingerprint_ = compute_cert_fingerprint(cert_path_);
    return !cert_fingerprint_.empty();
}

std::string CertificateManager::compute_cert_fingerprint(const std::filesystem::path& cert_path) {
    std::ifstream cert_file(cert_path, std::ios::binary);
    if (!cert_file) return {};

    std::string cert_pem((std::istreambuf_iterator<char>(cert_file)), std::istreambuf_iterator<char>());
    
    return IntegrityChecker::compute_sha256(cert_pem.data(), cert_pem.size());
}

bool CertificateManager::generate_self_signed_cert(const std::string& device_name) {
    // Check if certificate already exists
    if (has_certificate()) {
        return load_certificate();
    }

    // Generate a 2048-bit RSA key
    EVP_PKEY* pkey = EVP_PKEY_new();
    RSA* rsa = RSA_new();
    BIGNUM* exponent = BN_new();

    if (!pkey || !rsa || !exponent) {
        EVP_PKEY_free(pkey);
        RSA_free(rsa);
        BN_free(exponent);
        return false;
    }

    BN_set_word(exponent, RSA_F4);
    int key_bits = 2048;
    if (!RSA_generate_key_ex(rsa, key_bits, exponent, nullptr)) {
        EVP_PKEY_free(pkey);
        RSA_free(rsa);
        BN_free(exponent);
        return false;
    }

    BN_free(exponent);
    EVP_PKEY_assign_RSA(pkey, rsa);

    // Create X509 certificate
    X509* cert = X509_new();
    if (!cert) {
        EVP_PKEY_free(pkey);
        return false;
    }

    // Set serial number
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 1);

    // Set validity period: 365 days
    X509_gmtime_adj(X509_get_notBefore(cert), 0);
    X509_gmtime_adj(X509_get_notAfter(cert), 365 * 24 * 3600);

    // Set public key
    X509_set_pubkey(cert, pkey);

    // Set certificate subject (CN = device name)
    X509_NAME* name = X509_get_subject_name(cert);
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASN1, 
                               reinterpret_cast<const unsigned char*>(device_name.c_str()), 
                               -1, -1, 0);

    // Set issuer same as subject (self-signed)
    X509_set_issuer_name(cert, name);

    // Sign certificate with private key
    if (!X509_sign(cert, pkey, EVP_sha256())) {
        X509_free(cert);
        EVP_PKEY_free(pkey);
        return false;
    }

    // Write private key to file
    FILE* key_file = ::fopen(key_path_.c_str(), "wb");
    if (!key_file) {
        X509_free(cert);
        EVP_PKEY_free(pkey);
        return false;
    }

    if (!PEM_write_PrivateKey(key_file, pkey, nullptr, nullptr, 0, nullptr, nullptr)) {
        ::fclose(key_file);
        X509_free(cert);
        EVP_PKEY_free(pkey);
        return false;
    }
    ::fclose(key_file);

    // Write certificate to file
    FILE* cert_file = ::fopen(cert_path_.c_str(), "wb");
    if (!cert_file) {
        X509_free(cert);
        EVP_PKEY_free(pkey);
        return false;
    }

    if (!PEM_write_X509(cert_file, cert)) {
        ::fclose(cert_file);
        X509_free(cert);
        EVP_PKEY_free(pkey);
        return false;
    }
    ::fclose(cert_file);

    X509_free(cert);
    EVP_PKEY_free(pkey);

    // Load and compute fingerprint
    return load_certificate();
}

bool CertificateManager::verify_peer_cert(const std::string& peer_cert_pem) const {
    if (peer_cert_pem.empty()) return false;

    BIO* bio = BIO_new_mem_buf(peer_cert_pem.c_str(), -1);
    if (!bio) return false;

    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);

    if (!cert) return false;

    // Basic validation: check if certificate has expired
    bool is_valid = X509_cmp_current_time(X509_get_notAfter(cert)) > 0;

    X509_free(cert);
    return is_valid;
}

}  // namespace syncflow::security

#pragma once

#include <string>
#include <vector>
#include <filesystem>

namespace syncflow::security {

struct TrustedDevice {
    std::string device_name;
    std::string cert_fingerprint;
    std::string ip_address;
    long long first_seen_time;  // Unix timestamp
    bool is_approved{false};     // User-approved trusted device
};

class TrustedDeviceList {
public:
    explicit TrustedDeviceList(const std::filesystem::path& list_file);

    // Add or update a trusted device
    bool add_device(const std::string& device_name, 
                   const std::string& cert_fingerprint,
                   const std::string& ip_address);

    // Check if device is in trusted list
    bool is_trusted(const std::string& cert_fingerprint) const;

    // Approve a device (user explicitly trusts it)
    bool approve_device(const std::string& cert_fingerprint);

    // Get device info by fingerprint
    const TrustedDevice* find_device(const std::string& cert_fingerprint) const;

    // Load trusted devices from file
    bool load_from_file();

    // Save trusted devices to file
    bool save_to_file() const;

    // Get all devices
    const std::vector<TrustedDevice>& get_all_devices() const { return devices_; }

    // Remove a device from trusted list
    bool remove_device(const std::string& cert_fingerprint);

private:
    std::filesystem::path list_file_;
    std::vector<TrustedDevice> devices_;
};

}  // namespace syncflow::security

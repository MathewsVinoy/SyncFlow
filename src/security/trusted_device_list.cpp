#include "syncflow/security/trusted_device_list.h"
#include "syncflow/security/integrity_checker.h"

#include <fstream>
#include <sstream>
#include <chrono>
#include <algorithm>

namespace syncflow::security {

TrustedDeviceList::TrustedDeviceList(const std::filesystem::path& list_file)
    : list_file_(list_file) {
    load_from_file();
}

bool TrustedDeviceList::load_from_file() {
    if (!std::filesystem::exists(list_file_)) return true;

    std::ifstream file(list_file_);
    if (!file) return false;

    devices_.clear();
    std::string line;

    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;

        // Format: device_name|fingerprint|ip_address|timestamp|approved
        std::istringstream iss(line);
        std::string device_name, fingerprint, ip, timestamp_str, approved_str;

        if (std::getline(iss, device_name, '|') &&
            std::getline(iss, fingerprint, '|') &&
            std::getline(iss, ip, '|') &&
            std::getline(iss, timestamp_str, '|') &&
            std::getline(iss, approved_str, '|')) {

            TrustedDevice dev;
            dev.device_name = device_name;
            dev.cert_fingerprint = fingerprint;
            dev.ip_address = ip;
            dev.first_seen_time = std::stoll(timestamp_str);
            dev.is_approved = (approved_str == "1");

            devices_.push_back(dev);
        }
    }

    return true;
}

bool TrustedDeviceList::save_to_file() const {
    std::ofstream file(list_file_);
    if (!file) return false;

    file << "# Trusted Devices List\n";
    file << "# Format: device_name|fingerprint|ip_address|timestamp|approved\n\n";

    for (const auto& dev : devices_) {
        file << dev.device_name << '|'
             << dev.cert_fingerprint << '|'
             << dev.ip_address << '|'
             << dev.first_seen_time << '|'
             << (dev.is_approved ? "1" : "0") << '\n';
    }

    return true;
}

bool TrustedDeviceList::add_device(const std::string& device_name,
                                   const std::string& cert_fingerprint,
                                   const std::string& ip_address) {
    // Check if device already exists
    auto it = std::find_if(devices_.begin(), devices_.end(),
                          [&](const TrustedDevice& d) { 
                              return d.cert_fingerprint == cert_fingerprint; 
                          });

    if (it != devices_.end()) {
        it->ip_address = ip_address;  // Update IP if changed
        it->device_name = device_name;
        return save_to_file();
    }

    // Add new device
    TrustedDevice dev;
    dev.device_name = device_name;
    dev.cert_fingerprint = cert_fingerprint;
    dev.ip_address = ip_address;
    dev.first_seen_time = std::chrono::system_clock::now().time_since_epoch().count() / 1000000000LL;
    dev.is_approved = false;

    devices_.push_back(dev);
    return save_to_file();
}

bool TrustedDeviceList::is_trusted(const std::string& cert_fingerprint) const {
    auto it = std::find_if(devices_.begin(), devices_.end(),
                          [&](const TrustedDevice& d) { 
                              return d.cert_fingerprint == cert_fingerprint && d.is_approved; 
                          });
    return it != devices_.end();
}

bool TrustedDeviceList::approve_device(const std::string& cert_fingerprint) {
    auto it = std::find_if(devices_.begin(), devices_.end(),
                          [&](const TrustedDevice& d) { 
                              return d.cert_fingerprint == cert_fingerprint; 
                          });

    if (it != devices_.end()) {
        it->is_approved = true;
        return save_to_file();
    }

    return false;
}

const TrustedDevice* TrustedDeviceList::find_device(const std::string& cert_fingerprint) const {
    auto it = std::find_if(devices_.begin(), devices_.end(),
                          [&](const TrustedDevice& d) { 
                              return d.cert_fingerprint == cert_fingerprint; 
                          });

    if (it != devices_.end()) {
        return &(*it);
    }

    return nullptr;
}

bool TrustedDeviceList::remove_device(const std::string& cert_fingerprint) {
    auto it = std::find_if(devices_.begin(), devices_.end(),
                          [&](const TrustedDevice& d) { 
                              return d.cert_fingerprint == cert_fingerprint; 
                          });

    if (it != devices_.end()) {
        devices_.erase(it);
        return save_to_file();
    }

    return false;
}

}  // namespace syncflow::security

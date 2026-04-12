// src/discovery/device.cpp

#include <syncflow/discovery/discovery.h>

namespace syncflow::discovery {

Device::Device(const DeviceInfo& info)
    : info_(info), last_seen_(std::chrono::system_clock::now()) {}

const DeviceInfo& Device::get_info() const {
    return info_;
}

void Device::update_info(const DeviceInfo& info) {
    info_ = info;
    mark_seen();
}

void Device::mark_seen() {
    last_seen_ = std::chrono::system_clock::now();
}

bool Device::is_alive(int timeout_ms) const {
    auto now = std::chrono::system_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_seen_);
    return duration.count() < timeout_ms;
}

} // namespace syncflow::discovery

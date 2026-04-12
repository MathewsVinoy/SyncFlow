// src/discovery/device_manager.cpp

#include <syncflow/discovery/discovery.h>

namespace syncflow::discovery {

DeviceManager& DeviceManager::instance() {
    static DeviceManager manager;
    return manager;
}

bool DeviceManager::add_device(const DeviceInfo& info) {
    std::unique_lock<std::mutex> lock(devices_mutex_);
    
    auto it = devices_.find(info.id);
    if (it != devices_.end()) {
        it->second->update_info(info);
        return false;  // Device already existed
    }
    
    devices_[info.id] = std::make_shared<Device>(info);
    return true;  // New device added
}

bool DeviceManager::remove_device(const DeviceID& id) {
    std::unique_lock<std::mutex> lock(devices_mutex_);
    return devices_.erase(id) > 0;
}

std::shared_ptr<Device> DeviceManager::get_device(const DeviceID& id) {
    std::unique_lock<std::mutex> lock(devices_mutex_);
    auto it = devices_.find(id);
    return it != devices_.end() ? it->second : nullptr;
}

std::vector<std::shared_ptr<Device>> DeviceManager::get_all_devices() {
    std::unique_lock<std::mutex> lock(devices_mutex_);
    std::vector<std::shared_ptr<Device>> result;
    for (const auto& pair : devices_) {
        result.push_back(pair.second);
    }
    return result;
}

size_t DeviceManager::device_count() const {
    std::unique_lock<std::mutex> lock(devices_mutex_);
    return devices_.size();
}

void DeviceManager::cleanup_stale_devices(int timeout_ms) {
    std::unique_lock<std::mutex> lock(devices_mutex_);
    
    auto it = devices_.begin();
    while (it != devices_.end()) {
        if (!it->second->is_alive(timeout_ms)) {
            it = devices_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace syncflow::discovery

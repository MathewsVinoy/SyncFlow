#ifndef SYNCFLOW_DISCOVERY_DISCOVERY_H
#define SYNCFLOW_DISCOVERY_DISCOVERY_H

#include <syncflow/types.h>

#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace syncflow::discovery {

class Device {
public:
    Device(const DeviceInfo& info);

    const DeviceInfo& get_info() const;
    void update_info(const DeviceInfo& info);
    void mark_seen();
    bool is_alive(int timeout_ms) const;

private:
    DeviceInfo info_;
    std::chrono::system_clock::time_point last_seen_;
};

class DiscoveryEngine {
public:
    DiscoveryEngine();
    ~DiscoveryEngine();

    bool start(OnDeviceDiscovered on_discovered, OnDeviceLost on_lost);
    bool stop();
    bool is_running() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

class DeviceManager {
public:
    static DeviceManager& instance();

    bool add_device(const DeviceInfo& info);
    bool remove_device(const DeviceID& id);
    std::shared_ptr<Device> get_device(const DeviceID& id);
    std::vector<std::shared_ptr<Device>> get_all_devices();
    size_t device_count() const;

    void cleanup_stale_devices(int timeout_ms);

private:
    DeviceManager() = default;
    DeviceManager(const DeviceManager&) = delete;
    DeviceManager& operator=(const DeviceManager&) = delete;

    std::map<DeviceID, std::shared_ptr<Device>> devices_;
    mutable std::mutex devices_mutex_;
};

} // namespace syncflow::discovery

#endif // SYNCFLOW_DISCOVERY_DISCOVERY_H

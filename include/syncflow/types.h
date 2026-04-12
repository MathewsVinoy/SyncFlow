#ifndef SYNCFLOW_TYPES_H
#define SYNCFLOW_TYPES_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace syncflow {

using DeviceID = std::string;

enum class PlatformType : std::uint8_t {
    UNKNOWN = 0,
    WINDOWS = 1,
    LINUX = 2,
    MACOS = 3,
};

struct DeviceInfo {
    DeviceID id;
    std::string name;
    std::string hostname;
    std::string ip_address;
    std::uint16_t port = 0;
    PlatformType platform = PlatformType::UNKNOWN;
    std::string version;
    std::chrono::system_clock::time_point last_seen{};
};

using OnDeviceDiscovered = std::function<void(const DeviceInfo&)>;
using OnDeviceLost = std::function<void(const DeviceID&)>;

constexpr std::uint32_t HANDSHAKE_MAGIC = 0x53465831U;   // SFX1
constexpr std::uint32_t PROTOCOL_VERSION = 1U;
constexpr std::uint16_t DISCOVERY_PORT = 37020;
constexpr std::uint16_t TRANSFER_PORT = 37030;
constexpr int DISCOVERY_INTERVAL_MS = 2000;
constexpr int DISCOVERY_TIMEOUT_MS = 5000;

} // namespace syncflow

#endif // SYNCFLOW_TYPES_H

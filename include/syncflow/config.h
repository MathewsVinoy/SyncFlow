#pragma once

#include <chrono>
#include <cstdint>

namespace syncflow::config {

inline constexpr std::uint16_t kUdpDiscoveryPort = 45454;
inline constexpr std::uint16_t kTcpPort = 45455;
inline constexpr const char* kBroadcastAddress = "255.255.255.255";
inline constexpr const char* kMagic = "SYNCFLOW_PEER";
inline constexpr std::chrono::seconds kDiscoveryInterval{2};

}  // namespace syncflow::config

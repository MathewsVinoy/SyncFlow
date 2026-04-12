// src/platform/android/network_config.cpp
// Android network configuration

#include <syncflow/platform/platform.h>
#include <syncflow/common/logger.h>

#ifdef __ANDROID__
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <ifaddrs.h>
    #include <net/if.h>
    #include <string.h>

namespace syncflow::platform {

// Android network initialization
// In Termux, we use standard POSIX socket APIs (same as Linux)

class AndroidNetworkInit {
public:
    static bool initialize() {
        LOG_INFO("AndroidNetwork", "Network initialization - Termux uses standard socket APIs");
        // No special initialization needed for Termux/Android
        // Uses standard POSIX socket APIs
        return true;
    }
    
    static void cleanup() {
        LOG_INFO("AndroidNetwork", "Network cleanup complete");
    }
};

// Get primary network interface on Android
bool get_primary_interface(std::string& interface_name) {
    struct ifaddrs* ifaddr = nullptr;
    struct ifaddrs* ifa = nullptr;
    
    if (getifaddrs(&ifaddr) == -1) {
        LOG_ERROR("AndroidNetwork", "Failed to get interface addresses");
        return false;
    }
    
    // Prefer eth0, wlan0, or any other active interface
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr) continue;
        
        // Skip loopback
        if (ifa->ifa_flags & IFF_LOOPBACK) continue;
        
        // Prefer active interfaces
        if ((ifa->ifa_flags & IFF_UP) && (ifa->ifa_flags & IFF_RUNNING)) {
            if (ifa->ifa_addr->sa_family == AF_INET) {
                interface_name = ifa->ifa_name;
                freeifaddrs(ifaddr);
                return true;
            }
        }
    }
    
    // Fallback: return any interface
    for (ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr && ifa->ifa_addr->sa_family == AF_INET) {
            if (!(ifa->ifa_flags & IFF_LOOPBACK)) {
                interface_name = ifa->ifa_name;
                freeifaddrs(ifaddr);
                return true;
            }
        }
    }
    
    freeifaddrs(ifaddr);
    return false;
}

// Get MAC address from Android interface
bool get_mac_from_interface(const std::string& interface, std::string& mac) {
    // In Termux, try to read from /sys/class/net/<iface>/address
    std::string path = "/sys/class/net/" + interface + "/address";
    
    std::ifstream file(path);
    if (file.is_open()) {
        std::getline(file, mac);
        file.close();
        
        // Remove trailing whitespace
        if (!mac.empty() && mac.back() == '\n') {
            mac.pop_back();
        }
        
        LOG_INFO("AndroidNetwork", "MAC address from " + interface + ": " + mac);
        return !mac.empty();
    }
    
    LOG_WARN("AndroidNetwork", "Could not read MAC from " + path);
    return false;
}

} // namespace syncflow::platform

#endif // __ANDROID__

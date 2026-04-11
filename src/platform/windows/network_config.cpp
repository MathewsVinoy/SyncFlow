// src/platform/windows/network_config.cpp
// Windows-specific network configuration and utilities

#ifdef _WIN32

#include <winsock2.h>
#include <iphlpapi.h>
#include <ws2tcpip.h>
#include <stdio.h>
#include <stdlib.h>
#include <syncflow/common/logger.h>

#pragma comment(lib, "winsock2.lib")
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")

namespace syncflow::platform {

// Initialize Windows Socket library
class WindowsSocketInit {
public:
    static bool initialize() {
        WSADATA wsa_data;
        int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        if (result != 0) {
            LOG_ERROR("WindowsSocket", "WSAStartup failed with code: " + std::to_string(result));
            return false;
        }
        LOG_INFO("WindowsSocket", "Winsock initialized: " + 
                std::string(wsa_data.szDescription));
        return true;
    }
    
    static void cleanup() {
        WSACleanup();
        LOG_INFO("WindowsSocket", "Winsock cleaned up");
    }
};

// Get local network interface information
bool get_network_interfaces(std::vector<std::string>& addresses) {
    ULONG out_buf_len = 0;
    IP_ADAPTER_INFO* adapter_info = nullptr;
    
    // Get size of adapter info
    if (GetAdaptersInfo(nullptr, &out_buf_len) == ERROR_BUFFER_OVERFLOW) {
        adapter_info = (IP_ADAPTER_INFO*)malloc(out_buf_len);
        if (!adapter_info) {
            LOG_ERROR("Network", "Unable to allocate memory");
            return false;
        }
    }
    
    if (GetAdaptersInfo(adapter_info, &out_buf_len) == NO_ERROR) {
        IP_ADAPTER_INFO* current_adapter = adapter_info;
        
        while (current_adapter) {
            if (current_adapter->Type == MIB_IF_TYPE_ETHERNET || 
                current_adapter->Type == IF_TYPE_IEEE80211) {  // WiFi
                
                IP_ADDR_STRING* ip_addr = &current_adapter->IpAddressList;
                while (ip_addr) {
                    std::string addr(ip_addr->IpAddress.String);
                    if (addr != "0.0.0.0" && addr != "127.0.0.1") {
                        addresses.push_back(addr);
                        LOG_DEBUG("Network", "Found interface: " + addr);
                    }
                    ip_addr = ip_addr->Next;
                }
            }
            current_adapter = current_adapter->Next;
        }
    } else {
        LOG_ERROR("Network", "GetAdaptersInfo failed");
        free(adapter_info);
        return false;
    }
    
    if (adapter_info) {
        free(adapter_info);
    }
    
    return !addresses.empty();
}

// Enable socket features for Syncflow
bool configure_socket(SOCKET sock) {
    // Set socket to non-blocking
    u_long mode = 1;
    if (ioctlsocket(sock, FIONBIO, &mode) == SOCKET_ERROR) {
        LOG_ERROR("Network", "Failed to set non-blocking mode");
        return false;
    }
    
    // Enable reuse address
    BOOL reuse = TRUE;
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) == SOCKET_ERROR) {
        LOG_ERROR("Network", "Failed to set SO_REUSEADDR");
        return false;
    }
    
    // Set receive timeout
    DWORD timeout = 5000;  // 5 seconds
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) == SOCKET_ERROR) {
        LOG_ERROR("Network", "Failed to set receive timeout");
        return false;
    }
    
    // Set send timeout
    if (setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&timeout, sizeof(timeout)) == SOCKET_ERROR) {
        LOG_ERROR("Network", "Failed to set send timeout");
        return false;
    }
    
    return true;
}

// Enable broadcast on UDP socket
bool enable_broadcast(SOCKET sock) {
    BOOL broadcast = TRUE;
    if (setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char*)&broadcast, sizeof(broadcast)) == SOCKET_ERROR) {
        LOG_ERROR("Network", "Failed to enable broadcast");
        return false;
    }
    return true;
}

// Static initialization wrapper
static bool winsock_initialized = WindowsSocketInit::initialize();

} // namespace syncflow::platform

#endif // _WIN32

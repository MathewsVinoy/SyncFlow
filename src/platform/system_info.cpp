#include "syncflow/platform/system_info.h"

#include <chrono>
#include <ctime>
#include <cstring>
#include <filesystem>

// Platform detection
#if defined(_WIN32) || defined(_WIN64) || defined(__WINDOWS__)
    #define PLATFORM_WINDOWS 1
    #include <winsock2.h>
    #include <iphlpapi.h>
    #include <wchar.h>
    #pragma comment(lib, "ws2_32.lib")
    #pragma comment(lib, "iphlpapi.lib")
#elif defined(__APPLE__) && defined(__MACH__)
    #define PLATFORM_MACOS 1
    #include <arpa/inet.h>
    #include <ifaddrs.h>
    #include <net/if.h>
    #include <signal.h>
    #include <unistd.h>
    #include <sys/socket.h>
#else
    #define PLATFORM_LINUX 1
    #include <arpa/inet.h>
    #include <ifaddrs.h>
    #include <net/if.h>
    #include <signal.h>
    #include <unistd.h>
    #include <sys/socket.h>
#endif

namespace syncflow::platform {

namespace {
std::atomic_bool* g_running = nullptr;

#if !defined(PLATFORM_WINDOWS)
void on_signal(int) {
    if (g_running != nullptr) {
        g_running->store(false);
    }
}
#else
BOOL WINAPI console_ctrl_handler(DWORD ctrl_type) {
    if (ctrl_type == CTRL_C_EVENT || ctrl_type == CTRL_BREAK_EVENT) {
        if (g_running != nullptr) {
            g_running->store(false);
        }
        return TRUE;
    }
    return FALSE;
}
#endif
}  // namespace

// Platform detection functions
bool is_windows() {
#ifdef PLATFORM_WINDOWS
    return true;
#else
    return false;
#endif
}

bool is_macos() {
#ifdef PLATFORM_MACOS
    return true;
#else
    return false;
#endif
}

bool is_linux() {
#ifdef PLATFORM_LINUX
    return true;
#else
    return false;
#endif
}

std::string get_hostname() {
#ifdef PLATFORM_WINDOWS
    char buffer[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD size = sizeof(buffer);
    if (::GetComputerNameA(buffer, &size)) {
        return buffer;
    }
#else
    char buffer[256] = {};
    if (::gethostname(buffer, sizeof(buffer)) == 0 && buffer[0] != '\0') {
        return buffer;
    }
#endif
    return "unknown-device";
}

std::filesystem::path get_home_dir() {
#ifdef PLATFORM_WINDOWS
    const char* home = std::getenv("USERPROFILE");
    if (home) return home;
#else
    const char* home = std::getenv("HOME");
    if (home) return home;
#endif
    return std::filesystem::current_path();
}

std::filesystem::path get_config_dir() {
#ifdef PLATFORM_WINDOWS
    const char* appdata = std::getenv("APPDATA");
    if (appdata) {
        return std::filesystem::path(appdata) / "syncflow";
    }
    return get_home_dir() / "AppData" / "Roaming" / "syncflow";
#elif defined(PLATFORM_MACOS)
    return get_home_dir() / "Library" / "Application Support" / "syncflow";
#else  // Linux
    const char* config_home = std::getenv("XDG_CONFIG_HOME");
    if (config_home) {
        return std::filesystem::path(config_home) / "syncflow";
    }
    return get_home_dir() / ".config" / "syncflow";
#endif
}

std::filesystem::path get_cache_dir() {
#ifdef PLATFORM_WINDOWS
    const char* localappdata = std::getenv("LOCALAPPDATA");
    if (localappdata) {
        return std::filesystem::path(localappdata) / "syncflow";
    }
    return get_home_dir() / "AppData" / "Local" / "syncflow";
#elif defined(PLATFORM_MACOS)
    return get_home_dir() / "Library" / "Caches" / "syncflow";
#else  // Linux
    const char* cache_home = std::getenv("XDG_CACHE_HOME");
    if (cache_home) {
        return std::filesystem::path(cache_home) / "syncflow";
    }
    return get_home_dir() / ".cache" / "syncflow";
#endif
}

std::string get_local_ipv4() {
#ifdef PLATFORM_WINDOWS
    // Windows implementation using GetAdaptersInfo
    PIP_ADAPTER_INFO adapter_info = nullptr;
    PIP_ADAPTER_INFO adapter = nullptr;
    DWORD size = sizeof(IP_ADAPTER_INFO);
    
    adapter_info = (IP_ADAPTER_INFO*)malloc(size);
    if (adapter_info == nullptr) return "0.0.0.0";
    
    if (GetAdaptersInfo(adapter_info, &size) == ERROR_BUFFER_OVERFLOW) {
        free(adapter_info);
        adapter_info = (IP_ADAPTER_INFO*)malloc(size);
        if (adapter_info == nullptr) return "0.0.0.0";
    }
    
    std::string result = "0.0.0.0";
    if (GetAdaptersInfo(adapter_info, &size) == NO_ERROR) {
        for (adapter = adapter_info; adapter; adapter = adapter->Next) {
            // Skip loopback
            if (adapter->Type == IF_TYPE_SOFTWARE_LOOPBACK) continue;
            
            std::string ip = adapter->IpAddressList.IpAddress.String;
            if (!ip.empty() && ip != "0.0.0.0") {
                result = ip;
                break;
            }
        }
    }
    
    if (adapter_info) free(adapter_info);
    return result;
#else
    // Unix/Linux/macOS implementation using getifaddrs
    ifaddrs* ifaddr = nullptr;
    if (::getifaddrs(&ifaddr) != 0) {
        return "0.0.0.0";
    }

    std::string result = "0.0.0.0";
    for (auto* current = ifaddr; current != nullptr; current = current->ifa_next) {
        if (current->ifa_addr == nullptr || current->ifa_addr->sa_family != AF_INET) {
            continue;
        }

        if ((current->ifa_flags & IFF_LOOPBACK) != 0) {
            continue;
        }

        auto* addr = reinterpret_cast<sockaddr_in*>(current->ifa_addr);
        char ip[INET_ADDRSTRLEN] = {};
        if (::inet_ntop(AF_INET, &addr->sin_addr, ip, sizeof(ip)) != nullptr) {
            result = ip;
            break;
        }
    }

    ::freeifaddrs(ifaddr);
    return result;
#endif
}

std::string timestamp_now() {
    const auto now = std::chrono::system_clock::now();
    const auto tt = std::chrono::system_clock::to_time_t(now);
    
#ifdef PLATFORM_WINDOWS
    std::tm tm{};
    localtime_s(&tm, &tt);
#else
    std::tm tm{};
    localtime_r(&tt, &tm);
#endif

    char buffer[64] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
    return buffer;
}

void install_signal_handlers(std::atomic_bool& running) {
    g_running = &running;
#ifdef PLATFORM_WINDOWS
    ::SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
#else
    ::signal(SIGINT, on_signal);
    ::signal(SIGTERM, on_signal);
#endif
}

}  // namespace syncflow::platform

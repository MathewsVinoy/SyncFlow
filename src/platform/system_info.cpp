#include "syncflow/platform/system_info.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <signal.h>
#include <unistd.h>

#include <chrono>
#include <ctime>
#include <cstring>

namespace syncflow::platform {

namespace {

std::atomic_bool* g_running = nullptr;

void on_signal(int) {
    if (g_running != nullptr) {
        g_running->store(false);
    }
}

}  // namespace

std::string get_hostname() {
    char buffer[256] = {};
    if (::gethostname(buffer, sizeof(buffer)) == 0 && buffer[0] != '\0') {
        return buffer;
    }
    return "unknown-device";
}

std::string get_local_ipv4() {
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
}

std::string timestamp_now() {
    const auto now = std::chrono::system_clock::now();
    const auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&tt, &tm);

    char buffer[64] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
    return buffer;
}

void install_signal_handlers(std::atomic_bool& running) {
    g_running = &running;
    ::signal(SIGINT, on_signal);
    ::signal(SIGTERM, on_signal);
}

}  // namespace syncflow::platform

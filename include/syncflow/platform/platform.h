#ifndef SYNCFLOW_PLATFORM_PLATFORM_H
#define SYNCFLOW_PLATFORM_PLATFORM_H

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <string>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace syncflow::platform {

enum class AddressFamily {
    IPv4,
    IPv6,
};

struct SocketAddress {
    std::string ip;
    std::uint16_t port = 0;
    AddressFamily family = AddressFamily::IPv4;
};

struct PlatformInfo {
    std::string name;
    std::string version;
};

inline PlatformInfo get_platform_info() {
#ifdef _WIN32
    return {"windows", "1"};
#elif __APPLE__
    return {"macos", "1"};
#else
    return {"linux", "1"};
#endif
}

#ifdef _WIN32
using socket_handle_t = SOCKET;
constexpr socket_handle_t kInvalidSocket = INVALID_SOCKET;
#else
using socket_handle_t = int;
constexpr socket_handle_t kInvalidSocket = -1;
#endif

class Socket {
public:
    explicit Socket(socket_handle_t handle) : handle_(handle) {}
    ~Socket() { close(); }

    bool set_reuse_address(bool enabled) {
        int value = enabled ? 1 : 0;
#ifdef _WIN32
        return setsockopt(handle_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&value), sizeof(value)) == 0;
#else
        return setsockopt(handle_, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value)) == 0;
#endif
    }

    bool set_broadcast(bool enabled) {
        int value = enabled ? 1 : 0;
#ifdef _WIN32
        return setsockopt(handle_, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&value), sizeof(value)) == 0;
#else
        return setsockopt(handle_, SOL_SOCKET, SO_BROADCAST, &value, sizeof(value)) == 0;
#endif
    }

    bool set_receive_timeout(int timeout_ms) {
#ifdef _WIN32
        DWORD value = static_cast<DWORD>(timeout_ms);
        return setsockopt(handle_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&value), sizeof(value)) == 0;
#else
        timeval tv{};
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        return setsockopt(handle_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) == 0;
#endif
    }

    bool bind(const SocketAddress& addr) {
        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(addr.port);
        if (addr.ip.empty() || addr.ip == "0.0.0.0") {
            sa.sin_addr.s_addr = htonl(INADDR_ANY);
        } else {
            if (inet_pton(AF_INET, addr.ip.c_str(), &sa.sin_addr) != 1) {
                return false;
            }
        }
        return ::bind(handle_, reinterpret_cast<const sockaddr*>(&sa), sizeof(sa)) == 0;
    }

    size_t receive_from(std::uint8_t* buffer, size_t buffer_size, SocketAddress& from) {
        sockaddr_in sa{};
#ifdef _WIN32
        int len = sizeof(sa);
#else
        socklen_t len = sizeof(sa);
#endif
        const int received = recvfrom(handle_, reinterpret_cast<char*>(buffer), static_cast<int>(buffer_size), 0,
                                      reinterpret_cast<sockaddr*>(&sa), &len);
        if (received <= 0) {
#ifdef _WIN32
            const int err = WSAGetLastError();
            if (err == WSAETIMEDOUT || err == WSAEWOULDBLOCK) {
                return 0;
            }
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return 0;
            }
#endif
            return 0;
        }

        char ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &sa.sin_addr, ip, sizeof(ip));
        from.ip = ip;
        from.port = ntohs(sa.sin_port);
        from.family = AddressFamily::IPv4;
        return static_cast<size_t>(received);
    }

    size_t send_to(const std::uint8_t* data, size_t size, const SocketAddress& to) {
        sockaddr_in sa{};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(to.port);
        if (inet_pton(AF_INET, to.ip.c_str(), &sa.sin_addr) != 1) {
            return 0;
        }
        const int sent = sendto(handle_, reinterpret_cast<const char*>(data), static_cast<int>(size), 0,
                                reinterpret_cast<const sockaddr*>(&sa), sizeof(sa));
        return sent > 0 ? static_cast<size_t>(sent) : 0;
    }

    void close() {
        if (handle_ == kInvalidSocket) {
            return;
        }
#ifdef _WIN32
        closesocket(handle_);
#else
        ::close(handle_);
#endif
        handle_ = kInvalidSocket;
    }

    socket_handle_t native_handle() const { return handle_; }

private:
    socket_handle_t handle_ = kInvalidSocket;
};

class Network {
public:
    static std::shared_ptr<Network> create() {
#ifdef _WIN32
        static bool wsa_ready = [] {
            WSADATA wsa_data{};
            return WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
        }();
        (void)wsa_ready;
#endif
        return std::make_shared<Network>();
    }

    std::unique_ptr<Socket> create_udp_socket() {
        const socket_handle_t handle = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (handle == kInvalidSocket) {
            return nullptr;
        }
        return std::make_unique<Socket>(handle);
    }

    bool get_hostname(std::string& out) {
        char buffer[256] = {0};
#ifdef _WIN32
        if (::gethostname(buffer, static_cast<int>(sizeof(buffer))) != 0) {
            return false;
        }
#else
        if (::gethostname(buffer, sizeof(buffer)) != 0) {
            return false;
        }
#endif
        out = buffer;
        return true;
    }

    bool get_local_ip(AddressFamily, std::string& out) {
        socket_handle_t s = ::socket(AF_INET, SOCK_DGRAM, 0);
        if (s == kInvalidSocket) {
            return false;
        }

        sockaddr_in target{};
        target.sin_family = AF_INET;
        target.sin_port = htons(80);
        inet_pton(AF_INET, "8.8.8.8", &target.sin_addr);
        ::connect(s, reinterpret_cast<const sockaddr*>(&target), sizeof(target));

        sockaddr_in local{};
#ifdef _WIN32
        int len = sizeof(local);
#else
        socklen_t len = sizeof(local);
#endif
        bool ok = ::getsockname(s, reinterpret_cast<sockaddr*>(&local), &len) == 0;
        char buffer[INET_ADDRSTRLEN] = {0};
        if (ok && inet_ntop(AF_INET, &local.sin_addr, buffer, sizeof(buffer))) {
            out = buffer;
        } else {
            out = "127.0.0.1";
            ok = false;
        }
#ifdef _WIN32
        closesocket(s);
#else
        ::close(s);
#endif
        return ok || !out.empty();
    }

    bool get_mac_address(std::string& out) {
        std::string host;
        if (!get_hostname(host)) {
            out = "00:00:00:00:00:00";
            return false;
        }
        std::size_t h = std::hash<std::string>{}(host);
        char buf[18] = {0};
        std::snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
                      static_cast<unsigned>((h >> 0) & 0xFF),
                      static_cast<unsigned>((h >> 8) & 0xFF),
                      static_cast<unsigned>((h >> 16) & 0xFF),
                      static_cast<unsigned>((h >> 24) & 0xFF),
                      static_cast<unsigned>((h >> 32) & 0xFF),
                      static_cast<unsigned>((h >> 40) & 0xFF));
        out = buf;
        return true;
    }
};

} // namespace syncflow::platform

#endif // SYNCFLOW_PLATFORM_PLATFORM_H

// src/platform/networking.cpp

#include <syncflow/platform/platform.h>
#include <syncflow/common/logger.h>
#include <memory>

#ifdef _WIN32
    #include <winsock2.h>
    #include <iphlpapi.h>
    #pragma comment(lib, "ws2_32.lib")
    #pragma comment(lib, "iphlpapi.lib")
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <netdb.h>
    #include <fcntl.h>
    #include <unistd.h>
    #include <ifaddrs.h>
    typedef int SOCKET;
    constexpr SOCKET INVALID_SOCKET = -1;
    constexpr int SOCKET_ERROR = -1;
    inline int closesocket(int s) { return close(s); }
#endif

namespace syncflow::platform {

class SocketImpl : public Socket {
public:
    SocketImpl(SOCKET handle) : handle_(handle) {}
    
    ~SocketImpl() override {
        close();
    }
    
    bool bind(const SocketAddress& address) override {
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(address.port);
        addr.sin_addr.s_addr = inet_addr(address.ip.c_str());
        
        if (::bind(handle_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            LOG_ERROR("SocketImpl", "bind failed");
            return false;
        }
        return true;
    }
    
    bool listen(int backlog) override {
        if (::listen(handle_, backlog) == SOCKET_ERROR) {
            LOG_ERROR("SocketImpl", "listen failed");
            return false;
        }
        return true;
    }
    
    std::unique_ptr<Socket> accept(SocketAddress& peer_addr) override {
        sockaddr_in addr = {};
        socklen_t addr_len = sizeof(addr);
        
        SOCKET client = ::accept(handle_, (sockaddr*)&addr, &addr_len);
        if (client == INVALID_SOCKET) {
            LOG_ERROR("SocketImpl", "accept failed");
            return nullptr;
        }
        
        peer_addr.ip = inet_ntoa(addr.sin_addr);
        peer_addr.port = ntohs(addr.sin_port);
        peer_addr.family = AddressFamily::IPv4;
        
        return std::make_unique<SocketImpl>(client);
    }
    
    bool connect(const SocketAddress& address) override {
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(address.port);
        addr.sin_addr.s_addr = inet_addr(address.ip.c_str());
        
        if (::connect(handle_, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            LOG_ERROR("SocketImpl", "connect failed");
            return false;
        }
        return true;
    }
    
    bool close() override {
        if (handle_ != INVALID_SOCKET) {
            closesocket(handle_);
            handle_ = INVALID_SOCKET;
        }
        return true;
    }
    
    size_t send(const uint8_t* data, size_t size) override {
        if (handle_ == INVALID_SOCKET) return 0;
        int sent = ::send(handle_, (const char*)data, (int)size, 0);
        return sent > 0 ? sent : 0;
    }
    
    size_t receive(uint8_t* data, size_t max_size) override {
        if (handle_ == INVALID_SOCKET) return 0;
        int received = ::recv(handle_, (char*)data, (int)max_size, 0);
        return received > 0 ? received : 0;
    }
    
    size_t send_to(const uint8_t* data, size_t size, const SocketAddress& addr) override {
        sockaddr_in dest_addr = {};
        dest_addr.sin_family = AF_INET;
        dest_addr.sin_port = htons(addr.port);
        dest_addr.sin_addr.s_addr = inet_addr(addr.ip.c_str());
        
        int sent = ::sendto(handle_, (const char*)data, (int)size, 0,
                           (sockaddr*)&dest_addr, sizeof(dest_addr));
        return sent > 0 ? sent : 0;
    }
    
    size_t receive_from(uint8_t* data, size_t max_size, SocketAddress& from_addr) override {
        sockaddr_in addr = {};
        socklen_t addr_len = sizeof(addr);
        
        int received = ::recvfrom(handle_, (char*)data, (int)max_size, 0,
                                 (sockaddr*)&addr, &addr_len);
        
        if (received > 0) {
            from_addr.ip = inet_ntoa(addr.sin_addr);
            from_addr.port = ntohs(addr.sin_port);
            from_addr.family = AddressFamily::IPv4;
        }
        
        return received > 0 ? received : 0;
    }
    
    bool set_non_blocking(bool non_blocking) override {
#ifdef _WIN32
        unsigned long mode = non_blocking ? 1 : 0;
        return ioctlsocket(handle_, FIONBIO, &mode) != SOCKET_ERROR;
#else
        int flags = fcntl(handle_, F_GETFL, 0);
        if (non_blocking) {
            flags |= O_NONBLOCK;
        } else {
            flags &= ~O_NONBLOCK;
        }
        return fcntl(handle_, F_SETFL, flags) >= 0;
#endif
    }
    
    bool set_reuse_address(bool reuse) override {
        int opt = reuse ? 1 : 0;
        return setsockopt(handle_, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) >= 0;
    }
    
    bool set_broadcast(bool broadcast) override {
        int opt = broadcast ? 1 : 0;
        return setsockopt(handle_, SOL_SOCKET, SO_BROADCAST, (const char*)&opt, sizeof(opt)) >= 0;
    }
    
    bool set_receive_timeout(int milliseconds) override {
#ifdef _WIN32
        return setsockopt(handle_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&milliseconds, sizeof(milliseconds)) >= 0;
#else
        timeval tv = {};
        tv.tv_sec = milliseconds / 1000;
        tv.tv_usec = (milliseconds % 1000) * 1000;
        return setsockopt(handle_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) >= 0;
#endif
    }
    
    bool set_send_timeout(int milliseconds) override {
#ifdef _WIN32
        return setsockopt(handle_, SOL_SOCKET, SO_SNDTIMEO, (const char*)&milliseconds, sizeof(milliseconds)) >= 0;
#else
        timeval tv = {};
        tv.tv_sec = milliseconds / 1000;
        tv.tv_usec = (milliseconds % 1000) * 1000;
        return setsockopt(handle_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv)) >= 0;
#endif
    }
    
    int get_native_handle() const override {
        return (int)handle_;
    }
    
    bool is_valid() const override {
        return handle_ != INVALID_SOCKET;
    }
    
private:
    SOCKET handle_;
};

class NetworkImpl : public Network {
public:
    NetworkImpl() {
#ifdef _WIN32
        WSADATA wsa_data;
        WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif
    }
    
    ~NetworkImpl() override {
#ifdef _WIN32
        WSACleanup();
#endif
    }
    
    std::unique_ptr<Socket> create_tcp_socket() override {
        SOCKET handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (handle == INVALID_SOCKET) {
            LOG_ERROR("NetworkImpl", "Failed to create TCP socket");
            return nullptr;
        }
        return std::make_unique<SocketImpl>(handle);
    }
    
    std::unique_ptr<Socket> create_udp_socket() override {
        SOCKET handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (handle == INVALID_SOCKET) {
            LOG_ERROR("NetworkImpl", "Failed to create UDP socket");
            return nullptr;
        }
        return std::make_unique<SocketImpl>(handle);
    }
    
    bool get_local_ip(AddressFamily family, std::string& ip) override {
        // Simple approach: connect to a public DNS server and get local address
        std::unique_ptr<Socket> sock = create_udp_socket();
        if (!sock) return false;
        
        SocketAddress dns_addr{"8.8.8.8", 53, family};
        if (!sock->connect(dns_addr)) return false;
        
        sockaddr_in local_addr = {};
        socklen_t addr_len = sizeof(local_addr);
        
        if (getsockname((SOCKET)sock->get_native_handle(), (sockaddr*)&local_addr, &addr_len) != 0) {
            return false;
        }
        
        ip = inet_ntoa(local_addr.sin_addr);
        return true;
    }
    
    bool get_hostname(std::string& hostname) override {
        char name[256];
        if (gethostname(name, sizeof(name)) != 0) {
            return false;
        }
        hostname = name;
        return true;
    }
    
    bool get_mac_address(std::string& mac) override {
#ifdef _WIN32
        // Windows-specific MAC address retrieval
        IP_ADAPTER_INFO* adapter_info = nullptr;
        DWORD size = 0;
        
        if (GetAdaptersInfo(adapter_info, &size) == ERROR_BUFFER_OVERFLOW) {
            adapter_info = (IP_ADAPTER_INFO*)malloc(size);
        } else {
            return false;
        }
        
        if (GetAdaptersInfo(adapter_info, &size) != NO_ERROR) {
            free(adapter_info);
            return false;
        }
        
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                adapter_info->Address[0], adapter_info->Address[1],
                adapter_info->Address[2], adapter_info->Address[3],
                adapter_info->Address[4], adapter_info->Address[5]);
        
        mac = mac_str;
        free(adapter_info);
        return true;
#else
        // Unix-like systems
        #ifdef __APPLE__
            // macOS implementation
            mac = "00:00:00:00:00:00"; // Placeholder
        #else
            // Linux implementation
            std::ifstream file("/sys/class/net/eth0/address");
            if (file.is_open()) {
                std::getline(file, mac);
                return true;
            }
        #endif
        return false;
#endif
    }
    
    bool is_network_reachable() override {
        // Simple check: try to resolve a well-known hostname
        hostent* host = gethostbyname("8.8.8.8");
        return host != nullptr;
    }
};

std::unique_ptr<Network> Network::create() {
    return std::make_unique<NetworkImpl>();
}

} // namespace syncflow::platform

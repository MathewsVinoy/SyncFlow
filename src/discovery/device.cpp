#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#pragma comment(lib, "Ws2_32.lib")
#else
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

namespace {

// Use a dedicated high, non-privileged port for discovery traffic.
constexpr int PORT = 37020;
constexpr int MAXLINE = 1024;
constexpr int DISCOVERY_TIMEOUT_MS = 3000;
constexpr char kDiscoverMessage[] = "DISCOVER_SYNCFLOW";
constexpr char kReplyPrefix[] = "SYNCFLOW_DEVICE";

#ifdef _WIN32
using SocketHandle = SOCKET;
using SocketLen = int;
constexpr SocketHandle kInvalidSocket = INVALID_SOCKET;
#else
using SocketHandle = int;
using SocketLen = socklen_t;
constexpr SocketHandle kInvalidSocket = -1;
#endif

bool init_socket_runtime() {
#ifdef _WIN32
    WSADATA wsa_data{};
    return WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0;
#else
    return true;
#endif
}

void shutdown_socket_runtime() {
#ifdef _WIN32
    WSACleanup();
#endif
}

void close_socket(SocketHandle socket_fd) {
#ifdef _WIN32
    closesocket(socket_fd);
#else
    close(socket_fd);
#endif
}

bool set_socket_option_int(SocketHandle socket_fd, int level, int name, int value) {
#ifdef _WIN32
    return setsockopt(socket_fd,
                      level,
                      name,
                      reinterpret_cast<const char*>(&value),
                      sizeof(value)) == 0;
#else
    return setsockopt(socket_fd, level, name, &value, sizeof(value)) == 0;
#endif
}

std::string get_hostname() {
    char hostname[256] = {0};
#ifdef _WIN32
    if (gethostname(hostname, static_cast<int>(sizeof(hostname))) != 0) {
        return "unknown-host";
    }
#else
    if (gethostname(hostname, sizeof(hostname)) != 0) {
        return "unknown-host";
    }
#endif
    hostname[sizeof(hostname) - 1] = '\0';
    return hostname;
}

std::string ip_to_string(const sockaddr_in& addr) {
    char ip[INET_ADDRSTRLEN] = {0};
    if (inet_ntop(AF_INET, &addr.sin_addr, ip, sizeof(ip)) == nullptr) {
        return "0.0.0.0";
    }
    return ip;
}

bool make_ipv4_addr(sockaddr_in& out, const char* ip, int port) {
    out = {};
    out.sin_family = AF_INET;
    out.sin_port = htons(port);
    return inet_pton(AF_INET, ip, &out.sin_addr) == 1;
}

}  // namespace

int server() {
    SocketHandle sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == kInvalidSocket) {
        perror("socket creation failed");
        return 1;
    }

    int reuse = 1;
    if (!set_socket_option_int(sockfd, SOL_SOCKET, SO_REUSEADDR, reuse)) {
        perror("setsockopt(SO_REUSEADDR) failed");
        close_socket(sockfd);
        return 1;
    }

    sockaddr_in servaddr{};
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);

    if (bind(sockfd, reinterpret_cast<const sockaddr*>(&servaddr), sizeof(servaddr)) < 0) {
        perror("bind failed");
        close_socket(sockfd);
        return 1;
    }

    std::cout << "Discovery server listening on UDP " << PORT << "...\n";
    char buffer[MAXLINE];
    const std::string hostname = get_hostname();

    while (true) {
        sockaddr_in cliaddr{};
        SocketLen len = static_cast<SocketLen>(sizeof(cliaddr));

        const int n = recvfrom(
            sockfd,
            buffer,
            sizeof(buffer) - 1,
            0,
            reinterpret_cast<sockaddr*>(&cliaddr),
            &len);

        if (n < 0) {
            perror("recvfrom failed");
            continue;
        }

        buffer[n] = '\0';
        const std::string request(buffer);

        if (request != kDiscoverMessage) {
            continue;
        }

        const std::string client_ip = ip_to_string(cliaddr);
        std::cout << "Discovery request from " << client_ip << "\n";

        const std::string reply =
            std::string(kReplyPrefix) + "|" + hostname + "|" + std::to_string(PORT);

        if (sendto(sockfd,
                   reply.c_str(),
                   static_cast<int>(reply.size()),
                   0,
                   reinterpret_cast<const sockaddr*>(&cliaddr),
                   len) < 0) {
            perror("sendto failed");
        }
    }

    close_socket(sockfd);
    return 0;
}

int client() {
    SocketHandle sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd == kInvalidSocket) {
        perror("socket creation failed");
        return 1;
    }

    // Enable broadcast mode (required for LAN auto-discovery).
    int broadcast_enable = 1;
    if (!set_socket_option_int(sockfd, SOL_SOCKET, SO_BROADCAST, broadcast_enable)) {
        perror("setsockopt(SO_BROADCAST) failed");
        close_socket(sockfd);
        return 1;
    }

    // Optional: avoid hanging forever while waiting for replies.
#ifdef _WIN32
    DWORD timeout_ms = DISCOVERY_TIMEOUT_MS;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms)) < 0) {
        perror("setsockopt(SO_RCVTIMEO) failed");
        close_socket(sockfd);
        return 1;
    }
#else
    timeval timeout{};
    timeout.tv_sec = DISCOVERY_TIMEOUT_MS / 1000;
    timeout.tv_usec = 0;
    if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt(SO_RCVTIMEO) failed");
        close_socket(sockfd);
        return 1;
    }
#endif

    // Probe both broadcast and localhost so local-dev works reliably.
    std::vector<sockaddr_in> probe_targets;

    sockaddr_in bcast_addr{};
    bcast_addr.sin_family = AF_INET;
    bcast_addr.sin_port = htons(PORT);
    bcast_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);  // 255.255.255.255
    probe_targets.push_back(bcast_addr);

    sockaddr_in localhost_addr{};
    if (make_ipv4_addr(localhost_addr, "127.0.0.1", PORT)) {
        probe_targets.push_back(localhost_addr);
    }

    bool sent_any = false;
    for (const auto& target : probe_targets) {
        const int sent = sendto(sockfd,
                                kDiscoverMessage,
                                static_cast<int>(std::strlen(kDiscoverMessage)),
                                0,
                                reinterpret_cast<const sockaddr*>(&target),
                                static_cast<SocketLen>(sizeof(target)));
        if (sent >= 0) {
            sent_any = true;
        }
    }

    if (!sent_any) {
        perror("discovery probe send failed");
        close_socket(sockfd);
        return 1;
    }

    std::cout << "Broadcast discovery sent. Waiting for responses...\n";

    char buffer[MAXLINE];
    std::set<std::string> seen_devices;
    int found_count = 0;
    while (true) {
        sockaddr_in from{};
        SocketLen len = static_cast<SocketLen>(sizeof(from));

        const int n = recvfrom(
            sockfd,
            buffer,
            sizeof(buffer) - 1,
            0,
            reinterpret_cast<sockaddr*>(&from),
            &len);

        if (n < 0) {
#ifdef _WIN32
            const int socket_error = WSAGetLastError();
            if (socket_error == WSAETIMEDOUT) {
                break;  // timeout: discovery window ended
            }
#else
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  // timeout: discovery window ended
            }
#endif
            perror("recvfrom failed");
            close_socket(sockfd);
            return 1;
        }

        buffer[n] = '\0';
        const std::string sender_ip = ip_to_string(from);
        if (!seen_devices.insert(sender_ip + "|" + std::string(buffer)).second) {
            continue;
        }

        ++found_count;
        std::cout << "Found device " << sender_ip << ": " << buffer << "\n";
    }

    if (found_count == 0) {
        std::cout << "No devices discovered on UDP " << PORT << "\n";
    }

    close_socket(sockfd);
    return 0;
}

int main(int argc, char* argv[]) {
    if (!init_socket_runtime()) {
        std::cerr << "Failed to initialize socket runtime\n";
        return 1;
    }

    if (argc < 2) {
        std::cerr << "Usage: syncflow_discovery [server|client]\n";
        shutdown_socket_runtime();
        return 1;
    }

    const std::string mode = argv[1];
    int rc = 1;

    if (mode == "server") {
        rc = server();
    } else if (mode == "client") {
        rc = client();
    } else {
        std::cerr << "Unknown mode: " << mode << "\n";
        std::cerr << "Usage: syncflow_discovery [server|client]\n";
    }

    shutdown_socket_runtime();
    return rc;
}
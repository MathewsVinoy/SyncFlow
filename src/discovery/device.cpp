#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <set>
#include <string>
#include <thread>
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

// Default ports (can be overridden via environment variables).
constexpr int DEFAULT_DISCOVERY_UDP_PORT = 37020;
constexpr int DEFAULT_HANDSHAKE_TCP_PORT = 37021;
constexpr int MAXLINE = 1024;
constexpr int DEFAULT_DISCOVERY_TIMEOUT_MS = 3000;
constexpr int DEFAULT_TCP_TIMEOUT_MS = 2000;
constexpr char kProtocolVersion[] = "v1";
constexpr char kDiscoverMessage[] = "DISCOVER_SYNCFLOW";
constexpr char kReplyPrefix[] = "SYNCFLOW_DEVICE";
constexpr char kTcpHelloPrefix[] = "SYNCFLOW_TCP_HELLO";
constexpr char kTcpAckPrefix[] = "SYNCFLOW_TCP_ACK";
constexpr char kDefaultAuthToken[] = "syncflow-dev-token";

struct RuntimeConfig {
    int discovery_udp_port = DEFAULT_DISCOVERY_UDP_PORT;
    int handshake_tcp_port = DEFAULT_HANDSHAKE_TCP_PORT;
    int discovery_timeout_ms = DEFAULT_DISCOVERY_TIMEOUT_MS;
    int tcp_timeout_ms = DEFAULT_TCP_TIMEOUT_MS;
    std::string auth_token = kDefaultAuthToken;
};

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

int parse_env_int(const char* key, int default_value, int min_value, int max_value) {
    const char* raw = std::getenv(key);
    if (raw == nullptr || raw[0] == '\0') {
        return default_value;
    }

    try {
        int value = std::stoi(raw);
        if (value < min_value || value > max_value) {
            return default_value;
        }
        return value;
    } catch (...) {
        return default_value;
    }
}

RuntimeConfig load_runtime_config() {
    RuntimeConfig cfg;
    cfg.discovery_udp_port = parse_env_int("SYNCFLOW_DISCOVERY_UDP_PORT", DEFAULT_DISCOVERY_UDP_PORT, 1024, 65535);
    cfg.handshake_tcp_port = parse_env_int("SYNCFLOW_HANDSHAKE_TCP_PORT", DEFAULT_HANDSHAKE_TCP_PORT, 1024, 65535);
    cfg.discovery_timeout_ms = parse_env_int("SYNCFLOW_DISCOVERY_TIMEOUT_MS", DEFAULT_DISCOVERY_TIMEOUT_MS, 500, 30000);
    cfg.tcp_timeout_ms = parse_env_int("SYNCFLOW_TCP_TIMEOUT_MS", DEFAULT_TCP_TIMEOUT_MS, 500, 30000);

    const char* token = std::getenv("SYNCFLOW_AUTH_TOKEN");
    if (token != nullptr && token[0] != '\0') {
        cfg.auth_token = token;
    }
    return cfg;
}

const RuntimeConfig& runtime_config() {
    static const RuntimeConfig cfg = load_runtime_config();
    return cfg;
}

std::vector<std::string> split(const std::string& text, char delim) {
    std::vector<std::string> out;
    std::string token;
    for (char c : text) {
        if (c == delim) {
            out.push_back(token);
            token.clear();
        } else {
            token.push_back(c);
        }
    }
    out.push_back(token);
    return out;
}

bool set_recv_timeout(SocketHandle socket_fd, int timeout_ms) {
#ifdef _WIN32
    const DWORD to = static_cast<DWORD>(timeout_ms);
    return setsockopt(socket_fd,
                      SOL_SOCKET,
                      SO_RCVTIMEO,
                      reinterpret_cast<const char*>(&to),
                      sizeof(to)) == 0;
#else
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    return setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) == 0;
#endif
}

bool set_send_timeout(SocketHandle socket_fd, int timeout_ms) {
#ifdef _WIN32
    const DWORD to = static_cast<DWORD>(timeout_ms);
    return setsockopt(socket_fd,
                      SOL_SOCKET,
                      SO_SNDTIMEO,
                      reinterpret_cast<const char*>(&to),
                      sizeof(to)) == 0;
#else
    timeval timeout{};
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    return setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) == 0;
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

void tcp_handshake_server_loop() {
    const auto& cfg = runtime_config();
    SocketHandle listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == kInvalidSocket) {
        perror("tcp socket creation failed");
        return;
    }

    int reuse = 1;
    if (!set_socket_option_int(listen_fd, SOL_SOCKET, SO_REUSEADDR, reuse)) {
        perror("tcp setsockopt(SO_REUSEADDR) failed");
        close_socket(listen_fd);
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(cfg.handshake_tcp_port);

    if (bind(listen_fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
        perror("tcp bind failed");
        close_socket(listen_fd);
        return;
    }

    if (listen(listen_fd, 16) < 0) {
        perror("tcp listen failed");
        close_socket(listen_fd);
        return;
    }

    std::cout << "TCP handshake server listening on " << cfg.handshake_tcp_port << "...\n";
    const std::string local_host = get_hostname();

    while (true) {
        sockaddr_in peer_addr{};
        SocketLen peer_len = static_cast<SocketLen>(sizeof(peer_addr));
        SocketHandle conn_fd = accept(
            listen_fd,
            reinterpret_cast<sockaddr*>(&peer_addr),
            &peer_len);

        if (conn_fd == kInvalidSocket) {
            perror("tcp accept failed");
            continue;
        }

        if (!set_recv_timeout(conn_fd, cfg.tcp_timeout_ms) || !set_send_timeout(conn_fd, cfg.tcp_timeout_ms)) {
            close_socket(conn_fd);
            continue;
        }

        char buffer[MAXLINE];
        const int n = recv(conn_fd, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) {
            close_socket(conn_fd);
            continue;
        }

        buffer[n] = '\0';
        const std::string request(buffer);
        const std::string peer_ip = ip_to_string(peer_addr);

        const std::vector<std::string> parts = split(request, '|');
        if (parts.size() >= 4 &&
            parts[0] == kTcpHelloPrefix &&
            parts[1] == kProtocolVersion &&
            parts[3] == cfg.auth_token) {
            const std::string ack =
                std::string(kTcpAckPrefix) + "|" + kProtocolVersion + "|" + local_host;
            if (send(conn_fd, ack.c_str(), static_cast<int>(ack.size()), 0) < 0) {
                perror("tcp send ack failed");
            } else {
                std::cout << "TCP handshake completed with " << peer_ip << "\n";
            }
        }

        close_socket(conn_fd);
    }
}

bool perform_tcp_handshake(const sockaddr_in& discovered_from, const std::string& payload) {
    const auto& cfg = runtime_config();
    std::vector<std::string> parts = split(payload, '|');

    if (parts.size() < 5 || parts[0] != kReplyPrefix || parts[1] != kProtocolVersion) {
        return false;
    }

    int tcp_port = 0;
    try {
        tcp_port = std::stoi(parts[4]);
    } catch (...) {
        return false;
    }

    SocketHandle conn_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (conn_fd == kInvalidSocket) {
        perror("tcp client socket creation failed");
        return false;
    }

    if (!set_recv_timeout(conn_fd, cfg.tcp_timeout_ms) || !set_send_timeout(conn_fd, cfg.tcp_timeout_ms)) {
        close_socket(conn_fd);
        return false;
    }

    sockaddr_in target{};
    target.sin_family = AF_INET;
    target.sin_addr = discovered_from.sin_addr;
    target.sin_port = htons(tcp_port);

    if (connect(conn_fd, reinterpret_cast<const sockaddr*>(&target), sizeof(target)) < 0) {
        perror("tcp connect failed");
        close_socket(conn_fd);
        return false;
    }

    const std::string hello =
        std::string(kTcpHelloPrefix) + "|" + kProtocolVersion + "|" + get_hostname() + "|" + cfg.auth_token;
    if (send(conn_fd, hello.c_str(), static_cast<int>(hello.size()), 0) < 0) {
        perror("tcp send hello failed");
        close_socket(conn_fd);
        return false;
    }

    char ack[MAXLINE];
    const int n = recv(conn_fd, ack, sizeof(ack) - 1, 0);
    if (n <= 0) {
        perror("tcp recv ack failed");
        close_socket(conn_fd);
        return false;
    }

    ack[n] = '\0';
    close_socket(conn_fd);

    const std::string ack_msg(ack);
    const auto ack_parts = split(ack_msg, '|');
    return ack_parts.size() >= 3 && ack_parts[0] == kTcpAckPrefix && ack_parts[1] == kProtocolVersion;
}

}  // namespace

int server() {
    const auto& cfg = runtime_config();
    std::thread tcp_thread(tcp_handshake_server_loop);
    tcp_thread.detach();

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
    servaddr.sin_port = htons(cfg.discovery_udp_port);

    if (bind(sockfd, reinterpret_cast<const sockaddr*>(&servaddr), sizeof(servaddr)) < 0) {
        perror("bind failed");
        close_socket(sockfd);
        return 1;
    }

    std::cout << "Discovery server listening on UDP " << cfg.discovery_udp_port << "...\n";
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
            std::string(kReplyPrefix) + "|" + kProtocolVersion + "|" + hostname + "|" +
            std::to_string(cfg.discovery_udp_port) + "|" + std::to_string(cfg.handshake_tcp_port);

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
    const auto& cfg = runtime_config();
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
    if (!set_recv_timeout(sockfd, cfg.discovery_timeout_ms)) {
        perror("setsockopt(SO_RCVTIMEO) failed");
        close_socket(sockfd);
        return 1;
    }

    // Probe both broadcast and localhost so local-dev works reliably.
    std::vector<sockaddr_in> probe_targets;

    sockaddr_in bcast_addr{};
    bcast_addr.sin_family = AF_INET;
    bcast_addr.sin_port = htons(cfg.discovery_udp_port);
    bcast_addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);  // 255.255.255.255
    probe_targets.push_back(bcast_addr);

    sockaddr_in localhost_addr{};
    if (make_ipv4_addr(localhost_addr, "127.0.0.1", cfg.discovery_udp_port)) {
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

        const bool handshake_ok = perform_tcp_handshake(from, std::string(buffer));
        ++found_count;
        std::cout << "Found device " << sender_ip << ": " << buffer
                  << " | tcp-handshake=" << (handshake_ok ? "ok" : "failed") << "\n";
    }

    if (found_count == 0) {
        std::cout << "No devices discovered on UDP " << cfg.discovery_udp_port << "\n";
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

    const auto& cfg = runtime_config();
    std::cout << "syncflow protocol=" << kProtocolVersion
              << " udp=" << cfg.discovery_udp_port
              << " tcp=" << cfg.handshake_tcp_port
              << " timeout_ms=" << cfg.discovery_timeout_ms << "\n";

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
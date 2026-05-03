#include "syncflow/networking/peer_node.h"

#include "syncflow/config.h"
#include "syncflow/platform/system_info.h"

#include <algorithm>
#include <array>
#include <arpa/inet.h>
#include <chrono>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>

namespace syncflow::networking {

namespace {

void close_socket(int fd) {
    if (fd >= 0) {
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
    }
}

bool send_all(int fd, const std::string& data) {
    const char* ptr = data.data();
    std::size_t remaining = data.size();

    while (remaining > 0) {
        const ssize_t sent = ::send(fd, ptr, remaining, 0);
        if (sent <= 0) {
            return false;
        }
        ptr += sent;
        remaining -= static_cast<std::size_t>(sent);
    }

    return true;
}

}  // namespace

PeerNode::PeerNode(std::string device_name)
    : device_name_(std::move(device_name)),
      local_ip_(platform::get_local_ipv4()),
      logger_(device_name_, local_ip_) {}

void PeerNode::run() {
    platform::install_signal_handlers(running_);
    log_startup();

    tcp_thread_ = std::thread([this] { tcp_server_loop(); });
    udp_thread_ = std::thread([this] { udp_listener_loop(); });
    broadcast_thread_ = std::thread([this] { broadcast_loop(); });

    std::cout << "Press Ctrl+C to stop.\n";

    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    stop();
}

void PeerNode::stop() {
    close_socket(tcp_server_fd_.exchange(-1));
    close_socket(udp_listener_fd_.exchange(-1));

    if (broadcast_thread_.joinable()) {
        broadcast_thread_.join();
    }
    if (udp_thread_.joinable()) {
        udp_thread_.join();
    }
    if (tcp_thread_.joinable()) {
        tcp_thread_.join();
    }

    logger_.info("shutdown complete");
}

void PeerNode::log_startup() {
    logger_.info("starting peer node");
    logger_.info("device name: " + device_name_ + ", ip: " + local_ip_ +
                 ", tcp port: " + std::to_string(config::kTcpPort) +
                 ", udp port: " + std::to_string(config::kUdpDiscoveryPort));
}

bool PeerNode::should_initiate(const PeerInfo& peer) const {
    if (peer.name == device_name_ && peer.ip == local_ip_) {
        return false;
    }

    if (device_name_ != peer.name) {
        return device_name_ < peer.name;
    }

    return local_ip_ < peer.ip;
}

bool PeerNode::is_active(const PeerInfo& peer) {
    const auto key = endpoint_key(peer);
    std::lock_guard<std::mutex> guard(active_mutex_);
    return active_connections_.find(key) != active_connections_.end();
}

void PeerNode::mark_active(const PeerInfo& peer) {
    std::lock_guard<std::mutex> guard(active_mutex_);
    active_connections_.insert(endpoint_key(peer));
}

void PeerNode::mark_inactive(const PeerInfo& peer) {
    std::lock_guard<std::mutex> guard(active_mutex_);
    active_connections_.erase(endpoint_key(peer));
}

void PeerNode::handle_peer_connection(int fd, PeerInfo peer, const std::string& direction) {
    mark_active(peer);
    logger_.info(direction + " connection established with " + peer.name + " @ " + peer.ip + ":" + std::to_string(peer.tcp_port));
    logger_.info("connected successfully with " + peer.name + " @ " + peer.ip);

    const PeerInfo self{config::kMagic, device_name_, local_ip_, config::kTcpPort};
    const std::string hello = "HELLO|" + serialize_peer_info(self);
    (void)send_all(fd, hello);

    std::array<char, 1024> buffer{};
    while (running_) {
        const ssize_t received = ::recv(fd, buffer.data(), buffer.size() - 1, 0);
        if (received <= 0) {
            break;
        }

        buffer[static_cast<std::size_t>(received)] = '\0';
        std::string text = buffer.data();
        text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
        text.erase(std::remove(text.begin(), text.end(), '\n'), text.end());
        if (!text.empty()) {
            logger_.info("message from " + peer.name + " @ " + peer.ip + ": " + text);
        }
    }

    logger_.info("connection closed with " + peer.name + " @ " + peer.ip);
    mark_inactive(peer);
    close_socket(fd);
}

void PeerNode::connect_to_peer(PeerInfo peer) {
    if (!running_ || is_active(peer)) {
        return;
    }

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        logger_.info("failed to create TCP client socket");
        return;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(peer.tcp_port);
    if (::inet_pton(AF_INET, peer.ip.c_str(), &addr.sin_addr) != 1) {
        logger_.info("invalid peer ip address: " + peer.ip);
        close_socket(fd);
        return;
    }

    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close_socket(fd);
        return;
    }

    handle_peer_connection(fd, std::move(peer), "outbound");
}

void PeerNode::broadcast_loop() {
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        logger_.info("failed to create UDP broadcast socket");
        return;
    }

    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes));
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(config::kUdpDiscoveryPort);
    ::inet_pton(AF_INET, config::kBroadcastAddress, &dest.sin_addr);

    const PeerInfo self{config::kMagic, device_name_, local_ip_, config::kTcpPort};
    const std::string payload = serialize_peer_info(self);

    while (running_) {
        (void)::sendto(fd, payload.data(), payload.size(), 0, reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
        std::this_thread::sleep_for(config::kDiscoveryInterval);
    }

    close_socket(fd);
}

void PeerNode::udp_listener_loop() {
    const int fd = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        logger_.info("failed to create UDP listener socket");
        return;
    }

    udp_listener_fd_ = fd;

    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config::kUdpDiscoveryPort);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        logger_.info("failed to bind UDP listener port " + std::to_string(config::kUdpDiscoveryPort));
        close_socket(fd);
        return;
    }

    std::array<char, 1024> buffer{};
    while (running_) {
        sockaddr_in from{};
        socklen_t from_len = sizeof(from);
        const ssize_t received = ::recvfrom(fd, buffer.data(), buffer.size() - 1, 0, reinterpret_cast<sockaddr*>(&from), &from_len);
        if (received <= 0) {
            continue;
        }

        buffer[static_cast<std::size_t>(received)] = '\0';
        PeerInfo peer;
        if (!parse_peer_info(buffer.data(), peer)) {
            continue;
        }

        if (peer.name == device_name_ && peer.ip == local_ip_) {
            continue;
        }

        logger_.info("discovered peer " + peer.name + " @ " + peer.ip + " (tcp port " + std::to_string(peer.tcp_port) + ")");

        if (should_initiate(peer) && !is_active(peer)) {
            std::thread([this, peer] { connect_to_peer(peer); }).detach();
        }
    }

    close_socket(fd);
}

void PeerNode::tcp_server_loop() {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        logger_.info("failed to create TCP server socket");
        return;
    }

    tcp_server_fd_ = fd;

    int yes = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config::kTcpPort);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        logger_.info("failed to bind TCP server port " + std::to_string(config::kTcpPort));
        close_socket(fd);
        return;
    }

    if (::listen(fd, 8) != 0) {
        logger_.info("failed to listen on TCP port " + std::to_string(config::kTcpPort));
        close_socket(fd);
        return;
    }

    logger_.info("TCP server listening on port " + std::to_string(config::kTcpPort));

    while (running_) {
        sockaddr_in from{};
        socklen_t from_len = sizeof(from);
        const int client = ::accept(fd, reinterpret_cast<sockaddr*>(&from), &from_len);
        if (client < 0) {
            if (!running_) {
                break;
            }
            continue;
        }

        char ip[INET_ADDRSTRLEN] = {};
        ::inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));

        std::thread([this, client, remote_ip = std::string(ip)] {
            std::array<char, 1024> buffer{};
            const ssize_t received = ::recv(client, buffer.data(), buffer.size() - 1, 0);
            PeerInfo peer{config::kMagic, "unknown", remote_ip, 0};

            if (received > 0) {
                buffer[static_cast<std::size_t>(received)] = '\0';
                std::string line = buffer.data();
                line.erase(std::remove(line.begin(), line.end(), '\r'), line.end());
                line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());

                if (line.rfind("HELLO|", 0) == 0) {
                    line.erase(0, 6);
                }

                PeerInfo parsed;
                if (parse_peer_info(line, parsed)) {
                    peer = parsed;
                }
            }

            handle_peer_connection(client, peer, "inbound");
        }).detach();
    }

    close_socket(fd);
}

}  // namespace syncflow::networking

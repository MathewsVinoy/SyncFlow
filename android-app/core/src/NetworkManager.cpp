#include "NetworkManager.hpp"
#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <thread>

namespace {

constexpr const char* kPeerMagic = "SYNCFLOW_PEER";

void close_socket(int fd) {
    if (fd >= 0) {
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
    }
}

bool send_all_impl(int fd, const void* data, std::size_t size) {
    const auto* ptr = static_cast<const char*>(data);
    std::size_t remaining = size;

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

bool recv_exact_impl(int fd, void* data, std::size_t size) {
    auto* ptr = static_cast<char*>(data);
    std::size_t remaining = size;

    while (remaining > 0) {
        const ssize_t received = ::recv(fd, ptr, remaining, 0);
        if (received <= 0) {
            return false;
        }
        ptr += received;
        remaining -= static_cast<std::size_t>(received);
    }

    return true;
}

bool recv_line_impl(int fd, std::string& line) {
    line.clear();
    char ch = '\0';
    while (true) {
        const ssize_t received = ::recv(fd, &ch, 1, 0);
        if (received <= 0) {
            return false;
        }
        if (ch == '\n') {
            return true;
        }
        if (ch != '\r') {
            line.push_back(ch);
        }
    }
}

std::filesystem::path safe_receive_path(const std::filesystem::path& base_dir, const std::string& relative) {
    const auto clean = std::filesystem::path(relative).lexically_normal();
    return base_dir / clean;
}

}  // namespace

namespace syncflow {
namespace network {

NetworkManager::NetworkManager()
    : discovering_(false), server_running_(false), connected_(false), socket_fd_(-1), remote_port_(45455) {}

NetworkManager::~NetworkManager() {
    disconnect();
    stop_discovery();
    stop_server();
}

void NetworkManager::set_device_name(const std::string& device_name) {
    device_name_ = device_name;
}

void NetworkManager::set_receive_dir(const std::filesystem::path& receive_dir) {
    receive_dir_ = receive_dir;
}

void NetworkManager::set_remote_peer(const std::string& host, int port) {
    remote_host_ = host;
    remote_port_ = port;
}

bool NetworkManager::is_connected() const {
    return connected_;
}

bool NetworkManager::connect_with_timeout(int fd, const sockaddr_in& addr, std::chrono::seconds timeout, std::string& error_text) {
    const int old_flags = ::fcntl(fd, F_GETFL, 0);
    if (old_flags < 0) {
        error_text = std::strerror(errno);
        return false;
    }

    if (::fcntl(fd, F_SETFL, old_flags | O_NONBLOCK) != 0) {
        error_text = std::strerror(errno);
        return false;
    }

    const int connect_rc = ::connect(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    if (connect_rc == 0) {
        (void)::fcntl(fd, F_SETFL, old_flags);
        return true;
    }

    if (errno != EINPROGRESS) {
        error_text = std::strerror(errno);
        (void)::fcntl(fd, F_SETFL, old_flags);
        return false;
    }

    fd_set write_set;
    FD_ZERO(&write_set);
    FD_SET(fd, &write_set);

    timeval tv{};
    tv.tv_sec = timeout.count();
    tv.tv_usec = 0;

    const int select_rc = ::select(fd + 1, nullptr, &write_set, nullptr, &tv);
    if (select_rc <= 0) {
        error_text = (select_rc == 0) ? "timeout" : std::strerror(errno);
        (void)::fcntl(fd, F_SETFL, old_flags);
        return false;
    }

    int so_error = 0;
    socklen_t so_error_len = sizeof(so_error);
    if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &so_error_len) != 0) {
        error_text = std::strerror(errno);
        (void)::fcntl(fd, F_SETFL, old_flags);
        return false;
    }

    if (so_error != 0) {
        error_text = std::strerror(so_error);
        (void)::fcntl(fd, F_SETFL, old_flags);
        return false;
    }

    (void)::fcntl(fd, F_SETFL, old_flags);
    return true;
}

bool NetworkManager::send_all(int fd, const void* data, std::size_t size) {
    return send_all_impl(fd, data, size);
}

bool NetworkManager::recv_exact(int fd, void* data, std::size_t size) {
    return recv_exact_impl(fd, data, size);
}

bool NetworkManager::recv_line(int fd, std::string& line) {
    return recv_line_impl(fd, line);
}

bool NetworkManager::handle_incoming_file(int fd, const std::filesystem::path& output_path, std::uint64_t expected_size) {
    std::error_code ec;
    std::filesystem::create_directories(output_path.parent_path(), ec);
    if (ec) {
        return false;
    }

    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }

    std::vector<char> buffer(4096);
    std::uint64_t received_total = 0;
    while (received_total < expected_size) {
        const std::uint64_t remaining = expected_size - received_total;
        const std::size_t chunk = static_cast<std::size_t>(std::min<std::uint64_t>(remaining, buffer.size()));
        if (!recv_exact(fd, buffer.data(), chunk)) {
            return false;
        }

        output.write(buffer.data(), static_cast<std::streamsize>(chunk));
        if (!output) {
            return false;
        }

        received_total += static_cast<std::uint64_t>(chunk);
    }

    return true;
}

bool NetworkManager::handle_session(int fd) {
    const std::string hello = std::string("HELLO|") + kPeerMagic + "|" +
                              (device_name_.empty() ? std::string("android") : device_name_) +
                              "|0.0.0.0|" + std::to_string(remote_port_) + "\n";
    if (!send_all(fd, hello.data(), hello.size())) {
        return false;
    }

    std::filesystem::path current_base = receive_dir_.empty() ? std::filesystem::current_path() / "syncflow_received" : receive_dir_;
    std::string current_file_name;
    std::uint64_t current_expected_size = 0;

    while (connected_) {
        std::string line;
        if (!recv_line(fd, line)) {
            break;
        }

        if (line.rfind("SYNC_BEGIN|", 0) == 0) {
            const std::size_t first_sep = line.find('|');
            const std::size_t second_sep = line.find('|', first_sep + 1);
            const std::size_t third_sep = line.find('|', second_sep + 1);
            if (first_sep == std::string::npos || second_sep == std::string::npos || third_sep == std::string::npos) {
                continue;
            }

            current_file_name = line.substr(first_sep + 1, second_sep - first_sep - 1);
            const std::string kind = line.substr(second_sep + 1, third_sep - second_sep - 1);

            if (kind == "FILE") {
                try {
                    current_expected_size = static_cast<std::uint64_t>(std::stoull(line.substr(third_sep + 1)));
                } catch (...) {
                    continue;
                }

                const auto output_path = safe_receive_path(current_base, current_file_name);
                if (!handle_incoming_file(fd, output_path, current_expected_size)) {
                    return false;
                }
                current_file_name.clear();
                current_expected_size = 0;
                continue;
            }

            if (kind == "DIR") {
                std::error_code ec;
                std::filesystem::create_directories(current_base, ec);
                continue;
            }
        }

        if (line.rfind("FILE_ENTRY|", 0) == 0) {
            const std::size_t first_sep = line.find('|');
            const std::size_t second_sep = line.find('|', first_sep + 1);
            if (first_sep == std::string::npos || second_sep == std::string::npos) {
                continue;
            }

            const std::string relative_path = line.substr(first_sep + 1, second_sep - first_sep - 1);
            try {
                current_expected_size = static_cast<std::uint64_t>(std::stoull(line.substr(second_sep + 1)));
            } catch (...) {
                continue;
            }

            const auto output_path = safe_receive_path(current_base, relative_path);
            if (!handle_incoming_file(fd, output_path, current_expected_size)) {
                return false;
            }
            continue;
        }

        if (line.rfind("SYNC_END|", 0) == 0) {
            current_file_name.clear();
            current_expected_size = 0;
            continue;
        }
    }

    return true;
}

bool NetworkManager::connect_to_peer() {
    if (remote_host_.empty() || remote_port_ <= 0 || connected_) {
        return false;
    }

    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(remote_port_));
    if (::inet_pton(AF_INET, remote_host_.c_str(), &addr.sin_addr) != 1) {
        close_socket(fd);
        return false;
    }

    std::string connect_error;
    if (!connect_with_timeout(fd, addr, std::chrono::seconds(3), connect_error)) {
        close_socket(fd);
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        socket_fd_ = fd;
        connected_ = true;
    }

    session_thread_ = std::thread([this, fd]() {
        (void)handle_session(fd);
        disconnect();
    });

    return true;
}

void NetworkManager::disconnect() {
    connected_ = false;

    int fd = -1;
    {
        std::lock_guard<std::mutex> lock(socket_mutex_);
        fd = socket_fd_;
        socket_fd_ = -1;
    }

    close_socket(fd);

    if (session_thread_.joinable() && std::this_thread::get_id() != session_thread_.get_id()) {
        session_thread_.join();
    }
}

void NetworkManager::start_discovery(std::function<void(const DeviceInfo&)> on_device_found) {
    if (discovering_) return;
    discovering_ = true;
    discovery_thread_ = std::thread([this, on_device_found]() {
        while (discovering_) {
            if (!remote_host_.empty()) {
                DeviceInfo info;
                info.id = remote_host_;
                info.name = device_name_.empty() ? "android" : device_name_;
                info.ip_address = remote_host_;
                info.port = remote_port_;
                on_device_found(info);
                break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    });
}

void NetworkManager::stop_discovery() {
    discovering_ = false;
    if (discovery_thread_.joinable()) discovery_thread_.join();
}

void NetworkManager::start_server(int port) {
    if (server_running_) return;
    server_running_ = true;
    server_thread_ = std::thread([this, port]() {
        (void)port;
        while (server_running_) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });
}

void NetworkManager::stop_server() {
    server_running_ = false;
    if (server_thread_.joinable()) server_thread_.join();
}

bool NetworkManager::send_file(const std::string& ip, int port, const std::string& filepath) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    if (::inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        close_socket(fd);
        return false;
    }

    std::string error_text;
    if (!connect_with_timeout(fd, addr, std::chrono::seconds(3), error_text)) {
        close_socket(fd);
        return false;
    }

    std::ifstream input(filepath, std::ios::binary);
    if (!input) {
        close_socket(fd);
        return false;
    }

    std::vector<char> buffer(8192);
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize got = input.gcount();
        if (got > 0 && !send_all(fd, buffer.data(), static_cast<std::size_t>(got))) {
            close_socket(fd);
            return false;
        }
    }

    close_socket(fd);
    return true;
}

} // namespace network
} // namespace syncflow

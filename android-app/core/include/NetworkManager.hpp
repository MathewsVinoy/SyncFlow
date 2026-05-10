#ifndef NETWORK_MANAGER_HPP
#define NETWORK_MANAGER_HPP

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>
#include <mutex>

#include <netinet/in.h>

namespace syncflow {
namespace network {

struct DeviceInfo {
    std::string id;
    std::string name;
    std::string ip_address;
    int port;
};

class NetworkManager {
public:
    NetworkManager();
    ~NetworkManager();

    void set_device_name(const std::string& device_name);
    void set_receive_dir(const std::filesystem::path& receive_dir);
    void set_remote_peer(const std::string& host, int port);

    bool connect_to_peer();
    void disconnect();
    bool is_connected() const;
    std::string last_error() const;

    void start_discovery(std::function<void(const DeviceInfo&)> on_device_found);
    void stop_discovery();

    void start_server(int port);
    void stop_server();

    bool send_file(const std::string& ip, int port, const std::string& filepath);

private:
    bool connect_with_timeout(int fd, const struct sockaddr_in& addr, std::chrono::seconds timeout, std::string& error_text);
    bool send_all(int fd, const void* data, std::size_t size);
    bool recv_line(int fd, std::string& line);
    bool recv_exact(int fd, void* data, std::size_t size);
    bool handle_session(int fd);
    bool handle_incoming_file(int fd, const std::filesystem::path& output_path, std::uint64_t expected_size);
    void set_last_error(const std::string& error);

    std::atomic<bool> discovering_;
    std::atomic<bool> server_running_;
    std::atomic<bool> connected_;
    std::thread discovery_thread_;
    std::thread server_thread_;
    std::thread session_thread_;

    std::mutex socket_mutex_;
    int socket_fd_;

    std::string device_name_;
    std::string remote_host_;
    int remote_port_;
    std::filesystem::path receive_dir_;
    mutable std::mutex error_mutex_;
    std::string last_error_;
};

} // namespace network
} // namespace syncflow

#endif // NETWORK_MANAGER_HPP

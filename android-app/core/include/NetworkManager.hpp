#ifndef NETWORK_MANAGER_HPP
#define NETWORK_MANAGER_HPP

#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <atomic>

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

    void start_discovery(std::function<void(const DeviceInfo&)> on_device_found);
    void stop_discovery();

    void start_server(int port);
    void stop_server();

    bool send_file(const std::string& ip, int port, const std::string& filepath);

private:
    std::atomic<bool> discovering_;
    std::atomic<bool> server_running_;
    std::thread discovery_thread_;
    std::thread server_thread_;
};

} // namespace network
} // namespace syncflow

#endif // NETWORK_MANAGER_HPP

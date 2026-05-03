#pragma once

#include <atomic>
#include <mutex>
#include <set>
#include <string>
#include <thread>

#include "syncflow/logging.h"
#include "syncflow/networking/peer_protocol.h"

namespace syncflow::networking {

class PeerNode {
public:
    explicit PeerNode(std::string device_name);
    void run();

private:
    std::string device_name_;
    std::string local_ip_;
    syncflow::Logger logger_;
    std::atomic_bool running_{true};
    std::atomic<int> tcp_server_fd_{-1};
    std::atomic<int> udp_listener_fd_{-1};
    std::thread tcp_thread_;
    std::thread udp_thread_;
    std::thread broadcast_thread_;
    std::set<std::string> active_connections_;
    std::mutex active_mutex_;

    void stop();
    void log_startup();
    void broadcast_loop();
    void udp_listener_loop();
    void tcp_server_loop();
    void connect_to_peer(PeerInfo peer);
    void handle_peer_connection(int fd, PeerInfo peer, const std::string& direction);
    bool should_initiate(const PeerInfo& peer) const;
    bool is_active(const PeerInfo& peer);
    void mark_active(const PeerInfo& peer);
    void mark_inactive(const PeerInfo& peer);
};

}  // namespace syncflow::networking

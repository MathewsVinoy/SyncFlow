#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>

#include "syncflow/file_sync/file_sync.h"
#include "syncflow/logging.h"
#include "syncflow/networking/peer_protocol.h"
#include "syncflow/networking/file_transfer.h"

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
    std::set<std::string> pending_connections_;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> last_connect_attempt_;
    syncflow::file_sync::FileSyncConfig file_sync_config_;
    std::mutex active_mutex_;
    std::mutex connect_mutex_;
    std::mutex share_mutex_;
    bool share_in_progress_{false};
    std::string share_peer_key_;

    void stop();
    void log_startup();
    void broadcast_loop();
    void udp_listener_loop();
    void tcp_server_loop();
    void connect_to_peer(PeerInfo peer);
    void handle_peer_connection(int fd, PeerInfo peer, const std::string& direction);
    void maybe_sync_file(int fd, const PeerInfo& peer);
    bool should_send_file_to_peer(const PeerInfo& peer) const;
    bool send_file_payload(int fd, const std::filesystem::path& path, std::uint64_t& bytes_sent);
    bool send_directory_payload(int fd, const std::filesystem::path& root_path, std::uint64_t& bytes_sent);
    bool receive_file_payload(int fd, const std::filesystem::path& output_path, std::uint64_t expected_size, std::uint64_t& bytes_received);
    bool should_initiate(const PeerInfo& peer) const;
    bool is_active(const PeerInfo& peer);
    bool should_attempt_connect(const PeerInfo& peer);
    void clear_pending_connect(const PeerInfo& peer);
    bool try_acquire_share_slot(const PeerInfo& peer);
    void release_share_slot(const PeerInfo& peer);
    void mark_active(const PeerInfo& peer);
    void mark_inactive(const PeerInfo& peer);
};

}  // namespace syncflow::networking

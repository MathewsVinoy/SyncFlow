#ifndef SYNC_ENGINE_HPP
#define SYNC_ENGINE_HPP

#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include "NetworkManager.hpp"
#include "CryptoUtils.hpp"

namespace syncflow {

enum class SyncState {
    IDLE,
    SYNCING,
    ERROR
};

class SyncEngine {
public:
    SyncEngine();
    ~SyncEngine();

    void add_sync_folder(const std::string& path);
    void set_device_name(const std::string& name);
    void set_remote_peer(const std::string& host, int port);
    void set_receive_dir(const std::filesystem::path& receive_dir);
    void start_sync();
    void stop_sync();
    
    SyncState get_status() const;
    float get_progress() const;
    std::string get_last_error() const;

    void resolve_conflict(const std::string& filepath, bool keep_local);

private:
    std::vector<std::string> sync_folders_;
    SyncState state_;
    float progress_;
    std::unique_ptr<network::NetworkManager> network_manager_;
    std::string device_name_;
    std::string remote_host_;
    int remote_port_;
    std::filesystem::path receive_dir_;
};

} // namespace syncflow

#endif // SYNC_ENGINE_HPP

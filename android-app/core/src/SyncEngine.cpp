#include "SyncEngine.hpp"
#include <chrono>
#include <iostream>
#include <thread>

namespace syncflow {

SyncEngine::SyncEngine() : state_(SyncState::IDLE), progress_(0.0f), remote_port_(45455) {
    network_manager_ = std::make_unique<network::NetworkManager>();
}

SyncEngine::~SyncEngine() {
    stop_sync();
}

void SyncEngine::add_sync_folder(const std::string& path) {
    sync_folders_.push_back(path);
}

void SyncEngine::set_device_name(const std::string& name) {
    device_name_ = name;
    if (network_manager_) {
        network_manager_->set_device_name(name);
    }
}

void SyncEngine::set_remote_peer(const std::string& host, int port) {
    remote_host_ = host;
    remote_port_ = port;
    if (network_manager_) {
        network_manager_->set_remote_peer(host, port);
    }
}

void SyncEngine::set_receive_dir(const std::filesystem::path& receive_dir) {
    receive_dir_ = receive_dir;
    if (network_manager_) {
        network_manager_->set_receive_dir(receive_dir);
    }
}

void SyncEngine::start_sync() {
    if (state_ == SyncState::SYNCING) return;
    state_ = SyncState::SYNCING;
    progress_ = 0.0f;

    if (network_manager_) {
        network_manager_->set_device_name(device_name_);
        network_manager_->set_remote_peer(remote_host_, remote_port_);
        network_manager_->set_receive_dir(receive_dir_);
    }

    std::thread([this]() {
        if (!network_manager_ || !network_manager_->connect_to_peer()) {
            state_ = SyncState::ERROR;
            return;
        }

        // Keep the session open while the network manager consumes the peer stream.
        while (state_ == SyncState::SYNCING) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            if (!network_manager_->is_connected()) {
                state_ = SyncState::ERROR;
                break;
            }
        }
    }).detach();
}

void SyncEngine::stop_sync() {
    state_ = SyncState::IDLE;
    if (network_manager_) {
        network_manager_->disconnect();
    }
}

SyncState SyncEngine::get_status() const {
    return state_;
}

float SyncEngine::get_progress() const {
    return progress_;
}

std::string SyncEngine::get_last_error() const {
    if (!network_manager_) {
        return "network manager not initialized";
    }
    return network_manager_->last_error();
}

void SyncEngine::resolve_conflict(const std::string& filepath, bool keep_local) {
    // Conflict resolution logic
}

} // namespace syncflow

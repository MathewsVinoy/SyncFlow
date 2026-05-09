#include "SyncEngine.hpp"
#include <iostream>

namespace syncflow {

SyncEngine::SyncEngine() : state_(SyncState::IDLE), progress_(0.0f) {
    network_manager_ = std::make_unique<network::NetworkManager>();
}

SyncEngine::~SyncEngine() {
    stop_sync();
}

void SyncEngine::add_sync_folder(const std::string& path) {
    sync_folders_.push_back(path);
}

void SyncEngine::start_sync() {
    if (state_ == SyncState::SYNCING) return;
    state_ = SyncState::SYNCING;
    progress_ = 0.0f;
    // Core sync logic would go here:
    // 1. Scan folders
    // 2. Hash files
    // 3. Compare with remote
    // 4. Transfer differences
}

void SyncEngine::stop_sync() {
    state_ = SyncState::IDLE;
}

SyncState SyncEngine::get_status() const {
    return state_;
}

float SyncEngine::get_progress() const {
    return progress_;
}

void SyncEngine::resolve_conflict(const std::string& filepath, bool keep_local) {
    // Conflict resolution logic
}

} // namespace syncflow

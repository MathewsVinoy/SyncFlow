// src/sync/sync_engine.cpp

#include <syncflow/sync/sync.h>
#include <syncflow/common/logger.h>
#include <memory>

namespace syncflow::sync {

class SyncEngine::Impl {
public:
    explicit Impl(const SyncFolderConfig& config) : config_(config) {}
    
    bool initialize() {
        LOG_INFO("SyncEngine", "Initializing sync for: " + config_.local_path);
        return true;
    }
    
    bool start() {
        LOG_INFO("SyncEngine", "Starting sync engine");
        return true;
    }
    
    bool stop() {
        LOG_INFO("SyncEngine", "Stopping sync engine");
        return true;
    }
    
    bool is_running() const {
        return false;
    }
    
private:
    SyncFolderConfig config_;
};

SyncEngine::SyncEngine(const SyncFolderConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}

SyncEngine::~SyncEngine() = default;

bool SyncEngine::initialize() {
    return impl_->initialize();
}

bool SyncEngine::start() {
    return impl_->start();
}

bool SyncEngine::stop() {
    return impl_->stop();
}

bool SyncEngine::is_running() const {
    return impl_->is_running();
}

bool SyncEngine::perform_sync() {
    LOG_INFO("SyncEngine", "Performing manual sync");
    return true;
}

bool SyncEngine::add_sync_folder(const SyncFolderConfig& config) {
    LOG_INFO("SyncEngine", "Adding sync folder: " + config.local_path);
    return true;
}

bool SyncEngine::remove_sync_folder(const std::string& local_path) {
    LOG_INFO("SyncEngine", "Removing sync folder: " + local_path);
    return true;
}

void SyncEngine::set_progress_callback(std::function<void(const std::string&, int)> cb) {
    // Store callback
}

void SyncEngine::set_conflict_callback(OnConflictDetected cb) {
    // Store callback
}

} // namespace syncflow::sync

// src/watcher/file_watcher.cpp

#include <syncflow/watcher/watcher.h>
#include <syncflow/common/logger.h>

namespace syncflow::watcher {

FileWatcher::FileWatcher(const std::string& path)
    : watch_path_(path), is_active_(false) {
    monitor_ = FileSystemMonitor::create();
}

FileWatcher::~FileWatcher() {
    stop();
}

bool FileWatcher::start(OnFileChange callback) {
    if (is_active_) {
        return false;
    }
    
    if (!monitor_) {
        LOG_ERROR("FileWatcher", "Monitor not initialized");
        return false;
    }
    
    if (!monitor_->watch_directory(watch_path_)) {
        LOG_ERROR("FileWatcher", "Failed to watch directory: " + watch_path_);
        return false;
    }
    
    monitor_->set_callback(callback);
    
    if (!monitor_->start()) {
        LOG_ERROR("FileWatcher", "Failed to start monitor");
        return false;
    }
    
    is_active_ = true;
    LOG_INFO("FileWatcher", "Started watching: " + watch_path_);
    return true;
}

bool FileWatcher::stop() {
    if (!is_active_) {
        return false;
    }
    
    if (monitor_) {
        monitor_->stop();
        monitor_->unwatch_directory(watch_path_);
    }
    
    is_active_ = false;
    LOG_INFO("FileWatcher", "Stopped watching: " + watch_path_);
    return true;
}

bool FileWatcher::is_active() const {
    return is_active_;
}

std::vector<FileChangeEvent> FileWatcher::get_recent_changes() {
    if (!monitor_) {
        return {};
    }
    return monitor_->get_changes();
}

void FileWatcher::clear_changes() {
    // Implementation depends on monitor
}

} // namespace syncflow::watcher

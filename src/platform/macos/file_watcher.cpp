// src/platform/macos/file_watcher.cpp

#include <syncflow/watcher/watcher.h>
#include <syncflow/common/logger.h>

#ifdef __APPLE__

namespace syncflow::watcher {

class macOSFSMonitor : public FileSystemMonitor {
public:
    bool watch_directory(const std::string& path) override {
        LOG_INFO("macOSFSMonitor", "watch_directory called for: " + path);
        return true;
    }
    
    bool unwatch_directory(const std::string& path) override {
        return true;
    }
    
    bool is_watching(const std::string& path) const override {
        return false;
    }
    
    bool start() override {
        return true;
    }
    
    bool stop() override {
        return true;
    }
    
    bool is_running() const override {
        return false;
    }
    
    std::vector<FileChangeEvent> get_changes() override {
        return {};
    }
    
    void set_callback(OnFileChange callback) override {}
};

std::unique_ptr<FileSystemMonitor> FileSystemMonitor::create() {
    return std::make_unique<macOSFSMonitor>();
}

} // namespace syncflow::watcher

#endif // __APPLE__

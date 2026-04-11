// src/platform/windows/file_watcher.cpp

#include <syncflow/watcher/watcher.h>
#include <syncflow/common/logger.h>

#ifdef _WIN32

namespace syncflow::watcher {

class WindowsFSMonitor : public FileSystemMonitor {
public:
    bool watch_directory(const std::string& path) override {
        LOG_INFO("WindowsFSMonitor", "watch_directory called for: " + path);
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
    return std::make_unique<WindowsFSMonitor>();
}

} // namespace syncflow::watcher

#endif // _WIN32

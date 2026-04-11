// src/platform/windows/file_watcher.cpp
// Windows file system monitoring using ReadDirectoryChangesW API

#include <syncflow/watcher/watcher.h>
#include <syncflow/common/logger.h>

#ifdef _WIN32

#include <windows.h>
#include <thread>
#include <map>
#include <mutex>

namespace syncflow::watcher {

class WindowsFSMonitor : public FileSystemMonitor {
private:
    struct DirectoryWatch {
        HANDLE dir_handle;
        std::string path;
        char change_buffer[4096];
    };
    
    std::map<std::string, DirectoryWatch> watched_dirs_;
    mutable std::mutex watch_mutex_;
    bool running_;
    std::thread monitor_thread_;
    OnFileChange callback_;
    
    void monitor_changes() {
        while (running_) {
            std::unique_lock<std::mutex> lock(watch_mutex_);
            
            for (auto& pair : watched_dirs_) {
                DirectoryWatch& watch = pair.second;
                DWORD bytes_returned = 0;
                
                if (ReadDirectoryChangesW(
                    watch.dir_handle,
                    watch.change_buffer,
                    sizeof(watch.change_buffer),
                    TRUE,  // Watch subdirectories
                    FILE_NOTIFY_CHANGE_FILE_NAME |
                    FILE_NOTIFY_CHANGE_DIR_NAME |
                    FILE_NOTIFY_CHANGE_LAST_WRITE |
                    FILE_NOTIFY_CHANGE_SIZE,
                    &bytes_returned,
                    NULL,
                    NULL)) {
                    
                    if (bytes_returned > 0) {
                        FILE_NOTIFY_INFORMATION* info = (FILE_NOTIFY_INFORMATION*)watch.change_buffer;
                        
                        while (info) {
                            FileChangeEvent event;
                            event.timestamp = std::chrono::system_clock::now();
                            
                            // Convert wide char to UTF-8
                            int size_needed = WideCharToMultiByte(CP_UTF8, 0, info->FileName,
                                                                   info->FileNameLength / 2,
                                                                   NULL, 0, NULL, NULL);
                            std::string filename(size_needed, 0);
                            WideCharToMultiByte(CP_UTF8, 0, info->FileName,
                                              info->FileNameLength / 2,
                                              &filename[0], size_needed, NULL, NULL);
                            
                            event.path = filename;
                            
                            // Map Windows notification types
                            switch (info->Action) {
                                case FILE_ACTION_ADDED:
                                    event.type = FileChangeType::CREATED;
                                    break;
                                case FILE_ACTION_REMOVED:
                                    event.type = FileChangeType::DELETED;
                                    break;
                                case FILE_ACTION_MODIFIED:
                                    event.type = FileChangeType::MODIFIED;
                                    break;
                                case FILE_ACTION_RENAMED_OLD_NAME:
                                    event.type = FileChangeType::DELETED;
                                    break;
                                case FILE_ACTION_RENAMED_NEW_NAME:
                                    event.type = FileChangeType::CREATED;
                                    break;
                                default:\n                                    event.type = FileChangeType::MODIFIED;
                            }
                            
                            if (callback_) {
                                callback_(event);
                            }
                            
                            if (info->NextEntryOffset == 0) break;
                            info = (FILE_NOTIFY_INFORMATION*)((char*)info + info->NextEntryOffset);
                        }
                    }
                }
            }
            
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

public:
    WindowsFSMonitor() : running_(false) {}
    
    ~WindowsFSMonitor() override {
        stop();
    }
    
    bool watch_directory(const std::string& path) override {
        std::unique_lock<std::mutex> lock(watch_mutex_);
        
        if (watched_dirs_.count(path) > 0) {
            return true;  // Already watching
        }
        
        HANDLE dir_handle = CreateFileA(
            path.c_str(),
            FILE_LIST_DIRECTORY,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED,
            NULL);
        
        if (dir_handle == INVALID_HANDLE_VALUE) {
            LOG_ERROR("WindowsFSMonitor", "Failed to open directory: " + path);
            return false;
        }
        
        DirectoryWatch watch;
        watch.dir_handle = dir_handle;
        watch.path = path;
        watched_dirs_[path] = watch;
        
        LOG_INFO("WindowsFSMonitor", "Now watching: " + path);
        return true;
    }
    
    bool unwatch_directory(const std::string& path) override {
        std::unique_lock<std::mutex> lock(watch_mutex_);
        
        auto it = watched_dirs_.find(path);
        if (it != watched_dirs_.end()) {
            CloseHandle(it->second.dir_handle);
            watched_dirs_.erase(it);
            LOG_INFO("WindowsFSMonitor", "Stopped watching: " + path);
            return true;
        }
        return false;
    }
    
    bool is_watching(const std::string& path) const override {
        std::unique_lock<std::mutex> lock(watch_mutex_);
        return watched_dirs_.count(path) > 0;
    }
    
    bool start() override {
        if (running_) return true;
        
        running_ = true;
        monitor_thread_ = std::thread(&WindowsFSMonitor::monitor_changes, this);
        LOG_INFO("WindowsFSMonitor", "Monitor started");
        return true;
    }
    
    bool stop() override {
        if (!running_) return true;
        
        running_ = false;
        if (monitor_thread_.joinable()) {
            monitor_thread_.join();
        }
        
        std::unique_lock<std::mutex> lock(watch_mutex_);
        for (auto& pair : watched_dirs_) {
            CloseHandle(pair.second.dir_handle);
        }
        watched_dirs_.clear();
        
        LOG_INFO("WindowsFSMonitor", "Monitor stopped");
        return true;
    }
    
    bool is_running() const override {
        return running_;
    }
    
    std::vector<FileChangeEvent> get_changes() override {
        return {};
    }
    
    void set_callback(OnFileChange callback) override {
        callback_ = callback;
    }
};

std::unique_ptr<FileSystemMonitor> FileSystemMonitor::create() {
    return std::make_unique<WindowsFSMonitor>();
}

} // namespace syncflow::watcher

#endif // _WIN32

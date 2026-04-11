// src/watcher/fs_monitor.cpp

#include <syncflow/watcher/watcher.h>
#include <syncflow/common/logger.h>
#include <memory>
#include <map>
#include <set>

#ifdef __linux__
    #include <sys/inotify.h>
    #include <poll.h>
    #include <unistd.h>
#elif defined(__APPLE__)
    #include <CoreServices/CoreServices.h>
#elif defined(_WIN32)
    #include <windows.h>
#endif

namespace syncflow::watcher {

#ifdef __linux__

class LinuxFSMonitor : public FileSystemMonitor {
public:
    LinuxFSMonitor() : inotify_fd_(-1), running_(false) {}
    
    ~LinuxFSMonitor() override {
        stop();
    }
    
    bool watch_directory(const std::string& path) override {
        if (inotify_fd_ == -1) {
            inotify_fd_ = inotify_init1(IN_NONBLOCK);
            if (inotify_fd_ == -1) {
                LOG_ERROR("LinuxFSMonitor", "Failed to initialize inotify");
                return false;
            }
        }
        
        int wd = inotify_add_watch(inotify_fd_, path.c_str(),
                                   IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_TO | IN_MOVED_FROM);
        
        if (wd == -1) {
            LOG_ERROR("LinuxFSMonitor", "Failed to add watch: " + path);
            return false;
        }
        
        watched_dirs_[wd] = path;
        return true;
    }
    
    bool unwatch_directory(const std::string& path) override {
        for (auto& pair : watched_dirs_) {
            if (pair.second == path) {
                inotify_rm_watch(inotify_fd_, pair.first);
                watched_dirs_.erase(pair.first);
                return true;
            }
        }
        return false;
    }
    
    bool is_watching(const std::string& path) const override {
        for (const auto& pair : watched_dirs_) {
            if (pair.second == path) {
                return true;
            }
        }
        return false;
    }
    
    bool start() override {
        running_ = true;
        return true;
    }
    
    bool stop() override {
        running_ = false;
        if (inotify_fd_ != -1) {
            close(inotify_fd_);
            inotify_fd_ = -1;
        }
        return true;
    }
    
    bool is_running() const override {
        return running_;
    }
    
    std::vector<FileChangeEvent> get_changes() override {
        std::vector<FileChangeEvent> events;
        
        if (inotify_fd_ == -1) {
            return events;
        }
        
        const size_t BUF_LEN = 4096;
        char buf[BUF_LEN] __attribute__((aligned(__alignof__(struct inotify_event))));
        
        ssize_t len = read(inotify_fd_, buf, BUF_LEN);
        if (len <= 0) {
            return events;
        }
        
        for (char* p = buf; p < buf + len; ) {
            const struct inotify_event* event = (const struct inotify_event*)p;
            
            FileChangeEvent change_event;
            change_event.timestamp = std::chrono::system_clock::now();
            
            if (event->len > 0) {
                change_event.path = event->name;
            }
            
            if (event->mask & IN_CREATE) {
                change_event.type = FileChangeType::CREATED;
            } else if (event->mask & IN_DELETE) {
                change_event.type = FileChangeType::DELETED;
            } else if (event->mask & IN_MODIFY) {
                change_event.type = FileChangeType::MODIFIED;
            } else if (event->mask & IN_MOVED_TO) {
                change_event.type = FileChangeType::CREATED;
            } else if (event->mask & IN_MOVED_FROM) {
                change_event.type = FileChangeType::DELETED;
            }
            
            events.push_back(change_event);
            
            p += sizeof(struct inotify_event) + event->len;
        }
        
        return events;
    }
    
    void set_callback(OnFileChange callback) override {
        callback_ = callback;
    }
    
private:
    int inotify_fd_;
    bool running_;
    std::map<int, std::string> watched_dirs_;
    OnFileChange callback_;
};

#endif

class DefaultFSMonitor : public FileSystemMonitor {
public:
    bool watch_directory(const std::string& path) override {
        watched_dirs_.insert(path);
        return true;
    }
    
    bool unwatch_directory(const std::string& path) override {
        return watched_dirs_.erase(path) > 0;
    }
    
    bool is_watching(const std::string& path) const override {
        return watched_dirs_.count(path) > 0;
    }
    
    bool start() override {
        running_ = true;
        return true;
    }
    
    bool stop() override {
        running_ = false;
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
    
private:
    bool running_ = false;
    std::set<std::string> watched_dirs_;
    OnFileChange callback_;
};

std::unique_ptr<FileSystemMonitor> FileSystemMonitor::create() {
#ifdef __linux__
    return std::make_unique<LinuxFSMonitor>();
#else
    return std::make_unique<DefaultFSMonitor>();
#endif
}

} // namespace syncflow::watcher

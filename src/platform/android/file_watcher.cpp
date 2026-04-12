// src/platform/android/file_watcher.cpp
// Android file watcher implementation using inotify (works in Termux)

#include <syncflow/watcher/watcher.h>
#include <syncflow/common/logger.h>

#ifdef __ANDROID__
    #include <sys/inotify.h>
    #include <poll.h>
    #include <unistd.h>
    #include <map>
    #include <set>

namespace syncflow::watcher {

class AndroidFSMonitor : public FileSystemMonitor {
public:
    AndroidFSMonitor() : inotify_fd_(-1), running_(false) {}
    
    ~AndroidFSMonitor() override {
        stop();
    }
    
    bool watch_directory(const std::string& path) override {
        if (inotify_fd_ == -1) {
            inotify_fd_ = inotify_init1(IN_NONBLOCK);
            if (inotify_fd_ == -1) {
                LOG_ERROR("AndroidFSMonitor", "Failed to initialize inotify");
                return false;
            }
        }
        
        int wd = inotify_add_watch(inotify_fd_, path.c_str(),
                                   IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_TO | IN_MOVED_FROM);
        
        if (wd == -1) {
            LOG_ERROR("AndroidFSMonitor", "Failed to add watch: " + path);
            return false;
        }
        
        watched_dirs_[wd] = path;
        LOG_INFO("AndroidFSMonitor", "Watching directory: " + path);
        return true;
    }
    
    bool unwatch_directory(const std::string& path) override {
        for (auto& pair : watched_dirs_) {
            if (pair.second == path) {
                inotify_rm_watch(inotify_fd_, pair.first);
                watched_dirs_.erase(pair.first);
                LOG_INFO("AndroidFSMonitor", "Stopped watching: " + path);
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
        LOG_INFO("AndroidFSMonitor", "File monitoring started");
        return true;
    }
    
    bool stop() override {
        running_ = false;
        if (inotify_fd_ != -1) {
            close(inotify_fd_);
            inotify_fd_ = -1;
        }
        LOG_INFO("AndroidFSMonitor", "File monitoring stopped");
        return true;
    }
    
    bool is_running() const override {
        return running_;
    }
    
    std::vector<FileChangeEvent> get_changes() override {
        std::vector<FileChangeEvent> events;
        
        if (inotify_fd_ == -1 || !running_) {
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
            change_event.path = watched_dirs_[event->wd];
            
            if (event->len > 0) {
                change_event.path += "/" + std::string(event->name);
            }
            
            if (event->mask & IN_CREATE) {
                change_event.event_type = FileChangeType::Created;
            } else if (event->mask & IN_DELETE) {
                change_event.event_type = FileChangeType::Deleted;
            } else if (event->mask & IN_MODIFY) {
                change_event.event_type = FileChangeType::Modified;
            } else if (event->mask & (IN_MOVED_TO | IN_MOVED_FROM)) {
                change_event.event_type = FileChangeType::Modified;
            }
            
            events.push_back(change_event);
            
            p += sizeof(struct inotify_event) + event->len;
        }
        
        return events;
    }

private:
    int inotify_fd_;
    bool running_;
    std::map<int, std::string> watched_dirs_;
};

std::unique_ptr<FileSystemMonitor> FileSystemMonitor::create() {
    return std::make_unique<AndroidFSMonitor>();
}

} // namespace syncflow::watcher

#endif // __ANDROID__

// include/syncflow/watcher/watcher.h
// File system monitoring and watching

#ifndef SYNCFLOW_WATCHER_H
#define SYNCFLOW_WATCHER_H

#include <syncflow/types.h>
#include <memory>
#include <vector>
#include <functional>
#include <string>

namespace syncflow::watcher {

/**
 * Abstract file system monitor interface
 */
class FileSystemMonitor {
public:
    virtual ~FileSystemMonitor() = default;
    
    virtual bool watch_directory(const std::string& path) = 0;
    virtual bool unwatch_directory(const std::string& path) = 0;
    virtual bool is_watching(const std::string& path) const = 0;
    
    virtual bool start() = 0;
    virtual bool stop() = 0;
    virtual bool is_running() const = 0;
    
    virtual std::vector<FileChangeEvent> get_changes() = 0;
    
    virtual void set_callback(OnFileChange callback) = 0;
    
    static std::unique_ptr<FileSystemMonitor> create();
};

/**
 * File watcher that tracks changes in sync folders
 */
class FileWatcher {
public:
    FileWatcher(const std::string& path);
    ~FileWatcher();
    
    bool start(OnFileChange callback);
    bool stop();
    bool is_active() const;
    
    std::vector<FileChangeEvent> get_recent_changes();
    void clear_changes();
    
private:
    std::string watch_path_;
    std::unique_ptr<FileSystemMonitor> monitor_;
    bool is_active_;
};

} // namespace syncflow::watcher

#endif // SYNCFLOW_WATCHER_H

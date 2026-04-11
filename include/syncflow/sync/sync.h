// include/syncflow/sync/sync.h
// Synchronization engine

#ifndef SYNCFLOW_SYNC_H
#define SYNCFLOW_SYNC_H

#include <syncflow/types.h>
#include <memory>
#include <vector>
#include <map>
#include <mutex>

namespace syncflow::sync {

/**
 * File manifest tracking
 */
class FileManifest {
public:
    FileManifest(const std::string& base_path);
    
    bool load_from_disk();
    bool save_to_disk();
    
    bool add_file(const FileMetadata& metadata);
    bool remove_file(const std::string& path);
    std::vector<FileMetadata> get_all_files() const;
    bool get_file_metadata(const std::string& path, FileMetadata& metadata) const;
    
    // Find changed files since last sync
    std::vector<FileMetadata> get_changes_since_last_sync();
    void mark_synced();
    
private:
    std::string base_path_;
    std::string manifest_file_;
    std::map<std::string, FileMetadata> files_;
    std::vector<std::string> deleted_files_;
    mutable std::mutex manifest_mutex_;
};

/**
 * Conflict resolution
 */
class ConflictResolver {
public:
    enum class ConflictType {
        MODIFIED_VS_MODIFIED,
        DELETED_VS_MODIFIED,
        CREATED_VS_CREATED,
    };
    
    static bool resolve_conflict(
        const FileMetadata& local_file,
        const FileMetadata& remote_file,
        ConflictResolution strategy,
        FileMetadata& resolution);
    
    static std::string create_versioned_filename(const std::string& original_path);
};

/**
 * Sync engine managing bidirectional sync
 */
class SyncEngine {
public:
    SyncEngine(const SyncFolderConfig& config);
    ~SyncEngine();
    
    bool initialize();
    bool start();
    bool stop();
    bool is_running() const;
    
    // Manual sync trigger
    bool perform_sync();
    
    // Configuration
    bool add_sync_folder(const SyncFolderConfig& config);
    bool remove_sync_folder(const std::string& local_path);
    
    // Callbacks
    void set_progress_callback(std::function<void(const std::string&, int)> cb);
    void set_conflict_callback(OnConflictDetected cb);
    
private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace syncflow::sync

#endif // SYNCFLOW_SYNC_H

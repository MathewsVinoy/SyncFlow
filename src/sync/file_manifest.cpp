// src/sync/file_manifest.cpp

#include <syncflow/sync/sync.h>

namespace syncflow::sync {

FileManifest::FileManifest(const std::string& base_path)
    : base_path_(base_path) {
    manifest_file_ = base_path_ + "/.syncflow_manifest";
}

bool FileManifest::load_from_disk() {
    // TODO: Implement loading manifest from disk
    return true;
}

bool FileManifest::save_to_disk() {
    // TODO: Implement saving manifest to disk
    return true;
}

bool FileManifest::add_file(const FileMetadata& metadata) {
    std::unique_lock<std::mutex> lock(manifest_mutex_);
    files_[metadata.path] = metadata;
    return true;
}

bool FileManifest::remove_file(const std::string& path) {
    std::unique_lock<std::mutex> lock(manifest_mutex_);
    deleted_files_.push_back(path);
    return files_.erase(path) > 0;
}

std::vector<FileMetadata> FileManifest::get_all_files() const {
    std::unique_lock<std::mutex> lock(manifest_mutex_);
    std::vector<FileMetadata> result;
    for (const auto& pair : files_) {
        result.push_back(pair.second);
    }
    return result;
}

bool FileManifest::get_file_metadata(const std::string& path, FileMetadata& metadata) const {
    std::unique_lock<std::mutex> lock(manifest_mutex_);
    auto it = files_.find(path);
    if (it != files_.end()) {
        metadata = it->second;
        return true;
    }
    return false;
}

std::vector<FileMetadata> FileManifest::get_changes_since_last_sync() {
    // TODO: Implement change detection
    return get_all_files();
}

void FileManifest::mark_synced() {
    // TODO: Update last sync timestamp
}

} // namespace syncflow::sync

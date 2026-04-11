// src/sync/conflict_resolver.cpp

#include <syncflow/sync/sync.h>
#include <syncflow/common/utils.h>

namespace syncflow::sync {

bool ConflictResolver::resolve_conflict(const FileMetadata& local_file,
                                        const FileMetadata& remote_file,
                                        ConflictResolution strategy,
                                        FileMetadata& resolution) {
    switch (strategy) {
        case ConflictResolution::OVERWRITE:
            resolution = remote_file;
            return true;
            
        case ConflictResolution::SKIP:
            resolution = local_file;
            return false;  // Don't sync
            
        case ConflictResolution::VERSION:
            resolution = remote_file;
            resolution.path = create_versioned_filename(remote_file.path);
            return true;
            
        case ConflictResolution::ASK_USER:
        default:
            return false;
    }
}

std::string ConflictResolver::create_versioned_filename(const std::string& original_path) {
    size_t dot_pos = original_path.find_last_of('.');
    std::string timestamp = utils::format_timestamp(utils::get_current_timestamp_ms());
    
    if (dot_pos != std::string::npos) {
        return original_path.substr(0, dot_pos) + "_" + timestamp + 
               original_path.substr(dot_pos);
    } else {
        return original_path + "_" + timestamp;
    }
}

} // namespace syncflow::sync

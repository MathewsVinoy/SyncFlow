#pragma once

#include <filesystem>
#include <string>

namespace syncflow::file_sync {

struct FileSyncConfig {
    bool enabled{false};
    std::filesystem::path source_path;
    std::filesystem::path receive_dir{"received"};
};

FileSyncConfig load_config(const std::filesystem::path& config_path);
bool is_enabled(const FileSyncConfig& config);
bool source_exists(const FileSyncConfig& config);
bool source_is_directory(const FileSyncConfig& config);

}  // namespace syncflow::file_sync

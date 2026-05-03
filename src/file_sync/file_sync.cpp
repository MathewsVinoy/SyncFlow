#include "syncflow/file_sync/file_sync.h"

#include <fstream>
#include <sstream>

namespace syncflow::file_sync {

namespace {

std::string read_all_text(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        return {};
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

std::filesystem::path resolve_relative(const std::filesystem::path& base_dir, const std::filesystem::path& value) {
    if (value.is_absolute()) {
        return value;
    }
    return base_dir / value;
}

std::string extract_string_value(const std::string& content, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    const auto key_pos = content.find(needle);
    if (key_pos == std::string::npos) {
        return {};
    }

    const auto colon_pos = content.find(':', key_pos);
    if (colon_pos == std::string::npos) {
        return {};
    }

    const auto first_quote = content.find('"', colon_pos + 1);
    if (first_quote == std::string::npos) {
        return {};
    }

    const auto second_quote = content.find('"', first_quote + 1);
    if (second_quote == std::string::npos) {
        return {};
    }

    return content.substr(first_quote + 1, second_quote - first_quote - 1);
}

bool extract_bool_value(const std::string& content, const std::string& key, bool default_value) {
    const std::string needle = "\"" + key + "\"";
    const auto key_pos = content.find(needle);
    if (key_pos == std::string::npos) {
        return default_value;
    }

    const auto colon_pos = content.find(':', key_pos);
    if (colon_pos == std::string::npos) {
        return default_value;
    }

    const auto value_pos = content.find_first_not_of(" \t\r\n", colon_pos + 1);
    if (value_pos == std::string::npos) {
        return default_value;
    }

    if (content.compare(value_pos, 4, "true") == 0) {
        return true;
    }
    if (content.compare(value_pos, 5, "false") == 0) {
        return false;
    }

    return default_value;
}

}  // namespace

FileSyncConfig load_config(const std::filesystem::path& config_path) {
    FileSyncConfig config;
    const std::string content = read_all_text(config_path);
    if (content.empty()) {
        return config;
    }

    const std::filesystem::path base_dir = config_path.has_parent_path() ? config_path.parent_path() : std::filesystem::current_path();

    config.enabled = extract_bool_value(content, "enabled", false);

    const std::string source = extract_string_value(content, "source_path");
    if (!source.empty()) {
        config.source_path = resolve_relative(base_dir, source);
    }

    const std::string receive_dir = extract_string_value(content, "receive_dir");
    if (!receive_dir.empty()) {
        config.receive_dir = resolve_relative(base_dir, receive_dir);
    }

    return config;
}

bool is_enabled(const FileSyncConfig& config) {
    return config.enabled && !config.source_path.empty();
}

bool source_exists(const FileSyncConfig& config) {
    return !config.source_path.empty() && std::filesystem::exists(config.source_path);
}

bool source_is_directory(const FileSyncConfig& config) {
    return source_exists(config) && std::filesystem::is_directory(config.source_path);
}

}  // namespace syncflow::file_sync

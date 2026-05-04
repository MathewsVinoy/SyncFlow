#pragma once

#include <atomic>
#include <string>
#include <filesystem>

namespace syncflow::platform {

std::string get_hostname();
std::string get_local_ipv4();
std::string timestamp_now();
void install_signal_handlers(std::atomic_bool& running);

// Cross-platform directory functions
std::filesystem::path get_config_dir();    // ~/.config/syncflow (Linux), ~/Library/Application Support/syncflow (macOS), %APPDATA%/syncflow (Windows)
std::filesystem::path get_cache_dir();     // ~/.cache/syncflow (Linux), ~/Library/Caches/syncflow (macOS), %LOCALAPPDATA%/syncflow (Windows)
std::filesystem::path get_home_dir();      // User's home directory

// Platform detection
bool is_windows();
bool is_macos();
bool is_linux();

}  // namespace syncflow::platform

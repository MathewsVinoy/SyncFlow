#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace platform {

/**
 * PlatformPaths: Cross-platform directory and file path handling
 * Provides OS-specific paths for configuration, logs, data, and cache directories
 * Follows platform conventions:
 *   - Linux/Unix: ~/.config/syncflow, ~/.cache/syncflow, ~/.local/share/syncflow
 *   - macOS: ~/Library/Application Support/syncflow, ~/Library/Logs/syncflow
 *   - Windows: %APPDATA%\syncflow, %LOCALAPPDATA%\syncflow, %TEMP%\syncflow
 */
class PlatformPaths {
public:
	// Initialize paths with application name (call once at startup)
	static void initialize(const std::string& appName = "syncflow");

	// Directory accessors (return existing or create directories)
	static std::optional<std::filesystem::path> getConfigDir();
	static std::optional<std::filesystem::path> getLogDir();
	static std::optional<std::filesystem::path> getDataDir();
	static std::optional<std::filesystem::path> getCacheDir();
	static std::optional<std::filesystem::path> getTempDir();

	// File path helpers
	static std::filesystem::path getConfigFile(const std::string& filename = "config.json");
	static std::filesystem::path getLogFile(const std::string& filename = "syncflow.log");
	static std::filesystem::path getDataFile(const std::string& filename);
	static std::filesystem::path getCacheFile(const std::string& filename);

	// Platform detection
	static bool isWindows();
	static bool isLinux();
	static bool isMacOS();

	// Environment and system info
	static std::string getOSName();
	static std::optional<std::string> getEnv(const std::string& name);

	// Home directory
	static std::optional<std::filesystem::path> getHomeDir();

private:
	static std::string appName_;
	static std::optional<std::filesystem::path> configDir_;
	static std::optional<std::filesystem::path> logDir_;
	static std::optional<std::filesystem::path> dataDir_;
	static std::optional<std::filesystem::path> cacheDir_;

	static std::optional<std::filesystem::path> getOrCreateDir(
		std::optional<std::filesystem::path>& cachedPath,
		const std::filesystem::path& defaultPath);
};

}  // namespace platform

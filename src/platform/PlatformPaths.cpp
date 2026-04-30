#include "platform/PlatformPaths.h"

#include <cstdlib>
#include <iostream>

#ifdef _WIN32
#include <shlobj.h>
#include <windows.h>
#else
#include <pwd.h>
#include <unistd.h>
#endif

namespace platform {

std::string PlatformPaths::appName_ = "syncflow";
std::optional<std::filesystem::path> PlatformPaths::configDir_;
std::optional<std::filesystem::path> PlatformPaths::logDir_;
std::optional<std::filesystem::path> PlatformPaths::dataDir_;
std::optional<std::filesystem::path> PlatformPaths::cacheDir_;

void PlatformPaths::initialize(const std::string& appName) {
	if (!appName.empty()) {
		if (appName_ != appName) {
			appName_ = appName;
			configDir_.reset();
			logDir_.reset();
			dataDir_.reset();
			cacheDir_.reset();
		}
	}
}

std::optional<std::filesystem::path> PlatformPaths::getHomeDir() {
#ifdef _WIN32
	const char* home = std::getenv("USERPROFILE");
	if (home) {
		return std::filesystem::path(home);
	}
#else
	const char* home = std::getenv("HOME");
	if (home) {
		return std::filesystem::path(home);
	}

	struct passwd* pw = getpwuid(getuid());
	if (pw) {
		return std::filesystem::path(pw->pw_dir);
	}
#endif
	return std::nullopt;
}

std::optional<std::filesystem::path> PlatformPaths::getConfigDir() {
	return getOrCreateDir(configDir_, [&]() -> std::filesystem::path {
#ifdef _WIN32
		const char* appData = std::getenv("APPDATA");
		if (appData) {
			return std::filesystem::path(appData) / appName_;
		}
		if (auto home = getHomeDir()) {
			return *home / "AppData" / "Roaming" / appName_;
		}
#elif __APPLE__
		if (auto home = getHomeDir()) {
			return *home / "Library" / "Application Support" / appName_;
		}
#else  // Linux and other POSIX
		const char* xdgConfig = std::getenv("XDG_CONFIG_HOME");
		if (xdgConfig && xdgConfig[0] != '\0') {
			return std::filesystem::path(xdgConfig) / appName_;
		}
		if (auto home = getHomeDir()) {
			return *home / ".config" / appName_;
		}
#endif
		return std::filesystem::path(".");
	}());
}

std::optional<std::filesystem::path> PlatformPaths::getLogDir() {
	return getOrCreateDir(logDir_, [&]() -> std::filesystem::path {
#ifdef _WIN32
		const char* localAppData = std::getenv("LOCALAPPDATA");
		if (localAppData) {
			return std::filesystem::path(localAppData) / appName_ / "logs";
		}
		if (auto home = getHomeDir()) {
			return *home / "AppData" / "Local" / appName_ / "logs";
		}
#elif __APPLE__
		if (auto home = getHomeDir()) {
			return *home / "Library" / "Logs" / appName_;
		}
#else  // Linux and other POSIX
		const char* xdgCache = std::getenv("XDG_CACHE_HOME");
		if (xdgCache && xdgCache[0] != '\0') {
			return std::filesystem::path(xdgCache) / appName_;
		}
		if (auto home = getHomeDir()) {
			return *home / ".cache" / appName_;
		}
#endif
		return std::filesystem::path(".");
	}());
}

std::optional<std::filesystem::path> PlatformPaths::getDataDir() {
	return getOrCreateDir(dataDir_, [&]() -> std::filesystem::path {
#ifdef _WIN32
		const char* localAppData = std::getenv("LOCALAPPDATA");
		if (localAppData) {
			return std::filesystem::path(localAppData) / appName_;
		}
		if (auto home = getHomeDir()) {
			return *home / "AppData" / "Local" / appName_;
		}
#elif __APPLE__
		if (auto home = getHomeDir()) {
			return *home / "Library" / "Application Support" / appName_;
		}
#else  // Linux and other POSIX
		const char* xdgData = std::getenv("XDG_DATA_HOME");
		if (xdgData && xdgData[0] != '\0') {
			return std::filesystem::path(xdgData) / appName_;
		}
		if (auto home = getHomeDir()) {
			return *home / ".local" / "share" / appName_;
		}
#endif
		return std::filesystem::path(".");
	}());
}

std::optional<std::filesystem::path> PlatformPaths::getCacheDir() {
	return getOrCreateDir(cacheDir_, [&]() -> std::filesystem::path {
#ifdef _WIN32
		const char* temp = std::getenv("TEMP");
		if (temp) {
			return std::filesystem::path(temp) / appName_;
		}
#elif __APPLE__
		if (auto home = getHomeDir()) {
			return *home / "Library" / "Caches" / appName_;
		}
#else  // Linux and other POSIX
		const char* xdgCache = std::getenv("XDG_CACHE_HOME");
		if (xdgCache && xdgCache[0] != '\0') {
			return std::filesystem::path(xdgCache) / appName_;
		}
		if (auto home = getHomeDir()) {
			return *home / ".cache" / appName_;
		}
#endif
		return std::filesystem::path("/tmp");
	}());
}

std::optional<std::filesystem::path> PlatformPaths::getTempDir() {
	std::error_code ec;
	auto temp = std::filesystem::temp_directory_path(ec);
	if (ec) {
		return getCacheDir();
	}
	return temp / appName_;
}

std::filesystem::path PlatformPaths::getConfigFile(const std::string& filename) {
	if (auto dir = getConfigDir()) {
		return *dir / filename;
	}
	return std::filesystem::path(filename);
}

std::filesystem::path PlatformPaths::getLogFile(const std::string& filename) {
	if (auto dir = getLogDir()) {
		return *dir / filename;
	}
	return std::filesystem::path(filename);
}

std::filesystem::path PlatformPaths::getDataFile(const std::string& filename) {
	if (auto dir = getDataDir()) {
		return *dir / filename;
	}
	return std::filesystem::path(filename);
}

std::filesystem::path PlatformPaths::getCacheFile(const std::string& filename) {
	if (auto dir = getCacheDir()) {
		return *dir / filename;
	}
	return std::filesystem::path(filename);
}

bool PlatformPaths::isWindows() {
#ifdef _WIN32
	return true;
#else
	return false;
#endif
}

bool PlatformPaths::isLinux() {
#ifdef __linux__
	return true;
#else
	return false;
#endif
}

bool PlatformPaths::isMacOS() {
#ifdef __APPLE__
	return true;
#else
	return false;
#endif
}

std::string PlatformPaths::getOSName() {
#ifdef _WIN32
	return "Windows";
#elif __APPLE__
	return "macOS";
#elif __linux__
	return "Linux";
#else
	return "Unknown";
#endif
}

std::optional<std::string> PlatformPaths::getEnv(const std::string& name) {
	const char* value = std::getenv(name.c_str());
	return value ? std::optional<std::string>(std::string(value)) : std::nullopt;
}

std::optional<std::filesystem::path> PlatformPaths::getOrCreateDir(
	std::optional<std::filesystem::path>& cachedPath,
	const std::filesystem::path& defaultPath) {
	if (cachedPath) {
		return cachedPath;
	}

	std::error_code ec;
	std::filesystem::create_directories(defaultPath, ec);
	if (ec) {
		std::cerr << "Failed to create directory: " << defaultPath << " (" << ec.message() << ")\n";
		return std::nullopt;
	}

	cachedPath = defaultPath;
	return cachedPath;
}

}  // namespace platform

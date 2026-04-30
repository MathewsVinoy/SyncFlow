#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

class ConfigManager {
public:
	using Value = std::variant<std::string, std::int64_t>;

	// Load from specified path or use platform-specific config directory
	bool load(const std::string& filePath = "");
	
	// Load from platform-specific config directory
	bool loadFromConfigDir(const std::string& filename = "config.json");

	// Path of the config file that was last loaded successfully
	std::optional<std::filesystem::path> loadedPath() const;

	bool has(const std::string& key) const;

	std::string getString(const std::string& key, const std::string& defaultValue = "") const;
	int getInt(const std::string& key, int defaultValue = 0) const;

private:
	std::unordered_map<std::string, Value> data_;
	std::optional<std::filesystem::path> loadedPath_;
	
	// Try to resolve file path in standard locations
	std::optional<std::filesystem::path> resolveConfigPath(const std::string& filePath);
};

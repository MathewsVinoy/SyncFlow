#include "core/ConfigManager.h"

#include "platform/PlatformPaths.h"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

std::optional<std::filesystem::path> ConfigManager::resolveConfigPath(const std::string& filePath) {
	if (filePath.empty()) {
		// Try to find config in standard locations
		auto configPath = platform::PlatformPaths::getConfigFile("config.json");
		if (std::filesystem::exists(configPath)) {
			return configPath;
		}

		// Fall back to current directory
		if (std::filesystem::exists("config.json")) {
			return std::filesystem::path("config.json");
		}

		return std::nullopt;
	}

	std::filesystem::path resolvedPath(filePath);

	if (resolvedPath.is_absolute()) {
		if (std::filesystem::exists(resolvedPath)) {
			return resolvedPath;
		}
		return std::nullopt;
	}

	// Try relative path first
	if (std::filesystem::exists(resolvedPath)) {
		return resolvedPath;
	}

	// Try in config directory
	auto configDirPath = platform::PlatformPaths::getConfigFile(filePath);
	if (std::filesystem::exists(configDirPath)) {
		return configDirPath;
	}

	// Try walking up directory tree from current path
	const auto filename = resolvedPath.filename();
	for (auto current = std::filesystem::current_path(); ; current = current.parent_path()) {
		const auto candidate = current / filename;
		if (std::filesystem::exists(candidate)) {
			return candidate;
		}

		if (current.has_parent_path() && current != current.parent_path()) {
			continue;
		}

		break;
	}

	return std::nullopt;
}

bool ConfigManager::load(const std::string& filePath) {
	auto resolvedPath = resolveConfigPath(filePath);
	if (!resolvedPath) {
		data_.clear();
		return false;
	}

	std::ifstream input(*resolvedPath);
	if (!input.is_open()) {
		data_.clear();
		return false;
	}

	try {
		json parsed = json::parse(input);
		if (!parsed.is_object()) {
			data_.clear();
			return false;
		}

		std::unordered_map<std::string, Value> next;
		for (auto it = parsed.begin(); it != parsed.end(); ++it) {
			if (it.value().is_string()) {
				next[it.key()] = it.value().get<std::string>();
			} else if (it.value().is_number_integer()) {
				next[it.key()] = it.value().get<std::int64_t>();
			} else if (it.value().is_number_unsigned()) {
				const auto v = it.value().get<std::uint64_t>();
				if (v <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
					next[it.key()] = static_cast<std::int64_t>(v);
				}
			}
		}

		data_ = std::move(next);
		return true;
	} catch (const json::exception&) {
		data_.clear();
		return false;
	}
}

bool ConfigManager::loadFromConfigDir(const std::string& filename) {
	auto configPath = platform::PlatformPaths::getConfigFile(filename);
	if (!std::filesystem::exists(configPath)) {
		data_.clear();
		return false;
	}

	std::ifstream input(configPath);
	if (!input.is_open()) {
		data_.clear();
		return false;
	}

	try {
		json parsed = json::parse(input);
		if (!parsed.is_object()) {
			data_.clear();
			return false;
		}

		std::unordered_map<std::string, Value> next;
		for (auto it = parsed.begin(); it != parsed.end(); ++it) {
			if (it.value().is_string()) {
				next[it.key()] = it.value().get<std::string>();
			} else if (it.value().is_number_integer()) {
				next[it.key()] = it.value().get<std::int64_t>();
			} else if (it.value().is_number_unsigned()) {
				const auto v = it.value().get<std::uint64_t>();
				if (v <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
					next[it.key()] = static_cast<std::int64_t>(v);
				}
			}
		}

		data_ = std::move(next);
		return true;
	} catch (const json::exception&) {
		data_.clear();
		return false;
	}
}

bool ConfigManager::has(const std::string& key) const {
	return data_.find(key) != data_.end();
}

std::string ConfigManager::getString(const std::string& key, const std::string& defaultValue) const {
	const auto it = data_.find(key);
	if (it == data_.end()) {
		return defaultValue;
	}

	if (const auto* value = std::get_if<std::string>(&it->second)) {
		return *value;
	}

	if (const auto* value = std::get_if<std::int64_t>(&it->second)) {
		return std::to_string(*value);
	}

	return defaultValue;
}

int ConfigManager::getInt(const std::string& key, int defaultValue) const {
	const auto it = data_.find(key);
	if (it == data_.end()) {
		return defaultValue;
	}

	if (const auto* value = std::get_if<std::int64_t>(&it->second)) {
		return static_cast<int>(*value);
	}

	if (const auto* value = std::get_if<std::string>(&it->second)) {
		int parsed = defaultValue;
		const char* begin = value->c_str();
		const char* end = begin + value->size();
		const auto result = std::from_chars(begin, end, parsed);
		if (result.ec == std::errc{}) {
			return parsed;
		}
	}

	return defaultValue;
}


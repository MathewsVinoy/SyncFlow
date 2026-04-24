#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>

class ConfigManager {
public:
	using Value = std::variant<std::string, std::int64_t>;

	bool load(const std::string& filePath = "config.json");
	bool has(const std::string& key) const;

	std::string getString(const std::string& key, const std::string& defaultValue = "") const;
	int getInt(const std::string& key, int defaultValue = 0) const;

private:
	std::unordered_map<std::string, Value> data_;
};

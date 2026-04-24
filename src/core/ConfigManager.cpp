#include "core/ConfigManager.h"

#include <charconv>
#include <filesystem>
#include <fstream>
#include <limits>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

bool ConfigManager::load(const std::string& filePath) {
	std::filesystem::path resolvedPath(filePath);
	if (!resolvedPath.is_absolute() && !std::filesystem::exists(resolvedPath)) {
		const auto filename = resolvedPath.filename();
		for (auto current = std::filesystem::current_path(); ; current = current.parent_path()) {
			const auto candidate = current / filename;
			if (std::filesystem::exists(candidate)) {
				resolvedPath = candidate;
				break;
			}

			if (current.has_parent_path() && current != current.parent_path()) {
				continue;
			}

			data_.clear();
			return false;
		}
	}

	std::ifstream input(resolvedPath);
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

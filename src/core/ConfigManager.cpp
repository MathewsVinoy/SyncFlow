#include "core/ConfigManager.h"

#include <cctype>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace {
void skipWhitespace(const std::string& text, std::size_t& pos) {
	while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
		++pos;
	}
}

bool consume(const std::string& text, std::size_t& pos, char expected) {
	skipWhitespace(text, pos);
	if (pos >= text.size() || text[pos] != expected) {
		return false;
	}
	++pos;
	return true;
}
}  // namespace

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

	std::ostringstream buffer;
	buffer << input.rdbuf();

	std::unordered_map<std::string, Value> parsed;
	if (!parseJsonObject(buffer.str(), parsed)) {
		data_.clear();
		return false;
	}

	data_ = std::move(parsed);
	return true;
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

std::string ConfigManager::trim(const std::string& text) {
	std::size_t start = 0;
	std::size_t end = text.size();

	while (start < end && std::isspace(static_cast<unsigned char>(text[start]))) {
		++start;
	}
	while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
		--end;
	}

	return text.substr(start, end - start);
}

bool ConfigManager::parseStringToken(const std::string& text, std::size_t& pos, std::string& out) {
	if (pos >= text.size() || text[pos] != '"') {
		return false;
	}

	++pos;
	out.clear();

	while (pos < text.size()) {
		const char ch = text[pos++];
		if (ch == '"') {
			return true;
		}
		if (ch == '\\' && pos < text.size()) {
			const char escaped = text[pos++];
			switch (escaped) {
				case '"': out.push_back('"'); break;
				case '\\': out.push_back('\\'); break;
				case '/': out.push_back('/'); break;
				case 'b': out.push_back('\b'); break;
				case 'f': out.push_back('\f'); break;
				case 'n': out.push_back('\n'); break;
				case 'r': out.push_back('\r'); break;
				case 't': out.push_back('\t'); break;
				default: out.push_back(escaped); break;
			}
		} else {
			out.push_back(ch);
		}
	}

	return false;
}

bool ConfigManager::parseNumberToken(const std::string& text, std::size_t& pos, std::int64_t& out) {
	skipWhitespace(text, pos);
	const std::size_t start = pos;

	if (pos < text.size() && (text[pos] == '-' || text[pos] == '+')) {
		++pos;
	}
	while (pos < text.size() && std::isdigit(static_cast<unsigned char>(text[pos]))) {
		++pos;
	}

	if (start == pos) {
		return false;
	}

	const std::string number = text.substr(start, pos - start);
	const auto result = std::from_chars(number.data(), number.data() + number.size(), out);
	return result.ec == std::errc{};
}

bool ConfigManager::parseJsonObject(const std::string& text, std::unordered_map<std::string, Value>& out) {
	out.clear();

	std::size_t pos = 0;
	skipWhitespace(text, pos);
	if (!consume(text, pos, '{')) {
		return false;
	}

	skipWhitespace(text, pos);
	if (pos < text.size() && text[pos] == '}') {
		return true;
	}

	while (pos < text.size()) {
		std::string key;
		if (!parseStringToken(text, pos, key)) {
			return false;
		}

		if (!consume(text, pos, ':')) {
			return false;
		}

		skipWhitespace(text, pos);
		if (pos >= text.size()) {
			return false;
		}

		if (text[pos] == '"') {
			std::string value;
			if (!parseStringToken(text, pos, value)) {
				return false;
			}
			out[key] = std::move(value);
		} else {
			std::int64_t number = 0;
			if (!parseNumberToken(text, pos, number)) {
				return false;
			}
			out[key] = number;
		}

		skipWhitespace(text, pos);
		if (pos < text.size() && text[pos] == ',') {
			++pos;
			continue;
		}

		if (pos < text.size() && text[pos] == '}') {
			++pos;
			return true;
		}

		return false;
	}

	return false;
}

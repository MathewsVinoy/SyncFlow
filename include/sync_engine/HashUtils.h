#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>

namespace syncflow::hash {

inline std::uint64_t fnv1a64(const void* data, std::size_t size, std::uint64_t seed = 1469598103934665603ULL) {
	const auto* ptr = static_cast<const unsigned char*>(data);
	std::uint64_t value = seed;
	for (std::size_t i = 0; i < size; ++i) {
		value ^= static_cast<std::uint64_t>(ptr[i]);
		value *= 1099511628211ULL;
	}
	return value;
}

inline std::string toHex(std::uint64_t value) {
	std::ostringstream out;
	out << std::hex << std::setfill('0') << std::setw(16) << value;
	return out.str();
}

inline std::string hashFileFNV1a64(const std::filesystem::path& path) {
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open()) {
		return {};
	}

	std::array<char, 64 * 1024> buffer{};
	std::uint64_t hash = 1469598103934665603ULL;
	while (in.good()) {
		in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
		const auto n = static_cast<std::size_t>(in.gcount());
		if (n == 0) {
			break;
		}
		hash = fnv1a64(buffer.data(), n, hash);
	}

	return toHex(hash);
}

}  // namespace syncflow::hash

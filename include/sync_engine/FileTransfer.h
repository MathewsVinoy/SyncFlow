#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace syncflow::engine {

struct TransferChunk {
	std::uint64_t offset = 0;
	std::vector<std::byte> bytes;
	bool last = false;
};

class TransferStateStore {
public:
	explicit TransferStateStore(std::filesystem::path root);

	std::uint64_t loadOffset(const std::string& transferId) const;
	bool saveOffset(const std::string& transferId, std::uint64_t offset) const;
	void clear(const std::string& transferId) const;

private:
	std::filesystem::path root_;
};

class FileTransfer {
public:
	explicit FileTransfer(std::size_t chunkSize = 256 * 1024);

	std::optional<TransferChunk> readChunk(const std::filesystem::path& file, std::uint64_t offset) const;
	bool writeChunk(const std::filesystem::path& file, const TransferChunk& chunk) const;
	std::uint64_t fileSize(const std::filesystem::path& file) const;

private:
	std::size_t chunkSize_;
};

}  // namespace syncflow::engine

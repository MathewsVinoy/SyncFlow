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

	// Read a chunk from a file starting at given offset
	std::optional<TransferChunk> readChunk(const std::filesystem::path& file, std::uint64_t offset) const;

	// Write a chunk to a file, seeking to proper offset (for resumable transfers)
	bool writeChunk(const std::filesystem::path& file, const TransferChunk& chunk) const;

	// Write a chunk to temporary file with safe append semantics
	bool writeChunkToTemporary(const std::filesystem::path& tempFile,
	                           const TransferChunk& chunk,
	                           bool verifyOffset = true) const;

	// Complete a transfer by renaming temp file to final destination atomically
	bool completeTransfer(const std::filesystem::path& tempFile,
	                      const std::filesystem::path& finalFile,
	                      bool verifySize = true,
	                      std::uint64_t expectedSize = 0) const;

	// Verify a file matches expected size and optionally its hash
	bool verifyFile(const std::filesystem::path& file,
	               std::uint64_t expectedSize,
	               const std::string& expectedHash = "") const;

	// Calculate SHA256 hash of a file
	std::string calculateFileHash(const std::filesystem::path& file) const;

	std::uint64_t fileSize(const std::filesystem::path& file) const;

private:
	std::size_t chunkSize_;
};

}  // namespace syncflow::engine

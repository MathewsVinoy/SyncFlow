#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace syncflow::engine {

struct BlockDescriptor {
	std::uint64_t index = 0;
	std::uint64_t offset = 0;
	std::uint64_t size = 0;
	std::string hash;
};

struct BlockIndexEntry {
	std::string relativePath;
	bool isDirectory = false;
	bool deleted = false;
	std::uint64_t size = 0;
	std::int64_t modifiedUnixSeconds = 0;
	std::uint32_t permissions = 0;
	std::string contentHash;
	std::vector<BlockDescriptor> blocks;
};

struct BlockIndex {
	std::string rootPath;
	std::uint64_t blockSize = 128 * 1024;
	std::vector<BlockIndexEntry> entries;
};

enum class BlockTransferKind {
	CreateDirectory,
	DeleteEntry,
	TransferBlock
};

struct BlockTransferStep {
	BlockTransferKind kind = BlockTransferKind::TransferBlock;
	std::string relativePath;
	std::uint64_t blockIndex = 0;
	std::uint64_t offset = 0;
	std::uint64_t size = 0;
	std::string hash;
};

class BlockIndexStore {
public:
	explicit BlockIndexStore(std::filesystem::path storagePath, std::uint64_t blockSize = 128 * 1024);

	BlockIndex scan(const std::filesystem::path& root) const;
	bool save(const BlockIndex& index) const;
	std::optional<BlockIndex> load() const;

	std::vector<BlockTransferStep> planDelta(const BlockIndex& local, const BlockIndex& remote) const;

	std::string encode(const BlockIndex& index) const;
	std::optional<BlockIndex> decode(const std::string& payload) const;

	const std::filesystem::path& storagePath() const;
	std::uint64_t blockSize() const;

private:
	std::filesystem::path storagePath_;
	std::uint64_t blockSize_;

	static std::string normalizeRelativePath(const std::filesystem::path& root,
	                                       const std::filesystem::path& absolutePath);
	static std::int64_t toUnixSeconds(std::filesystem::file_time_type timePoint);
	static std::string sha256Hex(const std::vector<std::byte>& bytes);
	static std::string sha256Hex(const std::vector<std::string>& parts);
	static std::uint32_t permissionsToMask(std::filesystem::perms perms);

	static std::vector<std::filesystem::path> sortedDirectoryEntries(const std::filesystem::path& root);
	static std::vector<std::filesystem::path> parentDirectories(const std::filesystem::path& root,
	                                                          const std::filesystem::path& relativePath);
	static BlockIndexEntry makeDirectoryEntry(const std::string& relativePath,
	                                        const std::filesystem::directory_entry& entry);
	BlockIndexEntry makeFileEntry(const std::filesystem::path& root,
	                            const std::filesystem::directory_entry& entry) const;
};

}  // namespace syncflow::engine

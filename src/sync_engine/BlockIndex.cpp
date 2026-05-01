#include "sync_engine/BlockIndex.h"

#include <algorithm>
#include <cstddef>
#include <array>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <sstream>
#include <unordered_map>
#include <system_error>

namespace syncflow::engine {
namespace {
using Json = nlohmann::json;

std::string sha256Bytes(const std::byte* data, std::size_t size) {
	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int digestLen = 0;

	auto* ctx = EVP_MD_CTX_new();
	if (ctx == nullptr) {
		return {};
	}

	if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
		EVP_MD_CTX_free(ctx);
		return {};
	}
	if (size > 0 && EVP_DigestUpdate(ctx, data, size) != 1) {
		EVP_MD_CTX_free(ctx);
		return {};
	}
	if (EVP_DigestFinal_ex(ctx, digest, &digestLen) != 1) {
		EVP_MD_CTX_free(ctx);
		return {};
	}
	EVP_MD_CTX_free(ctx);

	std::ostringstream out;
	out << std::hex << std::setfill('0');
	for (unsigned int i = 0; i < digestLen; ++i) {
		out << std::setw(2) << static_cast<int>(digest[i]);
	}
	return out.str();
}

std::string sha256Strings(const std::vector<std::string>& parts) {
	auto* ctx = EVP_MD_CTX_new();
	if (ctx == nullptr) {
		return {};
	}

	if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
		EVP_MD_CTX_free(ctx);
		return {};
	}

	for (const auto& part : parts) {
		if (EVP_DigestUpdate(ctx, part.data(), part.size()) != 1) {
			EVP_MD_CTX_free(ctx);
			return {};
		}
	}

	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int digestLen = 0;
	if (EVP_DigestFinal_ex(ctx, digest, &digestLen) != 1) {
		EVP_MD_CTX_free(ctx);
		return {};
	}
	EVP_MD_CTX_free(ctx);

	std::ostringstream out;
	out << std::hex << std::setfill('0');
	for (unsigned int i = 0; i < digestLen; ++i) {
		out << std::setw(2) << static_cast<int>(digest[i]);
	}
	return out.str();
}

bool isRelativeChild(const std::filesystem::path& root, const std::filesystem::path& candidate) {
	std::error_code ec;
	auto canonicalRoot = std::filesystem::weakly_canonical(root, ec);
	if (ec) {
		canonicalRoot = std::filesystem::absolute(root, ec);
	}
	auto canonicalCandidate = std::filesystem::weakly_canonical(candidate, ec);
	if (ec) {
		canonicalCandidate = std::filesystem::absolute(candidate, ec);
	}
	const auto rootString = canonicalRoot.generic_string();
	const auto candidateString = canonicalCandidate.generic_string();
	return candidateString.rfind(rootString, 0) == 0;
}
}  // namespace

BlockIndexStore::BlockIndexStore(std::filesystem::path storagePath, std::uint64_t blockSize)
	: storagePath_(std::move(storagePath)), blockSize_(blockSize == 0 ? 128 * 1024 : blockSize) {}

const std::filesystem::path& BlockIndexStore::storagePath() const {
	return storagePath_;
}

std::uint64_t BlockIndexStore::blockSize() const {
	return blockSize_;
}

BlockIndex BlockIndexStore::scan(const std::filesystem::path& root) const {
	BlockIndex index;
	index.rootPath = std::filesystem::absolute(root).generic_string();
	index.blockSize = blockSize_;

	std::error_code ec;
	if (!std::filesystem::exists(root, ec)) {
		return index;
	}

	for (const auto& path : sortedDirectoryEntries(root)) {
		std::filesystem::directory_entry entry(path, ec);
		if (ec) {
			continue;
		}

		if (!isRelativeChild(root, path)) {
			continue;
		}

		const auto relativePath = normalizeRelativePath(root, path);
		if (relativePath.empty()) {
			continue;
		}

		if (entry.is_directory(ec)) {
			index.entries.push_back(makeDirectoryEntry(relativePath, entry));
		} else if (entry.is_regular_file(ec)) {
			index.entries.push_back(makeFileEntry(root, entry));
		}
	}

	std::sort(index.entries.begin(), index.entries.end(), [](const BlockIndexEntry& lhs, const BlockIndexEntry& rhs) {
		if (lhs.isDirectory != rhs.isDirectory) {
			return lhs.isDirectory > rhs.isDirectory;
		}
		return lhs.relativePath < rhs.relativePath;
	});

	return index;
}

bool BlockIndexStore::save(const BlockIndex& index) const {
	std::error_code ec;
	std::filesystem::create_directories(storagePath_.parent_path(), ec);
	if (ec) {
		return false;
	}

	std::ofstream out(storagePath_, std::ios::trunc);
	if (!out.is_open()) {
		return false;
	}

	out << encode(index);
	return static_cast<bool>(out);
}

std::optional<BlockIndex> BlockIndexStore::load() const {
	std::ifstream in(storagePath_);
	if (!in.is_open()) {
		return std::nullopt;
	}

	std::stringstream buffer;
	buffer << in.rdbuf();
	return decode(buffer.str());
}

std::vector<BlockTransferStep> BlockIndexStore::planDelta(const BlockIndex& local,
                                                          const BlockIndex& remote) const {
	std::vector<BlockTransferStep> steps;
	std::unordered_map<std::string, const BlockIndexEntry*> remoteEntries;
	remoteEntries.reserve(remote.entries.size());
	for (const auto& entry : remote.entries) {
		remoteEntries.emplace(entry.relativePath, &entry);
	}

	for (const auto& entry : local.entries) {
		auto remoteIt = remoteEntries.find(entry.relativePath);
		const BlockIndexEntry* remoteEntry = remoteIt == remoteEntries.end() ? nullptr : remoteIt->second;
		if (remoteEntry != nullptr && remoteEntry->deleted) {
			remoteEntry = nullptr;
		}

		if (entry.deleted) {
			steps.push_back(BlockTransferStep{BlockTransferKind::DeleteEntry, entry.relativePath, 0, 0, 0, {}});
			continue;
		}

		if (entry.isDirectory) {
			if (remoteEntry == nullptr || !remoteEntry->isDirectory) {
				steps.push_back(BlockTransferStep{BlockTransferKind::CreateDirectory, entry.relativePath, 0, 0, 0, {}});
			}
			continue;
		}

		if (remoteEntry != nullptr && !remoteEntry->isDirectory && remoteEntry->contentHash == entry.contentHash) {
			continue;
		}

		if (remoteEntry != nullptr && remoteEntry->isDirectory) {
			steps.push_back(BlockTransferStep{BlockTransferKind::DeleteEntry, entry.relativePath, 0, 0, 0, {}});
		}

		for (const auto& block : entry.blocks) {
			const BlockDescriptor* remoteBlock = nullptr;
			if (remoteEntry != nullptr) {
				auto it = std::find_if(remoteEntry->blocks.begin(), remoteEntry->blocks.end(), [&](const BlockDescriptor& candidate) {
					return candidate.index == block.index;
				});
				if (it != remoteEntry->blocks.end()) {
					remoteBlock = &*it;
				}
			}

			if (remoteBlock == nullptr || remoteBlock->hash != block.hash || remoteBlock->size != block.size) {
				steps.push_back(BlockTransferStep{BlockTransferKind::TransferBlock,
				                                  entry.relativePath,
				                                  block.index,
				                                  block.offset,
				                                  block.size,
				                                  block.hash});
			}
		}
	}

	return steps;
}

std::string BlockIndexStore::encode(const BlockIndex& index) const {
	Json root;
	root["rootPath"] = index.rootPath;
	root["blockSize"] = index.blockSize;
	root["entries"] = Json::array();

	for (const auto& entry : index.entries) {
		Json item;
		item["relativePath"] = entry.relativePath;
		item["isDirectory"] = entry.isDirectory;
		item["deleted"] = entry.deleted;
		item["size"] = entry.size;
		item["modifiedUnixSeconds"] = entry.modifiedUnixSeconds;
		item["permissions"] = entry.permissions;
		item["contentHash"] = entry.contentHash;
		item["blocks"] = Json::array();
		for (const auto& block : entry.blocks) {
			Json blockJson;
			blockJson["index"] = block.index;
			blockJson["offset"] = block.offset;
			blockJson["size"] = block.size;
			blockJson["hash"] = block.hash;
			item["blocks"].push_back(std::move(blockJson));
		}
		root["entries"].push_back(std::move(item));
	}

	return root.dump(2);
}

std::optional<BlockIndex> BlockIndexStore::decode(const std::string& payload) const {
	try {
		const auto json = Json::parse(payload);
		BlockIndex index;
		index.rootPath = json.value("rootPath", std::string{});
		index.blockSize = json.value("blockSize", blockSize_);

		const auto& entries = json.at("entries");
		for (const auto& item : entries) {
			BlockIndexEntry entry;
			entry.relativePath = item.value("relativePath", std::string{});
			entry.isDirectory = item.value("isDirectory", false);
			entry.deleted = item.value("deleted", false);
			entry.size = item.value("size", std::uint64_t{0});
			entry.modifiedUnixSeconds = item.value("modifiedUnixSeconds", std::int64_t{0});
			entry.permissions = item.value("permissions", std::uint32_t{0});
			entry.contentHash = item.value("contentHash", std::string{});

			const auto& blocks = item.at("blocks");
			for (const auto& blockJson : blocks) {
				BlockDescriptor block;
				block.index = blockJson.value("index", std::uint64_t{0});
				block.offset = blockJson.value("offset", std::uint64_t{0});
				block.size = blockJson.value("size", std::uint64_t{0});
				block.hash = blockJson.value("hash", std::string{});
				entry.blocks.push_back(std::move(block));
			}

			index.entries.push_back(std::move(entry));
		}

		return index;
	} catch (...) {
		return std::nullopt;
	}
}

std::string BlockIndexStore::normalizeRelativePath(const std::filesystem::path& root,
                                                  const std::filesystem::path& absolutePath) {
	std::error_code ec;
	auto rel = std::filesystem::relative(absolutePath, root, ec);
	if (ec) {
		return {};
	}
	return rel.lexically_normal().generic_string();
}

std::int64_t BlockIndexStore::toUnixSeconds(std::filesystem::file_time_type timePoint) {
	using namespace std::chrono;
	const auto systemNow = system_clock::now();
	const auto fileNow = std::filesystem::file_time_type::clock::now();
	const auto adjusted = timePoint - fileNow + systemNow;
	return duration_cast<seconds>(adjusted.time_since_epoch()).count();
}

std::string BlockIndexStore::sha256Hex(const std::vector<std::byte>& bytes) {
	return sha256Bytes(bytes.data(), bytes.size());
}

std::string BlockIndexStore::sha256Hex(const std::vector<std::string>& parts) {
	return sha256Strings(parts);
}

std::uint32_t BlockIndexStore::permissionsToMask(std::filesystem::perms perms) {
	return static_cast<std::uint32_t>(perms);
}

std::vector<std::filesystem::path> BlockIndexStore::sortedDirectoryEntries(const std::filesystem::path& root) {
	std::vector<std::filesystem::path> entries;
	std::error_code ec;
	for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
	     it != end && !ec;
	     ++it) {
		entries.push_back(it->path());
	}
	std::sort(entries.begin(), entries.end());
	return entries;
}

std::vector<std::filesystem::path> BlockIndexStore::parentDirectories(const std::filesystem::path& root,
                                                                     const std::filesystem::path& relativePath) {
	std::vector<std::filesystem::path> parents;
	const auto parent = (root / relativePath).parent_path();
	if (parent.empty() || parent == root) {
		return parents;
	}

	std::filesystem::path current = parent;
	std::vector<std::filesystem::path> reverse;
	while (!current.empty() && current != root && current.string().size() >= root.string().size()) {
		reverse.push_back(normalizeRelativePath(root, current));
		if (current == current.parent_path()) {
			break;
		}
		current = current.parent_path();
	}
	for (auto it = reverse.rbegin(); it != reverse.rend(); ++it) {
		if (!it->empty()) {
			parents.push_back(*it);
		}
	}
	return parents;
}

BlockIndexEntry BlockIndexStore::makeDirectoryEntry(const std::string& relativePath,
                                                   const std::filesystem::directory_entry& entry) {
	std::error_code ec;
	const auto status = entry.status(ec);
	BlockIndexEntry result;
	result.relativePath = relativePath;
	result.isDirectory = true;
	result.deleted = false;
	result.size = 0;
	result.modifiedUnixSeconds = 0;
	result.permissions = ec ? 0 : permissionsToMask(status.permissions());
	return result;
}

BlockIndexEntry BlockIndexStore::makeFileEntry(const std::filesystem::path& root,
                                              const std::filesystem::directory_entry& entry) const {
	std::error_code ec;
	BlockIndexEntry result;
	result.relativePath = normalizeRelativePath(root, entry.path());
	result.isDirectory = false;
	result.deleted = false;
	result.size = entry.file_size(ec);
	result.modifiedUnixSeconds = ec ? 0 : toUnixSeconds(entry.last_write_time(ec));
	result.permissions = permissionsToMask(entry.status(ec).permissions());

	std::ifstream in(entry.path(), std::ios::binary);
	if (!in.is_open()) {
		return result;
	}

	std::vector<std::string> blockHashes;
	std::vector<std::byte> buffer(blockSize_ == 0 ? 128 * 1024 : static_cast<std::size_t>(blockSize_));
	std::uint64_t offset = 0;
	std::uint64_t index = 0;
	while (in.good()) {
		in.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
		const auto n = static_cast<std::size_t>(in.gcount());
		if (n == 0) {
			break;
		}

		buffer.resize(n);
		const auto blockHash = sha256Hex(buffer);
		result.blocks.push_back(BlockDescriptor{index, offset, static_cast<std::uint64_t>(n), blockHash});
		blockHashes.push_back(blockHash);
		offset += static_cast<std::uint64_t>(n);
		++index;
		buffer.resize(blockSize_ == 0 ? 128 * 1024 : static_cast<std::size_t>(blockSize_));
	}

	blockHashes.push_back(result.relativePath);
	blockHashes.push_back(std::to_string(result.size));
	result.contentHash = sha256Hex(blockHashes);
	return result;
}

}  // namespace syncflow::engine

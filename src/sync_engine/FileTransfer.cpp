#include "sync_engine/FileTransfer.h"

#include <array>
#include <fstream>
#include <system_error>

namespace syncflow::engine {

TransferStateStore::TransferStateStore(std::filesystem::path root) : root_(std::move(root)) {}

std::uint64_t TransferStateStore::loadOffset(const std::string& transferId) const {
	std::ifstream in(root_ / (transferId + ".resume"));
	if (!in.is_open()) {
		return 0;
	}

	std::uint64_t offset = 0;
	in >> offset;
	return offset;
}

bool TransferStateStore::saveOffset(const std::string& transferId, std::uint64_t offset) const {
	std::error_code ec;
	std::filesystem::create_directories(root_, ec);
	if (ec) {
		return false;
	}

	std::ofstream out(root_ / (transferId + ".resume"), std::ios::trunc);
	if (!out.is_open()) {
		return false;
	}
	out << offset;
	return static_cast<bool>(out);
}

void TransferStateStore::clear(const std::string& transferId) const {
	std::error_code ec;
	std::filesystem::remove(root_ / (transferId + ".resume"), ec);
}

FileTransfer::FileTransfer(std::size_t chunkSize) : chunkSize_(chunkSize == 0 ? 256 * 1024 : chunkSize) {}

std::optional<TransferChunk> FileTransfer::readChunk(const std::filesystem::path& file, std::uint64_t offset) const {
	std::ifstream in(file, std::ios::binary);
	if (!in.is_open()) {
		return std::nullopt;
	}

	in.seekg(0, std::ios::end);
	const auto totalSize = static_cast<std::uint64_t>(in.tellg());
	if (offset >= totalSize) {
		return TransferChunk{offset, {}, true};
	}

	in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
	std::vector<std::byte> bytes(chunkSize_);
	in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	const auto n = static_cast<std::size_t>(in.gcount());
	bytes.resize(n);

	const bool last = (offset + n) >= totalSize;
	return TransferChunk{offset, std::move(bytes), last};
}

bool FileTransfer::writeChunk(const std::filesystem::path& file, const TransferChunk& chunk) const {
	std::error_code ec;
	std::filesystem::create_directories(file.parent_path(), ec);
	if (ec) {
		return false;
	}

	std::fstream out(file, std::ios::binary | std::ios::in | std::ios::out);
	if (!out.is_open()) {
		out.open(file, std::ios::binary | std::ios::out);
		if (!out.is_open()) {
			return false;
		}
	}

	out.seekp(static_cast<std::streamoff>(chunk.offset), std::ios::beg);
	out.write(reinterpret_cast<const char*>(chunk.bytes.data()), static_cast<std::streamsize>(chunk.bytes.size()));
	return static_cast<bool>(out);
}

std::uint64_t FileTransfer::fileSize(const std::filesystem::path& file) const {
	std::error_code ec;
	const auto sz = std::filesystem::file_size(file, ec);
	if (ec) {
		return 0;
	}
	return sz;
}

}  // namespace syncflow::engine

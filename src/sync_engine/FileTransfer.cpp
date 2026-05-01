#include "sync_engine/FileTransfer.h"
#include "core/Logger.h"

#include <array>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

// OpenSSL for SHA256 (use EVP API)
#include <openssl/evp.h>

namespace syncflow::engine {
namespace {
	std::vector<std::byte> rleCompress(const std::vector<std::byte>& input) {
		std::vector<std::byte> out;
		if (input.empty()) {
			return out;
		}

		out.reserve(input.size() + 4);
		out.push_back(std::byte{'R'});
		out.push_back(std::byte{'L'});
		out.push_back(std::byte{'E'});
		out.push_back(std::byte{'1'});

		for (std::size_t i = 0; i < input.size();) {
			const auto value = input[i];
			std::size_t run = 1;
			while (i + run < input.size() && input[i + run] == value && run < 255) {
				++run;
			}
			out.push_back(static_cast<std::byte>(run));
			out.push_back(value);
			i += run;
		}

		return out;
	}

	std::optional<std::vector<std::byte>> rleDecompress(const std::vector<std::byte>& input) {
		if (input.size() < 4 || input[0] != std::byte{'R'} || input[1] != std::byte{'L'} ||
		    input[2] != std::byte{'E'} || input[3] != std::byte{'1'}) {
			return std::nullopt;
		}

		std::vector<std::byte> out;
		for (std::size_t i = 4; i + 1 < input.size(); i += 2) {
			const auto run = static_cast<unsigned char>(input[i]);
			out.insert(out.end(), run, input[i + 1]);
		}
		return out;
	}
}  // namespace

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

std::optional<TransferChunk> FileTransfer::readChunk(const std::filesystem::path& file,
	                                                 std::uint64_t offset,
	                                                 bool allowCompression) const {
	std::ifstream in(file, std::ios::binary);
	if (!in.is_open()) {
		Logger::error("FileTransfer: Failed to open file for reading - " + file.string());
		return std::nullopt;
	}

	in.seekg(0, std::ios::end);
	const auto totalSize = static_cast<std::uint64_t>(in.tellg());
	if (offset >= totalSize) {
		Logger::debug("FileTransfer: Offset beyond file size - file: " + file.string() +
		              ", offset: " + std::to_string(offset) + ", size: " + std::to_string(totalSize));
		return TransferChunk{offset, {}, true, false};
	}

	in.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
	std::vector<std::byte> bytes(chunkSize_);
	in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
	const auto n = static_cast<std::size_t>(in.gcount());
	bytes.resize(n);

	const bool last = (offset + n) >= totalSize;
	Logger::debug("FileTransfer: Read chunk - file: " + file.string() + ", offset: " + std::to_string(offset) +
	              ", size: " + std::to_string(n) + ", last: " + (last ? "true" : "false"));
	TransferChunk chunk{offset, std::move(bytes), last, false};
	if (allowCompression) {
		const auto compressed = rleCompress(chunk.bytes);
		if (!compressed.empty() && compressed.size() < chunk.bytes.size()) {
			chunk.bytes = std::move(compressed);
			chunk.compressed = true;
		}
	}
	return chunk;
}

bool FileTransfer::writeChunk(const std::filesystem::path& file, const TransferChunk& chunk) const {
	std::error_code ec;
	std::filesystem::create_directories(file.parent_path(), ec);
	if (ec) {
		Logger::error("FileTransfer: Failed to create parent directories - " + file.string());
		return false;
	}

	std::fstream out(file, std::ios::binary | std::ios::in | std::ios::out);
	if (!out.is_open()) {
		out.open(file, std::ios::binary | std::ios::out);
		if (!out.is_open()) {
			Logger::error("FileTransfer: Failed to open file for writing - " + file.string());
			return false;
		}
	}

	std::vector<std::byte> payload = chunk.bytes;
	if (chunk.compressed) {
		auto decoded = rleDecompress(chunk.bytes);
		if (!decoded.has_value()) {
			Logger::error("FileTransfer: Failed to decompress chunk - " + file.string());
			return false;
		}
		payload = std::move(*decoded);
	}

	out.seekp(static_cast<std::streamoff>(chunk.offset), std::ios::beg);
	out.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
	if (!out) {
		Logger::error("FileTransfer: Failed to write chunk to file - " + file.string());
		return false;
	}

	Logger::debug("FileTransfer: Wrote chunk - file: " + file.string() + ", offset: " + std::to_string(chunk.offset) +
	              ", size: " + std::to_string(payload.size()) + (chunk.compressed ? ", compressed" : ""));
	return true;
}

bool FileTransfer::writeChunkToTemporary(const std::filesystem::path& tempFile,
                                        const TransferChunk& chunk,
                                        bool verifyOffset) const {
	std::error_code ec;
	std::filesystem::create_directories(tempFile.parent_path(), ec);
	if (ec) {
		Logger::error("FileTransfer: Failed to create parent directories for temp file - " + tempFile.string());
		return false;
	}

	// Open in append mode to ensure sequential writes
	std::fstream out(tempFile, std::ios::binary | std::ios::app);
	if (!out.is_open()) {
		// If append fails, try creating new file
		out.open(tempFile, std::ios::binary | std::ios::out);
		if (!out.is_open()) {
			Logger::error("FileTransfer: Failed to open temporary file for writing - " + tempFile.string());
			return false;
		}
	}

	// If verifying offset, check that file size matches chunk offset
	if (verifyOffset) {
		std::error_code sizeEc;
		const auto currentSize = std::filesystem::file_size(tempFile, sizeEc);
		if (!sizeEc && currentSize != chunk.offset) {
			Logger::warn("FileTransfer: Temporary file offset mismatch - file: " + tempFile.string() +
			             ", current size: " + std::to_string(currentSize) +
			             ", chunk offset: " + std::to_string(chunk.offset));
			// Don't fail here - just log the mismatch. The caller may handle partial writes.
		}
	}

	// Write chunk data at EOF (append mode)
	std::vector<std::byte> payload = chunk.bytes;
	if (chunk.compressed) {
		auto decoded = rleDecompress(chunk.bytes);
		if (!decoded.has_value()) {
			Logger::error("FileTransfer: Failed to decompress temporary chunk - " + tempFile.string());
			return false;
		}
		payload = std::move(*decoded);
	}
	out.write(reinterpret_cast<const char*>(payload.data()), static_cast<std::streamsize>(payload.size()));
	out.flush();

	if (!out) {
		Logger::error("FileTransfer: Failed to write chunk to temporary file - " + tempFile.string());
		return false;
	}

	Logger::debug("FileTransfer: Wrote chunk to temporary file - file: " + tempFile.string() +
	              ", chunk size: " + std::to_string(payload.size()) + (chunk.compressed ? ", compressed" : ""));
	return true;
}

bool FileTransfer::completeTransfer(const std::filesystem::path& tempFile,
                                   const std::filesystem::path& finalFile,
                                   bool verifySize,
                                   std::uint64_t expectedSize) const {
	if (!std::filesystem::exists(tempFile)) {
		Logger::error("FileTransfer: Temporary file does not exist - " + tempFile.string());
		return false;
	}

	// Verify file size if requested
	if (verifySize && expectedSize > 0) {
		std::error_code ec;
		const auto tempSize = std::filesystem::file_size(tempFile, ec);
		if (ec || tempSize != expectedSize) {
			Logger::error("FileTransfer: Transfer incomplete - size mismatch. File: " + tempFile.string() +
			              ", current size: " + std::to_string(tempSize) +
			              ", expected size: " + std::to_string(expectedSize));
			return false;
		}
	}

	// Create parent directories
	std::error_code ec;
	std::filesystem::create_directories(finalFile.parent_path(), ec);
	if (ec) {
		Logger::error("FileTransfer: Failed to create final directory - " + finalFile.string());
		return false;
	}

	// Atomic rename from temp to final location
	std::error_code renameEc;
	std::filesystem::rename(tempFile, finalFile, renameEc);
	if (renameEc) {
		Logger::error("FileTransfer: Failed to rename temp file to final location - temp: " + tempFile.string() +
		              ", final: " + finalFile.string() + ", error: " + renameEc.message());
		return false;
	}

	Logger::info("FileTransfer: Transfer completed and file moved to final location - " + finalFile.string());
	return true;
}

bool FileTransfer::verifyFile(const std::filesystem::path& file,
                             std::uint64_t expectedSize,
                             const std::string& expectedHash) const {
	std::error_code ec;
	if (!std::filesystem::exists(file, ec)) {
		Logger::warn("FileTransfer: File does not exist for verification - " + file.string());
		return false;
	}

	const auto actualSize = std::filesystem::file_size(file, ec);
	if (ec || actualSize != expectedSize) {
		Logger::warn("FileTransfer: File size mismatch - file: " + file.string() +
		             ", actual: " + std::to_string(actualSize) +
		             ", expected: " + std::to_string(expectedSize));
		return false;
	}

	if (!expectedHash.empty()) {
		const auto actualHash = calculateFileHash(file);
		if (actualHash != expectedHash) {
			Logger::warn("FileTransfer: File hash mismatch - file: " + file.string() +
			             ", actual: " + actualHash +
			             ", expected: " + expectedHash);
			return false;
		}
		Logger::debug("FileTransfer: File hash verified - file: " + file.string());
	}

	return true;
}

std::string FileTransfer::calculateFileHash(const std::filesystem::path& file) const {
	std::ifstream in(file, std::ios::binary);
	if (!in.is_open()) {
		Logger::error("FileTransfer: Failed to open file for hashing - " + file.string());
		return "";
	}

	unsigned char hash[EVP_MAX_MD_SIZE];
	unsigned int hashLen = 0;

	EVP_MD_CTX* mdctx = EVP_MD_CTX_new();
	if (!mdctx) {
		Logger::error("FileTransfer: Failed to create EVP_MD_CTX for hashing - " + file.string());
		return std::string();
	}
	if (EVP_DigestInit_ex(mdctx, EVP_sha256(), nullptr) != 1) {
		EVP_MD_CTX_free(mdctx);
		Logger::error("FileTransfer: EVP_DigestInit_ex failed - " + file.string());
		return std::string();
	}

	std::array<char, 65536> buffer{};
	while (in.read(buffer.data(), buffer.size()) || in.gcount() > 0) {
		if (EVP_DigestUpdate(mdctx, buffer.data(), static_cast<size_t>(in.gcount())) != 1) {
			EVP_MD_CTX_free(mdctx);
			Logger::error("FileTransfer: EVP_DigestUpdate failed - " + file.string());
			return std::string();
		}
	}

	if (EVP_DigestFinal_ex(mdctx, hash, &hashLen) != 1) {
		EVP_MD_CTX_free(mdctx);
		Logger::error("FileTransfer: EVP_DigestFinal_ex failed - " + file.string());
		return std::string();
	}
	EVP_MD_CTX_free(mdctx);

	// Convert hash to hex string
	std::ostringstream hashStream;
	for (unsigned int i = 0; i < hashLen; ++i) {
		hashStream << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
	}

	const auto hashStr = hashStream.str();
	Logger::debug("FileTransfer: Calculated file hash - file: " + file.string() +
	              ", hash: " + hashStr);
	return hashStr;
}

std::uint64_t FileTransfer::fileSize(const std::filesystem::path& file) const {
	std::error_code ec;
	const auto sz = std::filesystem::file_size(file, ec);
	if (ec) {
		Logger::warn("FileTransfer: Failed to get file size - " + file.string() +
		             ", error: " + ec.message());
		return 0;
	}
	return sz;
}

}  // namespace syncflow::engine

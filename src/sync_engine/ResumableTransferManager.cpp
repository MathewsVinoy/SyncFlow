#include "sync_engine/ResumableTransferManager.h"
#include "core/Logger.h"

#include <chrono>
#include <fstream>
#include <sstream>
#include <system_error>

namespace syncflow::engine {

ResumableTransferManager::ResumableTransferManager(std::filesystem::path resumeRoot,
                                                   std::filesystem::path tempRoot)
	: resumeRoot_(std::move(resumeRoot)), tempRoot_(std::move(tempRoot)) {
	// Ensure directories exist
	std::error_code ec;
	std::filesystem::create_directories(resumeRoot_, ec);
	std::filesystem::create_directories(tempRoot_, ec);
	if (ec) {
		Logger::warn("ResumableTransferManager: Failed to create directories - " + ec.message());
	}
	reloadStateCache();
}

std::string ResumableTransferManager::generateTransferId(const std::string& filePath,
                                                        const std::string& remoteDeviceId,
                                                        bool isDownload) const {
	// Generate ID: direction_deviceId_hash(filePath)
	// Example: "down_device123_a1b2c3d4"
	std::hash<std::string> hasher;
	const auto pathHashValue = hasher(filePath);
	std::ostringstream oss;
	oss << std::hex << pathHashValue;
	const auto truncatedHash = oss.str().substr(0, 8);
	const auto direction = isDownload ? "down" : "up";
	return direction + "_" + remoteDeviceId + "_" + truncatedHash;
}

std::optional<TransferState> ResumableTransferManager::loadTransferState(
	const std::string& transferId) const {
	{
		std::lock_guard<std::mutex> lock(stateMutex_);
		auto it = stateCache_.find(transferId);
		if (it != stateCache_.end()) {
			Logger::debug("ResumableTransferManager: Found transfer in cache - " + transferId);
			return it->second;
		}
	}

	// Try loading from disk
	auto state = loadFromDisk(transferId);
	if (state) {
		std::lock_guard<std::mutex> lock(stateMutex_);
		stateCache_[transferId] = *state;
		Logger::debug("ResumableTransferManager: Loaded transfer state from disk - " + transferId);
	}
	return state;
}

bool ResumableTransferManager::saveTransferState(const TransferState& state) const {
	std::error_code ec;
	std::filesystem::create_directories(resumeRoot_, ec);
	if (ec) {
		Logger::error("ResumableTransferManager: Failed to create resume directory - " + ec.message());
		return false;
	}

	const auto stateFile = getStateFilePath(state.transferId);
	const std::string serialized = serializeState(state);

	std::ofstream out(stateFile, std::ios::trunc);
	if (!out.is_open()) {
		Logger::error("ResumableTransferManager: Failed to open state file - " + stateFile.string());
		return false;
	}

	out << serialized;
	if (!out) {
		Logger::error("ResumableTransferManager: Failed to write state file - " + stateFile.string());
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(stateMutex_);
		stateCache_[state.transferId] = state;
	}

	Logger::debug("ResumableTransferManager: Saved transfer state - " + state.transferId +
	              " (transferred: " + std::to_string(state.bytesTransferred) + " / " +
	              std::to_string(state.fileSize) + " bytes)");
	return true;
}

std::uint64_t ResumableTransferManager::getResumeOffset(const std::string& transferId) const {
	auto state = loadTransferState(transferId);
	if (!state) {
		return 0;
	}
	return state->bytesTransferred;
}

bool ResumableTransferManager::updateTransferProgress(const std::string& transferId,
                                                      std::uint64_t bytesTransferred) const {
	auto state = loadTransferState(transferId);
	if (!state) {
		Logger::warn("ResumableTransferManager: Cannot update progress for unknown transfer - " +
		             transferId);
		return false;
	}

	state->bytesTransferred = bytesTransferred;
	state->lastUpdatedAt = std::filesystem::file_time_type::clock::now();
	return saveTransferState(*state);
}

std::filesystem::path ResumableTransferManager::getTemporaryFilePath(
	const std::string& transferId) const {
	return tempRoot_ / (transferId + ".tmp");
}

std::filesystem::path ResumableTransferManager::getFinalFilePath(const std::string& transferId,
                                                                const std::string& destinationRoot) const {
	auto state = loadTransferState(transferId);
	if (!state) {
		return {};
	}
	return std::filesystem::path(destinationRoot) / state->filePath;
}

bool ResumableTransferManager::canResumeTransfer(const std::string& transferId) const {
	auto state = loadTransferState(transferId);
	if (!state) {
		return false;
	}

	// Check if temp file exists
	const auto tempFile = getTemporaryFilePath(transferId);
	if (!std::filesystem::exists(tempFile)) {
		Logger::warn("ResumableTransferManager: Temp file missing for resumable transfer - " +
		             transferId);
		return false;
	}

	// Verify temp file size matches saved progress
	std::error_code ec;
	const auto tempSize = std::filesystem::file_size(tempFile, ec);
	if (ec || tempSize != state->bytesTransferred) {
		Logger::warn("ResumableTransferManager: Temp file size mismatch for transfer - " +
		             transferId + " (temp: " + std::to_string(tempSize) +
		             ", expected: " + std::to_string(state->bytesTransferred) + ")");
		return false;
	}

	return true;
}

bool ResumableTransferManager::completeTransfer(const std::string& transferId,
                                               const std::string& destinationRoot,
                                               bool verifyChecksum,
                                               const std::string& expectedHash) const {
	Logger::info("ResumableTransferManager: Completing transfer - " + transferId);

	auto state = loadTransferState(transferId);
	if (!state) {
		Logger::error("ResumableTransferManager: Cannot complete unknown transfer - " + transferId);
		return false;
	}

	const auto tempFile = getTemporaryFilePath(transferId);
	if (!std::filesystem::exists(tempFile)) {
		Logger::error("ResumableTransferManager: Temp file not found for completion - " +
		              transferId);
		return false;
	}

	// Verify file size matches
	std::error_code ec;
	const auto tempSize = std::filesystem::file_size(tempFile, ec);
	if (ec || tempSize != state->fileSize) {
		Logger::error("ResumableTransferManager: Transfer incomplete - size mismatch for " +
		              transferId + " (temp: " + std::to_string(tempSize) +
		              ", expected: " + std::to_string(state->fileSize) + ")");
		return false;
	}

	const auto finalFile = std::filesystem::path(destinationRoot) / state->filePath;
	std::error_code renameEc;

	// Create parent directories for final location
	std::filesystem::create_directories(finalFile.parent_path(), renameEc);
	if (renameEc) {
		Logger::error("ResumableTransferManager: Failed to create final directory for " + transferId);
		return false;
	}

	// Atomic rename from temp to final location
	std::filesystem::rename(tempFile, finalFile, renameEc);
	if (renameEc) {
		Logger::error("ResumableTransferManager: Failed to rename temp file to final location - " +
		              transferId + ": " + renameEc.message());
		return false;
	}

	// Clean up state file
	abortTransfer(transferId);

	Logger::info("ResumableTransferManager: Transfer completed and file moved to final location - " +
	             finalFile.string());
	return true;
}

bool ResumableTransferManager::abortTransfer(const std::string& transferId) const {
	Logger::warn("ResumableTransferManager: Aborting transfer - " + transferId);

	const auto stateFile = getStateFilePath(transferId);
	const auto tempFile = getTemporaryFilePath(transferId);

	std::error_code ec;
	std::filesystem::remove(stateFile, ec);
	std::filesystem::remove(tempFile, ec);

	{
		std::lock_guard<std::mutex> lock(stateMutex_);
		stateCache_.erase(transferId);
	}

	Logger::debug("ResumableTransferManager: Transfer aborted and files cleaned up - " + transferId);
	return true;
}

std::optional<std::string> ResumableTransferManager::findExistingTransfer(
	const std::string& filePath,
	const std::string& remoteDeviceId,
	bool isDownload) const {
	const auto transferId = generateTransferId(filePath, remoteDeviceId, isDownload);
	const auto state = loadTransferState(transferId);
	if (!state) {
		return std::nullopt;
	}
	Logger::debug("ResumableTransferManager: Found existing transfer for duplicate check - " +
	              transferId);
	return transferId;
}

std::size_t ResumableTransferManager::cleanupStaleTransfers(int retentionDays) const {
	Logger::info("ResumableTransferManager: Cleaning up stale transfers (older than " +
	             std::to_string(retentionDays) + " days)");

	std::size_t cleanedCount = 0;
	const auto now = std::filesystem::file_time_type::clock::now();
	const auto retentionPeriod =
		std::chrono::hours(24 * retentionDays);

	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(resumeRoot_, ec)) {
		if (!entry.is_regular_file()) {
			continue;
		}

		const auto lastWrite = std::filesystem::last_write_time(entry, ec);
		if (ec) {
			continue;
		}

		const auto age = now - lastWrite;
		if (age > retentionPeriod) {
			std::filesystem::remove(entry, ec);
			const auto tempFile = tempRoot_ / entry.path().filename();
			tempFile.replace_extension(".tmp");
			std::filesystem::remove(tempFile, ec);
			cleanedCount++;
			Logger::debug("ResumableTransferManager: Cleaned up stale transfer - " +
			              entry.path().filename().string());
		}
	}

	Logger::info("ResumableTransferManager: Cleanup complete - removed " + std::to_string(cleanedCount) +
	             " stale transfers");
	return cleanedCount;
}

std::vector<TransferState> ResumableTransferManager::getActiveTransfers() const {
	std::lock_guard<std::mutex> lock(stateMutex_);
	std::vector<TransferState> active;
	for (const auto& [id, state] : stateCache_) {
		if (state.bytesTransferred < state.fileSize) {
			active.push_back(state);
		}
	}
	return active;
}

void ResumableTransferManager::reloadStateCache() const {
	std::lock_guard<std::mutex> lock(stateMutex_);
	stateCache_.clear();

	std::error_code ec;
	for (const auto& entry : std::filesystem::directory_iterator(resumeRoot_, ec)) {
		if (!entry.is_regular_file() || entry.path().extension() != ".resume") {
			continue;
		}

		auto state = parseStateFile(entry.path());
		if (state) {
			stateCache_[state->transferId] = *state;
		}
	}
}

std::filesystem::path ResumableTransferManager::getStateFilePath(
	const std::string& transferId) const {
	return resumeRoot_ / (transferId + ".resume");
}

std::optional<TransferState> ResumableTransferManager::parseStateFile(
	const std::filesystem::path& filePath) const {
	std::ifstream in(filePath);
	if (!in.is_open()) {
		return std::nullopt;
	}

	std::string line;
	TransferState state;

	// Read state file format:
	// transferId|filePath|fileSize|bytesTransferred|createdAt|lastUpdatedAt|sourceDeviceId|destinationDeviceId|contentHash|isDownload
	std::vector<std::string> tokens;
	while (std::getline(in, line)) {
		if (line.empty()) continue;
		std::stringstream ss(line);
		std::string token;
		while (std::getline(ss, token, '|')) {
			tokens.push_back(token);
		}
	}

	if (tokens.size() < 10) {
		return std::nullopt;
	}

	try {
		state.transferId = tokens[0];
		state.filePath = tokens[1];
		state.fileSize = std::stoull(tokens[2]);
		state.bytesTransferred = std::stoull(tokens[3]);
		state.sourceDeviceId = tokens[6];
		state.destinationDeviceId = tokens[7];
		state.contentHash = tokens[8];
		state.isDownload = tokens[9] == "1";
	} catch (...) {
		return std::nullopt;
	}

	return state;
}

std::string ResumableTransferManager::serializeState(const TransferState& state) const {
	std::ostringstream out;
	out << state.transferId << '|'
	    << state.filePath << '|'
	    << state.fileSize << '|'
	    << state.bytesTransferred << '|'
	    << state.createdAt.time_since_epoch().count() << '|'
	    << state.lastUpdatedAt.time_since_epoch().count() << '|'
	    << state.sourceDeviceId << '|'
	    << state.destinationDeviceId << '|'
	    << state.contentHash << '|'
	    << (state.isDownload ? 1 : 0);
	return out.str();
}

std::optional<TransferState> ResumableTransferManager::loadFromDisk(
	const std::string& transferId) const {
	const auto stateFile = getStateFilePath(transferId);
	if (!std::filesystem::exists(stateFile)) {
		return std::nullopt;
	}
	return parseStateFile(stateFile);
}

}  // namespace syncflow::engine

#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace syncflow::engine {

// Represents the state of an ongoing or paused transfer
struct TransferState {
	std::string transferId;
	std::string filePath;
	std::uint64_t fileSize = 0;
	std::uint64_t bytesTransferred = 0;
	std::filesystem::file_time_type createdAt;
	std::filesystem::file_time_type lastUpdatedAt;
	std::string sourceDeviceId;
	std::string destinationDeviceId;
	std::string contentHash;  // For verifying transfer integrity
	bool isDownload = true;   // true for download, false for upload
};

// Manages per-file transfer state, temporary files, and resumability
class ResumableTransferManager {
public:
	// Initialize with root path for storing temporary files and resume metadata
	explicit ResumableTransferManager(std::filesystem::path resumeRoot,
	                                  std::filesystem::path tempRoot);

	// Generate a unique transfer ID for a given file path and remote device
	std::string generateTransferId(const std::string& filePath,
	                               const std::string& remoteDeviceId,
	                               bool isDownload) const;

	// Load transfer state for resuming an interrupted transfer
	std::optional<TransferState> loadTransferState(const std::string& transferId) const;

	// Save transfer state to persist progress
	bool saveTransferState(const TransferState& state) const;

	// Get the current byte offset for resuming a transfer
	std::uint64_t getResumeOffset(const std::string& transferId) const;

	// Update the progress of a transfer
	bool updateTransferProgress(const std::string& transferId, std::uint64_t bytesTransferred) const;

	// Get temporary file path for a transfer
	std::filesystem::path getTemporaryFilePath(const std::string& transferId) const;

	// Get final destination path where file will be moved after completion
	std::filesystem::path getFinalFilePath(const std::string& transferId,
	                                       const std::string& destinationRoot) const;

	// Check if a transfer with given ID exists and is eligible for resuming
	bool canResumeTransfer(const std::string& transferId) const;

	// Mark transfer as complete and move temp file to final location with atomic rename
	bool completeTransfer(const std::string& transferId,
	                      const std::string& destinationRoot,
	                      bool verifyChecksum = false,
	                      const std::string& expectedHash = "") const;

	// Abort a transfer and clean up temporary files and state
	bool abortTransfer(const std::string& transferId) const;

	// Check if a transfer with the same file and peer already exists (duplicate detection)
	std::optional<std::string> findExistingTransfer(const std::string& filePath,
	                                               const std::string& remoteDeviceId,
	                                               bool isDownload) const;

	// Clean up old/stale transfer files and states (older than retentionDays)
	std::size_t cleanupStaleTransfers(int retentionDays = 7) const;

	// Get all active transfers
	std::vector<TransferState> getActiveTransfers() const;

	// Thread-safe access to internal state
	std::mutex& getStateMutex() { return stateMutex_; }

private:
	std::filesystem::path resumeRoot_;  // Root for storing .resume state files
	std::filesystem::path tempRoot_;    // Root for storing temporary transfer files

	// Cache of transfer states for quick lookup (protected by stateMutex_)
	mutable std::mutex stateMutex_;
	mutable std::unordered_map<std::string, TransferState> stateCache_;

	// Helper: Load all transfer states from disk into cache
	void reloadStateCache() const;

	// Helper: Get state file path for a transfer ID
	std::filesystem::path getStateFilePath(const std::string& transferId) const;

	// Helper: Parse transfer state from file
	std::optional<TransferState> parseStateFile(const std::filesystem::path& filePath) const;

	// Helper: Serialize transfer state to string
	std::string serializeState(const TransferState& state) const;

	// Helper: Extract transfer ID from filename
	std::optional<TransferState> loadFromDisk(const std::string& transferId) const;
};

}  // namespace syncflow::engine

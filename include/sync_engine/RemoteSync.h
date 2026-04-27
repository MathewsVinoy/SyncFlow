#pragma once

#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <filesystem>
#include <chrono>

namespace syncflow::engine {

// Represents metadata of a file on a device
struct RemoteFileInfo {
	std::string path;
	std::uint64_t size;
	std::uint64_t lastModifiedTime;  // Unix timestamp in milliseconds
	std::string hash;
	bool isDirectory;
	
	std::string toWireFormat() const;
	static RemoteFileInfo fromWireFormat(const std::string& wire);
};

// Represents sync action between two devices
enum class SyncAction {
	None,           // No action needed
	UploadFile,     // Send local file to remote
	DownloadFile,   // Receive remote file to local
	CreateRemoteDir, // Create directory on remote
	CreateLocalDir,  // Create directory locally
	DeleteRemote,   // Delete file on remote
	DeleteLocal     // Delete file locally
};

struct SyncPlan {
	SyncAction action;
	std::string localPath;
	std::string remotePath;
	RemoteFileInfo localInfo;
	RemoteFileInfo remoteInfo;
	
	std::string description() const;
};

// Orchestrates cross-device file synchronization based on timestamps
class RemoteSync {
public:
	RemoteSync();
	
	// Build local file metadata (similar to SyncEngine snapshot but with wire format)
	std::vector<RemoteFileInfo> getLocalFileMetadata(const std::filesystem::path& rootFolder) const;
	
	// Compare local and remote metadata, produce sync plan
	std::vector<SyncPlan> compareMeta(
		const std::vector<RemoteFileInfo>& localMeta,
		const std::vector<RemoteFileInfo>& remoteMeta,
		const std::filesystem::path& localRoot
	) const;
	
	// Encode metadata for transmission over network
	std::string encodeMetadataList(const std::vector<RemoteFileInfo>& meta) const;
	
	// Decode metadata received from network
	std::vector<RemoteFileInfo> decodeMetadataList(const std::string& encoded) const;
	
	// Get detailed info about a specific file
	RemoteFileInfo getFileInfo(const std::filesystem::path& file) const;

private:
	// Resolve conflicts between versions (returns newer timestamp)
	std::uint64_t resolveTimestampConflict(
		std::uint64_t localTime,
		std::uint64_t remoteTime
	) const;
};

} // namespace syncflow::engine

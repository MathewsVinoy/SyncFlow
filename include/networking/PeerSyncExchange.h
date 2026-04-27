#pragma once

#include "sync_engine/RemoteSync.h"
#include <string>
#include <vector>
#include <filesystem>

namespace syncflow::networking {

// Protocol for exchanging files and metadata between connected peers
class PeerSyncExchange {
public:
	PeerSyncExchange(std::string localDeviceId, std::filesystem::path syncFolder);
	
	// Build sync request message (request remote metadata)
	std::string buildMetadataRequest() const;
	
	// Build metadata response (send local metadata)
	std::string buildMetadataResponse() const;
	
	// Parse received metadata from peer
	std::vector<syncflow::engine::RemoteFileInfo> parseMetadata(const std::string& message) const;
	
	// Plan sync actions based on local and remote metadata
	std::vector<syncflow::engine::SyncPlan> planSync(
		const std::vector<syncflow::engine::RemoteFileInfo>& remoteMeta
	) const;
	
	// Build file transfer request for a specific file
	std::string buildFileTransferRequest(const std::string& filePath, std::uint64_t offset = 0) const;
	
	// Parse file chunk from network message
	struct FileChunk {
		std::string filePath;
		std::uint64_t offset;
		std::vector<char> data;
		bool isFinal;
	};
	
	FileChunk parseFileChunk(const std::string& message) const;
	
	// Build file chunk to send
	std::string buildFileChunk(
		const std::string& filePath,
		const std::vector<char>& data,
		std::uint64_t offset,
		bool isFinal
	) const;
	
	// Apply sync action (download file from peer or upload local file)
	bool applySync(
		const syncflow::engine::SyncPlan& plan,
		const std::string& fileData  // For download actions
	) const;

private:
	std::string localDeviceId_;
	std::filesystem::path syncFolder_;
	syncflow::engine::RemoteSync remoteSync_;
};

} // namespace syncflow::networking

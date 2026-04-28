#include "networking/PeerSyncExchange.h"
#include "core/Logger.h"

#include <sstream>
#include <fstream>
#include <algorithm>

namespace syncflow::networking {

PeerSyncExchange::PeerSyncExchange(std::string localDeviceId, std::filesystem::path syncFolder)
	: localDeviceId_(std::move(localDeviceId)), syncFolder_(std::move(syncFolder)) {}

std::string PeerSyncExchange::buildMetadataRequest() const {
	// Message format: REQ_META|deviceId
	return "REQ_META|" + localDeviceId_;
}

std::string PeerSyncExchange::buildMetadataResponse() const {
	// Get local metadata and encode it
	auto meta = remoteSync_.getLocalFileMetadata(syncFolder_);
	std::string encoded = remoteSync_.encodeMetadataList(meta);
	
	// Message format: RESP_META|deviceId|encodedMeta
	return "RESP_META|" + localDeviceId_ + "|" + encoded;
}

std::vector<syncflow::engine::RemoteFileInfo> PeerSyncExchange::parseMetadata(const std::string& message) const {
	// Expected format: RESP_META|deviceId|encodedMeta
	size_t pos = message.find('|');
	if (pos == std::string::npos) {
		Logger::warn("PeerSyncExchange: invalid metadata message format");
		return {};
	}
	
	pos = message.find('|', pos + 1);
	if (pos == std::string::npos) {
		Logger::warn("PeerSyncExchange: invalid metadata message format (missing encoded data)");
		return {};
	}
	
	std::string encoded = message.substr(pos + 1);
	return remoteSync_.decodeMetadataList(encoded);
}

std::vector<syncflow::engine::SyncPlan> PeerSyncExchange::planSync(
	const std::vector<syncflow::engine::RemoteFileInfo>& remoteMeta
) const {
	auto localMeta = remoteSync_.getLocalFileMetadata(syncFolder_);
	return remoteSync_.compareMeta(localMeta, remoteMeta, syncFolder_);
}

std::string PeerSyncExchange::buildFileTransferRequest(const std::string& filePath,
                                                     std::uint64_t fileSize,
                                                     std::uint64_t offset) const {
	// Message format: REQ_FILE|filePath|fileSize|offset
	Logger::debug("PeerSyncExchange: Building file transfer request - filePath: " + filePath +
	              ", fileSize: " + std::to_string(fileSize) + ", offset: " + std::to_string(offset));
	return "REQ_FILE|" + filePath + "|" + std::to_string(fileSize) + "|" + std::to_string(offset);
}

PeerSyncExchange::FileChunk PeerSyncExchange::parseFileChunk(const std::string& message) const {
	// Expected format: FILE_CHUNK|filePath|fileSize|offset|isFinal|dataSize|data
	FileChunk chunk{};
	
	std::stringstream ss(message);
	std::string part;
	std::vector<std::string> parts;
	
	while (std::getline(ss, part, '|')) {
		parts.push_back(part);
	}
	
	if (parts.size() < 6) {
		Logger::warn("PeerSyncExchange: invalid file chunk format (expected at least 6 parts)");
		return chunk;
	}
	
	try {
		// parts[0] = "FILE_CHUNK"
		chunk.filePath = parts[1];
		chunk.fileSize = std::stoull(parts[2]);
		chunk.offset = std::stoull(parts[3]);
		chunk.isFinal = (parts[4] == "1");
		(void)std::stoull(parts[5]);  // dataSize - just validate it exists
		
		// Data starts at parts[6]
		if (parts.size() > 6) {
			std::string data = parts[6];
			chunk.data = std::vector<char>(data.begin(), data.end());
		}
		
		Logger::debug("PeerSyncExchange: Parsed file chunk - filePath: " + chunk.filePath +
		              ", fileSize: " + std::to_string(chunk.fileSize) +
		              ", offset: " + std::to_string(chunk.offset) +
		              ", dataSize: " + std::to_string(chunk.data.size()) +
		              ", final: " + (chunk.isFinal ? "true" : "false"));
	} catch (const std::exception& e) {
		Logger::warn("PeerSyncExchange: Failed to parse file chunk - " + std::string(e.what()));
	}
	
	return chunk;
}

std::string PeerSyncExchange::buildFileChunk(
	const std::string& filePath,
	std::uint64_t fileSize,
	const std::vector<char>& data,
	std::uint64_t offset,
	bool isFinal
) const {
	// Message format: FILE_CHUNK|filePath|fileSize|offset|isFinal|dataSize|data
	std::string result = "FILE_CHUNK|" + filePath + "|" 
		+ std::to_string(fileSize) + "|"
		+ std::to_string(offset) + "|"
		+ (isFinal ? "1" : "0") + "|"
		+ std::to_string(data.size()) + "|";
	
	result.append(data.begin(), data.end());
	
	Logger::debug("PeerSyncExchange: Built file chunk - filePath: " + filePath +
	              ", fileSize: " + std::to_string(fileSize) +
	              ", offset: " + std::to_string(offset) +
	              ", dataSize: " + std::to_string(data.size()) +
	              ", final: " + (isFinal ? "true" : "false"));
	
	return result;
}

bool PeerSyncExchange::applySync(
	const syncflow::engine::SyncPlan& plan,
	const std::string& fileData
) const {
	using namespace syncflow::engine;
	
	switch (plan.action) {
		case RemoteSyncAction::CreateLocalDir: {
			std::filesystem::create_directories(syncFolder_ / plan.localPath);
			Logger::info("sync created local dir: " + plan.localPath);
			return true;
		}
		
		case RemoteSyncAction::DownloadFile: {
			auto targetPath = syncFolder_ / plan.localPath;
			std::filesystem::create_directories(targetPath.parent_path());
			
			std::ofstream out(targetPath, std::ios::binary);
			if (!out) {
				Logger::error("sync failed to write: " + plan.localPath);
				return false;
			}
			
			out.write(fileData.c_str(), fileData.size());
			out.close();
			
			Logger::info("sync downloaded: " + plan.localPath);
			return true;
		}
		
		case RemoteSyncAction::DeleteLocal: {
			try {
				std::filesystem::remove(syncFolder_ / plan.localPath);
				Logger::info("sync deleted local: " + plan.localPath);
				return true;
			} catch (const std::filesystem::filesystem_error& e) {
				Logger::error("sync failed to delete: " + plan.localPath);
				return false;
			}
		}
		
		default:
			Logger::warn("PeerSyncExchange: unhandled sync action in applySync");
			return false;
	}
}

} // namespace syncflow::networking

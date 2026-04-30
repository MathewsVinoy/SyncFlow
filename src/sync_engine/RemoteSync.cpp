#include "sync_engine/RemoteSync.h"
#include "core/Logger.h"
#include "sync_engine/HashUtils.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <chrono>

namespace syncflow::engine {

std::string RemoteFileInfo::toWireFormat() const {
	// Format: path|size|timestamp|hash|isDir
	std::string result = path + "|" 
		+ std::to_string(size) + "|"
		+ std::to_string(lastModifiedTime) + "|"
		+ hash + "|"
		+ (isDirectory ? "1" : "0");
	return result;
}

RemoteFileInfo RemoteFileInfo::fromWireFormat(const std::string& wire) {
	RemoteFileInfo info{};
	std::stringstream ss(wire);
	std::string part;
	
	// Parse: path|size|timestamp|hash|isDir
	if (std::getline(ss, info.path, '|')) {
		std::getline(ss, part, '|');
		info.size = std::stoull(part);
		
		std::getline(ss, part, '|');
		info.lastModifiedTime = std::stoull(part);
		
		std::getline(ss, info.hash, '|');
		
		std::getline(ss, part, '|');
		info.isDirectory = (part == "1");
	}
	
	return info;
}

std::string SyncPlan::description() const {
	std::string actionStr;
	switch (action) {
		case RemoteSyncAction::UploadFile: actionStr = "UPLOAD"; break;
		case RemoteSyncAction::DownloadFile: actionStr = "DOWNLOAD"; break;
		case RemoteSyncAction::CreateRemoteDir: actionStr = "CREATE_REMOTE_DIR"; break;
		case RemoteSyncAction::CreateLocalDir: actionStr = "CREATE_LOCAL_DIR"; break;
		case RemoteSyncAction::DeleteRemote: actionStr = "DELETE_REMOTE"; break;
		case RemoteSyncAction::DeleteLocal: actionStr = "DELETE_LOCAL"; break;
		case RemoteSyncAction::None: actionStr = "NONE"; break;
	}
	return actionStr + ": " + localPath + " <-> " + remotePath;
}

RemoteSync::RemoteSync() {}

RemoteFileInfo RemoteSync::getFileInfo(const std::filesystem::path& file) const {
	RemoteFileInfo info{};
	info.path = file.filename().string();
	info.isDirectory = std::filesystem::is_directory(file);
	
	if (std::filesystem::exists(file)) {
		info.size = info.isDirectory ? 0 : std::filesystem::file_size(file);
		
		// Get last write time in milliseconds since epoch
		auto last_write = std::filesystem::last_write_time(file);
		auto sctp = std::chrono::time_point_cast<std::chrono::milliseconds>(last_write);
		auto ms = sctp.time_since_epoch();
		info.lastModifiedTime = ms.count();
		
		// Compute hash if it's a file
		if (!info.isDirectory) {
			info.hash = getCachedOrComputeHash(file, info.size, info.lastModifiedTime);
		}
	}
	
	return info;
}

std::vector<RemoteFileInfo> RemoteSync::getLocalFileMetadata(const std::filesystem::path& rootFolder) const {
	std::vector<RemoteFileInfo> result;
	
	if (!std::filesystem::exists(rootFolder)) {
		Logger::warn("RemoteSync: root folder does not exist: " + rootFolder.string());
		return result;
	}
	
	try {
		for (const auto& entry : std::filesystem::recursive_directory_iterator(rootFolder)) {
			RemoteFileInfo info{};
			info.path = std::filesystem::relative(entry.path(), rootFolder).string();
			info.isDirectory = entry.is_directory();
			info.size = entry.is_regular_file() ? entry.file_size() : 0;
			
			// Get last write time in milliseconds
			auto last_write = entry.last_write_time();
			auto sctp = std::chrono::time_point_cast<std::chrono::milliseconds>(last_write);
			auto ms = sctp.time_since_epoch();
			info.lastModifiedTime = ms.count();
			
			// Compute hash only for files
			if (info.isDirectory) {
				info.hash = "";
			} else {
				info.hash = getCachedOrComputeHash(entry.path(), info.size, info.lastModifiedTime);
			}
			
			result.push_back(info);
		}
	} catch (const std::filesystem::filesystem_error& e) {
		Logger::error("RemoteSync: error walking directory: " + std::string(e.what()));
	}
	
	return result;
}

std::vector<SyncPlan> RemoteSync::compareMeta(
	const std::vector<RemoteFileInfo>& localMeta,
	const std::vector<RemoteFileInfo>& remoteMeta,
		const std::filesystem::path& /* localRoot */
) const {
	std::vector<SyncPlan> plans;
	
	// Create maps for quick lookup
	std::map<std::string, RemoteFileInfo> localMap;
	std::map<std::string, RemoteFileInfo> remoteMap;
	
	for (const auto& info : localMeta) {
		localMap[info.path] = info;
	}
	
	for (const auto& info : remoteMeta) {
		remoteMap[info.path] = info;
	}
	
	// Check all local files
	for (const auto& [localPath, localInfo] : localMap) {
		auto it = remoteMap.find(localPath);
		
		if (it == remoteMap.end()) {
			if (localInfo.isDirectory) {
				SyncPlan plan{
					.action = RemoteSyncAction::CreateRemoteDir,
					.localPath = localPath,
					.remotePath = localPath,
					.localInfo = localInfo,
					.remoteInfo = RemoteFileInfo{}
				};
				plans.push_back(plan);
				Logger::info("sync plan CREATE_REMOTE_DIR: " + localPath);
			} else {
				// File exists locally but not on remote → upload
				SyncPlan plan{
					.action = RemoteSyncAction::UploadFile,
					.localPath = localPath,
					.remotePath = localPath,
					.localInfo = localInfo,
					.remoteInfo = RemoteFileInfo{}
				};
				plans.push_back(plan);
				Logger::info("sync plan UPLOAD: " + localPath);
			}
		} else {
			// File exists on both sides
			const auto& remoteInfo = it->second;
			
			// Check if they're both directories or both files
			if (localInfo.isDirectory && remoteInfo.isDirectory) {
				// Both directories, no action needed
				continue;
			}
			
			if (localInfo.isDirectory != remoteInfo.isDirectory) {
				// Type mismatch - skip for safety
				Logger::warn("sync plan SKIP: type mismatch for " + localPath);
				continue;
			}
			
			if (!localInfo.hash.empty() && localInfo.hash == remoteInfo.hash) {
				continue;
			}

			// Both are files and content differs - compare timestamps
			if (localInfo.lastModifiedTime > remoteInfo.lastModifiedTime) {
				// Local file is newer → upload
				SyncPlan plan{
					.action = RemoteSyncAction::UploadFile,
					.localPath = localPath,
					.remotePath = localPath,
					.localInfo = localInfo,
					.remoteInfo = remoteInfo
				};
				plans.push_back(plan);
				Logger::info("sync plan UPLOAD (newer): " + localPath);
			} else if (remoteInfo.lastModifiedTime > localInfo.lastModifiedTime) {
				// Remote file is newer → download
				SyncPlan plan{
					.action = RemoteSyncAction::DownloadFile,
					.localPath = localPath,
					.remotePath = localPath,
					.localInfo = localInfo,
					.remoteInfo = remoteInfo
				};
				plans.push_back(plan);
				Logger::info("sync plan DOWNLOAD (newer): " + localPath);
			}
			// If timestamps are equal, no action needed
		}
	}
	
	// Check all remote files
	for (const auto& [remotePath, remoteInfo] : remoteMap) {
		auto it = localMap.find(remotePath);
		
		if (it == localMap.end()) {
			if (remoteInfo.isDirectory) {
				SyncPlan plan{
					.action = RemoteSyncAction::CreateLocalDir,
					.localPath = remotePath,
					.remotePath = remotePath,
					.localInfo = RemoteFileInfo{},
					.remoteInfo = remoteInfo
				};
				plans.push_back(plan);
				Logger::info("sync plan CREATE_LOCAL_DIR: " + remotePath);
			} else {
				// File exists on remote but not locally → download
				SyncPlan plan{
					.action = RemoteSyncAction::DownloadFile,
					.localPath = remotePath,
					.remotePath = remotePath,
					.localInfo = RemoteFileInfo{},
					.remoteInfo = remoteInfo
				};
				plans.push_back(plan);
				Logger::info("sync plan DOWNLOAD: " + remotePath);
			}
		}
	}
	
	return plans;
}

std::string RemoteSync::encodeMetadataList(const std::vector<RemoteFileInfo>& meta) const {
	// Format: count;meta1;meta2;...
	std::string result = std::to_string(meta.size());
	
	for (const auto& info : meta) {
		result += ";";
		result += info.toWireFormat();
	}
	
	return result;
}

std::vector<RemoteFileInfo> RemoteSync::decodeMetadataList(const std::string& encoded) const {
	std::vector<RemoteFileInfo> result;
	
	if (encoded.empty()) {
		return result;
	}
	
	std::stringstream ss(encoded);
	std::string part;
	
	// First part is count (skip it)
	if (!std::getline(ss, part, ';')) {
		return result;
	}
	
	// Remaining parts are file infos
	while (std::getline(ss, part, ';')) {
		if (!part.empty()) {
			result.push_back(RemoteFileInfo::fromWireFormat(part));
		}
	}
	
	return result;
}

std::uint64_t RemoteSync::resolveTimestampConflict(
	std::uint64_t localTime,
	std::uint64_t remoteTime
) const {
	// Simply return the newer timestamp
	return std::max(localTime, remoteTime);
}

std::string RemoteSync::getCachedOrComputeHash(const std::filesystem::path& file,
	                                           std::uint64_t size,
	                                           std::uint64_t modifiedTime) const {
	std::error_code ec;
	const auto canonical = std::filesystem::weakly_canonical(file, ec);
	const std::string key = ec ? file.lexically_normal().string() : canonical.string();
	{
		std::lock_guard<std::mutex> lock(cacheMutex_);
		auto it = hashCache_.find(key);
		if (it != hashCache_.end() && it->second.size == size && it->second.lastModifiedTime == modifiedTime) {
			return it->second.hash;
		}
	}

	const auto hash = syncflow::hash::hashFileSHA256(file);
	if (hash.empty()) {
		return {};
	}

	std::lock_guard<std::mutex> lock(cacheMutex_);
	hashCache_[key] = HashCacheEntry{size, modifiedTime, hash};
	return hash;
}

} // namespace syncflow::engine

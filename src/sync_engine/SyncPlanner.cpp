#include "sync_engine/SyncPlanner.h"

#include <cmath>

namespace syncflow::engine {

SyncPlanner::SyncPlanner(std::string localDeviceId, PlannerConfig config)
	: localDeviceId_(std::move(localDeviceId)), config_(config) {}

std::vector<SyncAction> SyncPlanner::plan(
	const std::unordered_map<std::string, FileMetadata>& local,
	const std::unordered_map<std::string, FileMetadata>& remote) const {
	std::vector<SyncAction> actions;
	actions.reserve(local.size() + remote.size());

	for (const auto& [path, localFile] : local) {
		auto remoteIt = remote.find(path);
		if (remoteIt == remote.end()) {
			if (!localFile.deleted) {
				actions.push_back({ActionType::Upload, path, "missing on remote"});
			}
			continue;
		}

		const auto& remoteFile = remoteIt->second;
		if (localFile.deleted || remoteFile.deleted) {
			actions.push_back(resolveDeleted(localFile, remoteFile));
			continue;
		}

		if (!localFile.hash.empty() && localFile.hash == remoteFile.hash) {
			actions.push_back({ActionType::None, path, "already in sync"});
			continue;
		}

		actions.push_back(resolveBothPresent(localFile, remoteFile));
	}

	for (const auto& [path, remoteFile] : remote) {
		if (local.find(path) == local.end() && !remoteFile.deleted) {
			actions.push_back({ActionType::Download, path, "missing locally"});
		}
	}

	return actions;
}

SyncAction SyncPlanner::resolveBothPresent(const FileMetadata& localFile, const FileMetadata& remoteFile) const {
	if (localFile.hash != remoteFile.hash) {
		const auto delta = std::llabs(localFile.modifiedUnixSeconds - remoteFile.modifiedUnixSeconds);
		if (config_.preferNewerTimestamp && delta > config_.clockSkewToleranceSeconds) {
			if (localFile.modifiedUnixSeconds > remoteFile.modifiedUnixSeconds) {
				return {ActionType::Upload, localFile.relativePath, "newer local content timestamp"};
			}
			return {ActionType::Download, localFile.relativePath, "newer remote content timestamp"};
		}

		if (localFile.lastEditorDeviceId == remoteFile.lastEditorDeviceId) {
			if (localFile.modifiedUnixSeconds >= remoteFile.modifiedUnixSeconds) {
				return {ActionType::Upload, localFile.relativePath, "same editor, local content wins"};
			}
			return {ActionType::Download, localFile.relativePath, "same editor, remote content wins"};
		}

		if (localDeviceId_ < remoteFile.lastEditorDeviceId) {
			return {ActionType::Upload, localFile.relativePath, "content conflict tiebreaker local wins"};
		}

		if (localDeviceId_ > remoteFile.lastEditorDeviceId) {
			return {ActionType::Download, localFile.relativePath, "content conflict tiebreaker remote wins"};
		}

		return {ActionType::ConflictKeepBoth, localFile.relativePath, "content conflict keep both"};
	}

	return {ActionType::None, localFile.relativePath, "hashes match"};
}

SyncAction SyncPlanner::resolveDeleted(const FileMetadata& localFile, const FileMetadata& remoteFile) const {
	if (localFile.deleted && remoteFile.deleted) {
		return {ActionType::None, localFile.relativePath, "already deleted"};
	}

	if (localFile.deleted && !remoteFile.deleted) {
		if (remoteFile.modifiedUnixSeconds > localFile.modifiedUnixSeconds + config_.clockSkewToleranceSeconds) {
			return {ActionType::Download, localFile.relativePath, "remote recreated after local delete"};
		}
		return {ActionType::DeleteRemote, localFile.relativePath, "propagate local delete"};
	}

	if (!localFile.deleted && remoteFile.deleted) {
		if (localFile.modifiedUnixSeconds > remoteFile.modifiedUnixSeconds + config_.clockSkewToleranceSeconds) {
			return {ActionType::Upload, localFile.relativePath, "local recreated after remote delete"};
		}
		return {ActionType::DeleteLocal, localFile.relativePath, "propagate remote delete"};
	}

	return {ActionType::None, localFile.relativePath, "no-op"};
}

}  // namespace syncflow::engine

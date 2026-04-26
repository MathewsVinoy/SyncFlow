#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace syncflow::engine {

struct FileMetadata {
	std::string relativePath;
	std::uint64_t size = 0;
	std::int64_t modifiedUnixSeconds = 0;
	std::string hash;
	std::string lastEditorDeviceId;
	bool deleted = false;
};

enum class ActionType {
	Upload,
	Download,
	DeleteRemote,
	DeleteLocal,
	ConflictKeepBoth,
	None
};

struct SyncAction {
	ActionType type = ActionType::None;
	std::string relativePath;
	std::string reason;
};

struct PlannerConfig {
	std::int64_t clockSkewToleranceSeconds = 3;
	bool preferNewerTimestamp = true;
};

class SyncPlanner {
public:
	explicit SyncPlanner(std::string localDeviceId, PlannerConfig config = {});

	std::vector<SyncAction> plan(
		const std::unordered_map<std::string, FileMetadata>& local,
		const std::unordered_map<std::string, FileMetadata>& remote) const;

private:
	SyncAction resolveBothPresent(const FileMetadata& localFile, const FileMetadata& remoteFile) const;
	SyncAction resolveDeleted(const FileMetadata& localFile, const FileMetadata& remoteFile) const;

	std::string localDeviceId_;
	PlannerConfig config_;
};

}  // namespace syncflow::engine

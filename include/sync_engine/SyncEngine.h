#pragma once

#include "core/ThreadPool.h"
#include "platform/FolderWatcher.h"
#include "sync_engine/SyncPlanner.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

class SyncEngine {
public:
	struct TransferTask {
		syncflow::engine::ActionType action = syncflow::engine::ActionType::None;
		std::string remoteDeviceId;
		std::string relativePath;
		std::string reason;
		std::int64_t decidedUnixSeconds = 0;
	};

	using TransferHandler = std::function<void(const TransferTask&)>;

	struct FileEntry {
		std::uintmax_t size = 0;
		std::filesystem::file_time_type modifiedAt{};
		std::int64_t modifiedUnixSeconds = 0;
		std::string hash;
	};

	struct HashCacheEntry {
		std::uintmax_t size = 0;
		std::filesystem::file_time_type modifiedAt{};
		std::string hash;
	};

	explicit SyncEngine(std::filesystem::path sourceFolder,
	                   std::filesystem::path mirrorFolder = {});
	~SyncEngine();

	bool start();
	void stop();

	std::optional<std::string> pollEvent();
	std::filesystem::path sourceFolder() const;
	std::filesystem::path mirrorFolder() const;
	std::size_t trackedFileCount() const;
	std::unordered_map<std::string, syncflow::engine::FileMetadata> localMetadata(
		const std::string& localDeviceId) const;
	std::vector<syncflow::engine::SyncAction> planAgainstRemote(
		const std::unordered_map<std::string, syncflow::engine::FileMetadata>& remote,
		const std::string& localDeviceId) const;
	void evaluateRemoteMetadata(
		const std::string& remoteDeviceId,
		const std::unordered_map<std::string, syncflow::engine::FileMetadata>& remote,
		const std::string& localDeviceId);
	void setTransferHandler(TransferHandler handler);
	std::optional<TransferTask> pollTransferTask();

private:
	void dispatchTransferLoop();
	void runLoop();
	void scanAndSync();
	void recordEvent(const std::string& event);
	void markRecentLocalChange(const std::string& relativePath);
	bool isRecentlyChangedLocally(const std::string& relativePath) const;
	bool enqueueTransferTask(const TransferTask& task);
	void resolveConflictKeepBoth(const std::string& relativePath, const std::string& localDeviceId);
	std::string buildRemoteDigest(const std::unordered_map<std::string, syncflow::engine::FileMetadata>& remote) const;
	void copyFileAsync(const std::filesystem::path& source, const std::filesystem::path& destination);
	void archiveVersionIfExists(const std::filesystem::path& mirroredFile, const std::string& relativePath);
	std::unordered_map<std::string, FileEntry> buildSnapshot() const;
	static bool isInsideMirror(const std::filesystem::path& mirrorRoot, const std::filesystem::path& candidate);
	static std::int64_t toUnixSeconds(std::filesystem::file_time_type timePoint);

	std::filesystem::path sourceFolder_;
	std::filesystem::path mirrorFolder_;
	std::filesystem::path versionFolder_;
	ThreadPool workers_;
	std::unique_ptr<FolderWatcher> watcher_;

	mutable std::mutex stateMutex_;
	bool running_ = false;
	std::thread loopThread_;
	std::thread transferDispatchThread_;
	std::deque<std::string> events_;
	std::unordered_map<std::string, FileEntry> lastSnapshot_;
	mutable std::unordered_map<std::string, HashCacheEntry> hashCache_;
	std::unordered_map<std::string, std::chrono::steady_clock::time_point> recentLocalChanges_;
	std::chrono::milliseconds interval_{1500};
	std::chrono::seconds loopPreventionWindow_{8};

	mutable std::mutex transferMutex_;
	std::condition_variable transferCv_;
	TransferHandler transferHandler_;
	std::deque<TransferTask> transferQueue_;
	std::unordered_set<std::string> queuedTransferKeys_;
	std::unordered_map<std::string, std::string> remoteMetadataDigests_;
	std::size_t transferQueueMaxSize_ = 2048;
};
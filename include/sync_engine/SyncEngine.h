#pragma once

#include "core/ThreadPool.h"
#include "sync_engine/SyncPlanner.h"

#include <chrono>
#include <cstdint>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

class SyncEngine {
public:
	struct FileEntry {
		std::uintmax_t size = 0;
		std::filesystem::file_time_type modifiedAt{};
		std::int64_t modifiedUnixSeconds = 0;
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

private:
	void runLoop();
	void scanAndSync();
	void recordEvent(const std::string& event);
	void copyFileAsync(const std::filesystem::path& source, const std::filesystem::path& destination);
	void archiveVersionIfExists(const std::filesystem::path& mirroredFile, const std::string& relativePath);
	std::unordered_map<std::string, FileEntry> buildSnapshot() const;
	static bool isInsideMirror(const std::filesystem::path& mirrorRoot, const std::filesystem::path& candidate);
	static std::int64_t toUnixSeconds(std::filesystem::file_time_type timePoint);

	std::filesystem::path sourceFolder_;
	std::filesystem::path mirrorFolder_;
	std::filesystem::path versionFolder_;
	ThreadPool workers_;

	mutable std::mutex stateMutex_;
	bool running_ = false;
	std::thread loopThread_;
	std::deque<std::string> events_;
	std::unordered_map<std::string, FileEntry> lastSnapshot_;
	std::chrono::milliseconds interval_{1500};
};